#include "core.hpp"
#include <QDir>
#include <QFontDatabase>
#include <QThread>
#include <QtConcurrent>
#include <QThreadPool>
#include <QPrinter>
#include <QSvgGenerator>
#include <QPainterPath>
#include <QLinearGradient>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <set>
#include <sstream>
#include <stdexcept>
#include <QImageReader>
#include <QMutex>
#include <unordered_map>

namespace HandWrite {

//=============================================================================
// 工具函数
//=============================================================================

QString HandwriteGenerator::convertChinesePunctuation(const QString& text) {
    QString result = text;
    result.replace(QChar(0x2018), QChar(','));
    result.replace(QChar(0x2019), QChar(','));
    result.replace(QChar(0x201C), QChar('"'));
    result.replace(QChar(0x201D), QChar('"'));
    result.replace(QChar(0x3000), QChar(' '));
    result.replace(QChar(0x3001), QChar(','));
    result.replace(QChar(0x3002), QChar('.'));
    result.replace(QChar(0xFF01), QChar('!'));
    result.replace(QChar(0xFF0C), QChar(','));
    result.replace(QChar(0xFF1A), QChar(':'));
    result.replace(QChar(0xFF1B), QChar(';'));
    result.replace(QChar(0xFF1F), QChar('?'));
    result.replace(QChar(0xFF08), QChar('('));
    result.replace(QChar(0xFF09), QChar(')'));
    result.replace(QChar(0xFF3B), QChar('['));
    result.replace(QChar(0xFF3D), QChar(']'));
    result.replace(QChar(0x300A), QChar('<'));
    result.replace(QChar(0x300B), QChar('>'));
    return result;
}

// 仅清理本程序生成的页码 PNG（避免误删用户目录中的其他文件）
static void cleanGeneratedPages(const QString& qDir) {
    QDir dir(qDir);
    if (!dir.exists()) return;
    const QStringList entries = dir.entryList(QStringList() << "*.png", QDir::Files);
    for (const QString& name : entries) {
        QString base = name.left(name.lastIndexOf('.'));
        bool ok = false;
        base.toInt(&ok);
        if (ok) dir.remove(name);
    }
}

// 按「原文索引」找字符级覆盖（索引 -1 表示程序插入的字符，不参与匹配）
static CharacterOverride findCharOverride(int origIndex, const std::vector<CharacterOverrideRange>& overrides) {
    for (const auto& range : overrides) {
        if (range.contains(origIndex)) {
            return range.override;
        }
    }
    return CharacterOverride();
}

//=============================================================================
// Markdown 轻标记解析
//=============================================================================

std::vector<StyledSpan> HandwriteGenerator::parseMarkdown(const QString& text) {
    std::vector<StyledSpan> spans;
    QString current;
    TextStyle currentStyle = TextStyle::Normal;
    int i = 0;
    int spanStart = -1;   // current 中首个字符在原文里的索引
    
    auto flush = [&]() {
        if (!current.isEmpty()) {
            StyledSpan span;
            span.style = currentStyle;
            span.text = current;
            span.origStart = spanStart;
            if (currentStyle == TextStyle::Heading) span.fontSizeOverride = 48;
            else if (currentStyle == TextStyle::SubHeading) span.fontSizeOverride = 36;
            else if (currentStyle == TextStyle::Strikethrough) span.strikethrough = true;
            spans.push_back(span);
            current.clear();
            spanStart = -1;
        }
    };
    // 记录首个字符的原文索引，供「渲染索引 → 原文索引」映射使用
    auto appendChar = [&](QChar ch, int origIndex) {
        if (current.isEmpty()) spanStart = origIndex;
        current += ch;
    };
    
    while (i < text.length()) {
        // 分割线（仅行首，避免正文中的 "---" 被误判）
        const bool atLineStart = (i == 0 || text[i-1] == '\n');
        if (atLineStart && (text.mid(i, 3) == "---" || text.mid(i, 3) == "***")) {
            flush();
            StyledSpan sep;
            sep.style = TextStyle::Separator;
            sep.text = "---";
            sep.origStart = i;
            spans.push_back(sep);
            i += 3;
            while (i < text.length() && text[i] == '-') i++;
            continue;
        }
        
        // 标题 # 
        if (i == 0 || (i > 0 && text[i-1] == '\n')) {
            int hashCount = 0;
            while (i + hashCount < text.length() && text[i + hashCount] == '#') hashCount++;
            if (hashCount > 0 && i + hashCount < text.length() && text[i + hashCount] == ' ') {
                flush();
                currentStyle = (hashCount == 1) ? TextStyle::Heading : TextStyle::SubHeading;
                i += hashCount + 1;
                continue;
            }
        }
        
        // 删除线 ~~text~~
        // 同一个 "~~" 既是开标记也是闭标记，按当前状态切换；
        // 旧实现用两个 if 分别处理，闭合分支被开启分支完全包含且前者 continue，
        // 导致闭合永不生效、同行后续文本被连带划掉。
        if (i + 1 < text.length() && text[i] == '~' && text[i+1] == '~') {
            flush();
            currentStyle = (currentStyle == TextStyle::Strikethrough)
                         ? TextStyle::Normal
                         : TextStyle::Strikethrough;
            i += 2;
            continue;
        }
        
        // 换行重置样式
        if (text[i] == '\n') {
            flush();
            currentStyle = TextStyle::Normal;
            StyledSpan newline;
            newline.style = TextStyle::Normal;
            newline.text = QChar('\n');
            newline.origStart = i;
            spans.push_back(newline);
            i++;
            continue;
        }
        
        appendChar(text[i], i);
        i++;
    }
    flush();
    return spans;
}

//=============================================================================
// 字符覆盖序列化（GUI / CLI 共用）
//=============================================================================

std::string HandwriteGenerator::serializeCharOverride(const CharacterOverrideRange& r) {
    auto num = [](double v) { std::ostringstream o; o << v; return o.str(); };
    std::string s = std::to_string(r.startIndex) + "," + std::to_string(r.endIndex) + ",";
    s += r.override.fontSize ? std::to_string(*r.override.fontSize) : "";
    s += ",";
    s += r.override.perturbX ? num(*r.override.perturbX) : "";
    s += ",";
    s += r.override.perturbY ? num(*r.override.perturbY) : "";
    s += ",";
    s += r.override.perturbTheta ? num(*r.override.perturbTheta) : "";
    s += ",";
    if (r.override.fillColor) {
        s += std::to_string(r.override.fillColor->r) + "," +
             std::to_string(r.override.fillColor->g) + "," +
             std::to_string(r.override.fillColor->b) + "," +
             std::to_string(r.override.fillColor->a);
    } else {
        s += ",,,";
    }
    return s;
}

std::optional<CharacterOverrideRange> HandwriteGenerator::deserializeCharOverride(const std::string& s) {
    std::vector<std::string> parts;
    std::string cur;
    for (char ch : s) {
        if (ch == ',') { parts.push_back(cur); cur.clear(); } else { cur += ch; }
    }
    parts.push_back(cur);
    if (parts.size() < 10) return std::nullopt;

    auto toInt = [](const std::string& t) -> std::optional<int> {
        if (t.empty()) return std::nullopt;
        try { return std::stoi(t); } catch (...) { return std::nullopt; }
    };
    auto toDbl = [](const std::string& t) -> std::optional<double> {
        if (t.empty()) return std::nullopt;
        try { return std::stod(t); } catch (...) { return std::nullopt; }
    };

    CharacterOverrideRange r;
    const auto s0 = toInt(parts[0]), s1 = toInt(parts[1]);
    if (!s0 || !s1) return std::nullopt;
    r.startIndex = *s0;
    r.endIndex = *s1;
    if (auto v = toInt(parts[2])) r.override.fontSize = *v;
    if (auto v = toDbl(parts[3])) r.override.perturbX = *v;
    if (auto v = toDbl(parts[4])) r.override.perturbY = *v;
    if (auto v = toDbl(parts[5])) r.override.perturbTheta = *v;
    const auto cr = toInt(parts[6]), cg = toInt(parts[7]);
    const auto cb = toInt(parts[8]), ca = toInt(parts[9]);
    if (cr && cg && cb && ca) {
        r.override.fillColor = Color(static_cast<unsigned char>(*cr), static_cast<unsigned char>(*cg),
                                     static_cast<unsigned char>(*cb), static_cast<unsigned char>(*ca));
    }
    if (r.override.isEmpty()) return std::nullopt;
    return r;
}

//=============================================================================
// 构造函数 & 简单方法
//=============================================================================

HandwriteGenerator::HandwriteGenerator() {
    std::random_device rd;
    m_rng.seed(rd());
    auto ttfFiles = BasicTools().getTtfFiles();
    if (!ttfFiles.second.empty()) {
        m_params.fontPath = ttfFiles.second[0];
    }
}

void HandwriteGenerator::modifyTemplateParams(const TemplateParams& params) { m_params = params; }
void HandwriteGenerator::setFont(const std::string& path, int size) { m_params.fontPath = path; m_params.fontSize = size; }

//=============================================================================
// 随机数
//=============================================================================

double HandwriteGenerator::gaussianRandom(double sigma) {
    if (sigma <= 0) return 0.0;
    std::normal_distribution<double> dist(0.0, sigma);
    return dist(m_rng);
}
int HandwriteGenerator::gaussianRandomInt(double sigma) {
    return static_cast<int>(std::round(gaussianRandom(sigma)));
}
double HandwriteGenerator::gaussianRandomStatic(double sigma, std::mt19937& rng) {
    if (sigma <= 0) return 0.0;
    std::normal_distribution<double> dist(0.0, sigma);
    return dist(rng);
}
int HandwriteGenerator::gaussianRandomIntStatic(double sigma, std::mt19937& rng) {
    return static_cast<int>(std::round(gaussianRandomStatic(sigma, rng)));
}

bool HandwriteGenerator::isStartChar(QChar c) const { return QString::fromStdString(m_params.startChars).contains(c); }
bool HandwriteGenerator::isEndChar(QChar c) const { return QString::fromStdString(m_params.endChars).contains(c); }

//=============================================================================
// 纸张纹理绘制
//=============================================================================

void HandwriteGenerator::drawPaperTexture(QPainter& painter, int width, int height,
                                           PaperTexture texture, int rate, double opacity) {
    if (texture == PaperTexture::None) return;
    
    painter.save();
    QPen linePen(QColor(180, 180, 200, static_cast<int>(opacity * 255)));
    linePen.setWidth(1);
    painter.setPen(linePen);
    
    int gridSize = 25 * rate; // 5mm at 96DPI * rate ≈ 25px * rate
    
    switch (texture) {
        case PaperTexture::HorizontalLine:
            for (int y = 0; y < height; y += gridSize) {
                painter.drawLine(0, y, width, y);
            }
            break;
            
        case PaperTexture::Grid:
            for (int y = 0; y < height; y += gridSize)
                painter.drawLine(0, y, width, y);
            for (int x = 0; x < width; x += gridSize)
                painter.drawLine(x, 0, x, height);
            break;
            
        case PaperTexture::TianZiGe: {
            // 田字格：大方格 + 十字虚线
            int cellSize = gridSize;
            QPen dashPen(QColor(200, 200, 220, static_cast<int>(opacity * 200)));
            dashPen.setStyle(Qt::DotLine);
            dashPen.setWidth(1);
            for (int y = 0; y < height; y += cellSize) {
                painter.setPen(linePen);
                painter.drawLine(0, y, width, y);
                painter.setPen(dashPen);
                painter.drawLine(0, y + cellSize/2, width, y + cellSize/2);
            }
            for (int x = 0; x < width; x += cellSize) {
                painter.setPen(linePen);
                painter.drawLine(x, 0, x, height);
                painter.setPen(dashPen);
                painter.drawLine(x + cellSize/2, 0, x + cellSize/2, height);
            }
            break;
        }
            
        case PaperTexture::Composition: {
            // 作文纸：上方标题区 + 方格正文
            int headerHeight = 80 * rate;
            QPen redPen(QColor(220, 100, 100, static_cast<int>(opacity * 255)));
            redPen.setWidth(2);
            painter.setPen(redPen);
            painter.drawLine(0, headerHeight, width, headerHeight);
            // 评分区文字
            painter.setFont(QFont("SimHei", 10 * rate));
            painter.drawText(10, headerHeight - 5, "题目:");
            painter.drawText(width/2, headerHeight - 5, "分数:");
            // 方格
            int cellSize = 28 * rate;
            painter.setPen(linePen);
            for (int y = headerHeight + cellSize; y < height; y += cellSize)
                for (int x = 0; x < width; x += cellSize)
                    painter.drawRect(x, y, cellSize, cellSize);
            break;
        }
            
        case PaperTexture::DotGrid:
            for (int y = gridSize; y < height; y += gridSize) {
                for (int x = gridSize; x < width; x += gridSize) {
                    painter.drawPoint(x, y);
                }
            }
            break;
            
        default:
            break;
    }
    
    painter.restore();
}

//=============================================================================
// 字体混合选择
//=============================================================================

// 带缓存的字体注册：每个路径只调用 addApplicationFont 一次（线程安全）
static QString getCachedFontFamily(const std::string& path) {
    static std::unordered_map<std::string, QString> s_cache;
    static QMutex s_mutex;
    QMutexLocker lock(&s_mutex);
    auto it = s_cache.find(path);
    if (it != s_cache.end()) return it->second;
    QString family;
    int id = QFontDatabase::addApplicationFont(QString::fromStdString(path));
    if (id != -1) {
        QStringList fams = QFontDatabase::applicationFontFamilies(id);
        if (!fams.isEmpty()) family = fams[0];
    }
    s_cache.emplace(path, family);
    return family;
}

QFont HandwriteGenerator::pickMixedFont(const QFont& baseFont,
                                         const std::vector<std::string>& fontMixList,
                                         double mixRate, std::mt19937& rng) {
    if (fontMixList.empty() || baseFont.pixelSize() <= 0) return baseFont;
    mixRate = qBound(0.0, mixRate, 1.0);
    if (mixRate <= 0.0) return baseFont;

    // 使用页面级 rng（旧实现用 thread_local 随机源，导致同一份文档
    // 在不同线程数/不同调用顺序下结果不同，预览与导出无法复现）
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    if (dist(rng) >= mixRate) return baseFont;

    size_t idx = static_cast<size_t>(dist(rng) * fontMixList.size());
    if (idx >= fontMixList.size()) idx = fontMixList.size() - 1;
    QString family = getCachedFontFamily(fontMixList[idx]);
    if (!family.isEmpty() && family != baseFont.family()) {
        QFont mixed(family);
        mixed.setPixelSize(baseFont.pixelSize());
        return mixed;
    }
    return baseFont;
}

//=============================================================================
// 墨水洇染效果
//=============================================================================

void HandwriteGenerator::applyInkBleed(QImage& image, double radius) {
    if (radius <= 0) return;
    int r = static_cast<int>(std::ceil(radius));
    if (r <= 0) return;
    const int w = image.width(), h = image.height();
    if (w <= 0 || h <= 0) return;

    // 1) 统计不透明像素的平均墨色（仅一次 O(W*H) 遍历）
    qulonglong ar = 0, ag = 0, ab = 0, ac = 0;
    for (int y = 0; y < h; ++y) {
        const unsigned char* s = image.constScanLine(y);
        for (int x = 0; x < w; ++x)
            if (s[x*4+3] > 128) { ar += s[x*4]; ag += s[x*4+1]; ab += s[x*4+2]; ++ac; }
    }
    if (ac == 0) return;
    const int mr = static_cast<int>(ar / ac);
    const int mg = static_cast<int>(ag / ac);
    const int mb = static_cast<int>(ab / ac);

    // 2) 可分离最大值滤波（膨胀）alpha 通道：O(W*H*r)，远优于原始 O(W*H*r^2)
    QImage alphaBuf(w, h, QImage::Format_Alpha8);
    std::vector<unsigned char> hbuf(static_cast<size_t>(w) * h, 0);
    for (int y = 0; y < h; ++y) {
        const unsigned char* s = image.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            unsigned char mx = s[x*4+3];
            int lo = std::max(0, x - r);
            int hi = std::min(w - 1, x + r);
            for (int xx = lo; xx <= hi; ++xx)
                mx = std::max(mx, s[xx*4+3]);
            hbuf[static_cast<size_t>(y) * w + x] = mx;
        }
    }
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            unsigned char mx = 0;
            int lo = std::max(0, y - r);
            int hi = std::min(h - 1, y + r);
            for (int yy = lo; yy <= hi; ++yy)
                mx = std::max(mx, hbuf[static_cast<size_t>(yy) * w + x]);
            *(alphaBuf.scanLine(y) + x) = mx;
        }
    }

    // 3) 为"原始透明、但邻居有墨"的像素填充淡墨色
    const int bleedAlpha = qMin(40, 255 / (r * 2));
    for (int y = 0; y < h; ++y) {
        unsigned char* d = image.scanLine(y);
        const unsigned char* a = alphaBuf.constScanLine(y);
        for (int x = 0; x < w; ++x)
            if (d[x*4+3] == 0 && a[x] > 128) {
                d[x*4]   = static_cast<unsigned char>(mr);
                d[x*4+1] = static_cast<unsigned char>(mg);
                d[x*4+2] = static_cast<unsigned char>(mb);
                d[x*4+3] = static_cast<unsigned char>(bleedAlpha);
            }
    }
}

//=============================================================================
// 文本布局（支持段落缩进和段间距）
//=============================================================================

std::vector<LineLayout> HandwriteGenerator::layoutText(const QString& text, const QFont& font,
                                                        int maxLineWidth, int scaledLineSpacing,
                                                        int scaledWordSpacing) {
    (void)scaledLineSpacing;  // 行距在 layoutPages 中按行应用
    std::vector<LineLayout> lines;

    // 三种字号各建一个度量对象，全部用值语义。
    // 旧实现每个带字号覆盖的 span 都 new 一个 QFontMetrics 且不把指针复位，
    // 既泄漏内存，又让标题的字号度量串到后续正文上（正文被提前换行）。
    QFont headingFont = font, subHeadingFont = font;
    headingFont.setPixelSize(48 * m_params.rate);
    subHeadingFont.setPixelSize(36 * m_params.rate);
    const QFontMetrics baseFm(font);
    const QFontMetrics headingFm(headingFont);
    const QFontMetrics subHeadingFm(subHeadingFont);

    const auto spans = parseMarkdown(text);

    const int perturbationMargin = static_cast<int>(m_params.fontSizeSigma * m_params.rate * 0.5) +
                                   static_cast<int>(m_params.perturbXSigma * m_params.rate) +
                                   static_cast<int>(m_params.wordSpacingSigma * m_params.rate * 2);
    int effectiveWidth = maxLineWidth - perturbationMargin;
    if (effectiveWidth < maxLineWidth / 2) effectiveWidth = maxLineWidth / 2;

    // 段首缩进宽度（两个汉字）
    const int indentWidth = m_params.paragraphIndent
                          ? baseFm.horizontalAdvance(QStringLiteral("哈")) * 2 : 0;

    LineLayout current;
    int currentWidth = 0;
    int lineStyleSize = 0;      // 当前行继承的标题字号（0 = 正文字号）
    bool newParagraph = true;   // 第一行是段首

    auto flushLine = [&]() {
        current.fontSizeOverride = lineStyleSize;
        lines.push_back(std::move(current));
        current = LineLayout();
        currentWidth = 0;
        lineStyleSize = 0;
    };

    for (const auto& span : spans) {
        if (span.style == TextStyle::Separator) {
            if (!current.text.isEmpty()) flushLine();
            LineLayout sep;
            sep.text = QStringLiteral("---");
            sep.origIndices.assign(3, -1);
            lines.push_back(std::move(sep));
            newParagraph = true;
            continue;
        }

        const bool isHeading = (span.style == TextStyle::Heading);
        const int spanSize = span.fontSizeOverride;
        const QFontMetrics& fm = isHeading ? headingFm
                              : (spanSize > 0 ? subHeadingFm : baseFm);

        const QString converted = m_params.preserveChinesePunctuation
                                ? span.text : convertChinesePunctuation(span.text);

        for (int i = 0; i < converted.length(); ++i) {
            const QChar c = converted[i];
            const int origIndex = (span.origStart >= 0) ? span.origStart + i : -1;
            // 禁则判断用原文标点：转换后标点会变成 ASCII，用原文更稳妥
            const QChar origChar = (i < span.text.length()) ? span.text[i] : c;

            if (c == QLatin1Char('\n')) {
                flushLine();
                newParagraph = true;
                continue;
            }

            const int charWidth = fm.horizontalAdvance(c) + scaledWordSpacing;

            // 段首缩进：标题不缩进；插入的全角空格映射为 -1，不参与字符覆盖
            if (newParagraph && current.text.isEmpty() && indentWidth > 0 && spanSize == 0) {
                current.text += QString(QChar(0x3000)) + QChar(0x3000);
                current.origIndices.push_back(-1);
                current.origIndices.push_back(-1);
                currentWidth = indentWidth;
            }
            newParagraph = false;

            // 换行判定 + 中文避头尾
            if (currentWidth + charWidth > effectiveWidth && !current.text.isEmpty()) {
                if (isEndChar(origChar)) {
                    // 行尾禁则：标点跟随上一行，避免标点孤悬于行首
                    current.text += c;
                    current.origIndices.push_back(origIndex);
                    flushLine();
                    continue;
                }
                if (isStartChar(current.text.back())) {
                    // 行首禁则：把行尾的起始标点挪到下一行
                    const QChar lastChar = current.text.back();
                    const int lastOrig = current.origIndices.back();
                    current.text.chop(1);
                    current.origIndices.pop_back();
                    flushLine();
                    current.text += lastChar;
                    current.origIndices.push_back(lastOrig);
                    currentWidth = fm.horizontalAdvance(lastChar) + scaledWordSpacing;
                } else {
                    flushLine();
                }
            }

            if (spanSize > 0 && lineStyleSize == 0) lineStyleSize = spanSize;
            current.text += c;
            current.origIndices.push_back(origIndex);
            currentWidth += charWidth;
        }
    }

    if (!current.text.isEmpty()) flushLine();
    return lines;
}

//=============================================================================
// 横线导引几何
//=============================================================================
// 全部是纯函数，可直接在单元测试里验证。

void GuideCurve::sample(qreal x, qreal* y, qreal* angle) const {
    if (y) *y = 0.0;
    if (angle) *angle = 0.0;
    if (pts.empty()) return;

    if (pts.size() == 1) {
        if (y) *y = pts.front().y();
        return;
    }

    // 端点外夹取：不做外推，否则边缘会出现离谱的 y
    if (x <= pts.front().x()) {
        if (y) *y = pts.front().y();
        if (angle) *angle = std::atan2(pts[1].y() - pts[0].y(), pts[1].x() - pts[0].x());
        return;
    }
    if (x >= pts.back().x()) {
        if (y) *y = pts.back().y();
        const size_t n = pts.size();
        if (angle) *angle = std::atan2(pts[n-1].y() - pts[n-2].y(), pts[n-1].x() - pts[n-2].x());
        return;
    }

    // 线性查找所在段（点数通常 < 100，二分不值得；热路径也只是每字符一次）
    size_t i = 0;
    while (i + 2 < pts.size() && pts[i + 1].x() < x) ++i;

    const QPointF& a = pts[i];
    const QPointF& b = pts[i + 1];
    const qreal dx = b.x() - a.x();
    if (std::abs(dx) < 1e-6) {
        if (y) *y = b.y();
        if (angle) *angle = 0.0;
        return;
    }
    const qreal t = (x - a.x()) / dx;
    if (y) *y = a.y() + (b.y() - a.y()) * t;
    if (angle) *angle = std::atan2(b.y() - a.y(), dx);
}

void GuideCurve::normalize(int smoothPasses) {
    if (pts.size() < 2) return;

    // 1) 按 x 升序
    std::stable_sort(pts.begin(), pts.end(),
                     [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });

    // 2) 合并 x 过近的点（取 y 均值），避免出现零长度的段
    std::vector<QPointF> merged;
    merged.reserve(pts.size());
    for (const QPointF& p : pts) {
        if (!merged.empty() && std::abs(p.x() - merged.back().x()) < 1.0) {
            merged.back().setY((merged.back().y() + p.y()) * 0.5);
            continue;
        }
        merged.push_back(p);
    }
    if (merged.size() < 2) { pts = merged; return; }

    // 3) 移动平均平滑（端点保持不变，否则手绘的首尾会被拉偏）
    for (int pass = 0; pass < smoothPasses; ++pass) {
        std::vector<QPointF> next = merged;
        for (size_t i = 1; i + 1 < merged.size(); ++i) {
            next[i].setY((merged[i-1].y() + merged[i].y() + merged[i+1].y()) / 3.0);
        }
        merged = std::move(next);
    }
    pts = std::move(merged);
}

std::vector<QPointF> GuideCurve::resampleByArcLength(int n) const {
    std::vector<QPointF> out;
    if (pts.empty() || n < 2) return out;

    // 累计弧长
    std::vector<qreal> acc(pts.size(), 0.0);
    for (size_t i = 1; i < pts.size(); ++i) {
        const qreal dx = pts[i].x() - pts[i-1].x();
        const qreal dy = pts[i].y() - pts[i-1].y();
        acc[i] = acc[i-1] + std::sqrt(dx*dx + dy*dy);
    }
    const qreal total = acc.back();
    if (total < 1e-6) {
        // 退化成一点：全部输出同一点
        out.assign(static_cast<size_t>(n), pts.front());
        return out;
    }

    out.reserve(static_cast<size_t>(n));
    size_t seg = 1;
    for (int k = 0; k < n; ++k) {
        const qreal target = total * k / static_cast<qreal>(n - 1);
        while (seg + 1 < pts.size() && acc[seg] < target) ++seg;
        const qreal a0 = acc[seg-1], a1 = acc[seg];
        const qreal t = (a1 - a0) < 1e-9 ? 0.0 : (target - a0) / (a1 - a0);
        const QPointF& p0 = pts[seg-1];
        const QPointF& p1 = pts[seg];
        out.push_back(QPointF(p0.x() + (p1.x() - p0.x()) * t,
                              p0.y() + (p1.y() - p0.y()) * t));
    }
    return out;
}

std::vector<GuideCurve> parseGuideCurves(const std::vector<double>& flat) {
    std::vector<GuideCurve> out;
    size_t i = 0;
    while (i < flat.size()) {
        const double nRaw = flat[i++];
        const int n = static_cast<int>(nRaw);
        // n 必须是正整数，且剩余数据足够；否则说明文件被改坏了，停止解析
        if (n < 2 || nRaw != static_cast<double>(n) || i + static_cast<size_t>(n) * 2 > flat.size()) {
            break;
        }
        GuideCurve c;
        c.pts.reserve(static_cast<size_t>(n));
        for (int j = 0; j < n; ++j) {
            const double x = flat[i + static_cast<size_t>(j) * 2];
            const double y = flat[i + static_cast<size_t>(j) * 2 + 1];
            c.pts.push_back(QPointF(x, y));
        }
        i += static_cast<size_t>(n) * 2;
        out.push_back(std::move(c));
    }
    return out;
}

std::vector<double> flattenGuideCurves(const std::vector<GuideCurve>& curves) {
    std::vector<double> out;
    for (const GuideCurve& c : curves) {
        out.push_back(static_cast<double>(c.pts.size()));
        for (const QPointF& p : c.pts) {
            out.push_back(p.x());
            out.push_back(p.y());
        }
    }
    return out;
}

namespace {

// 曲线整体的纵向位置（用点集 y 均值）。排序关键曲线时用它做键。
qreal guideMeanY(const GuideCurve& c) {
    if (c.pts.empty()) return 0.0;
    qreal s = 0.0;
    for (const QPointF& p : c.pts) s += p.y();
    return s / static_cast<qreal>(c.pts.size());
}

// 把关键曲线整理成「可插值」的形式：
//   · 丢掉不可用的（少于 2 点）
//   · 每条按 x 升序 + 去重（sample() 的前置条件，不平滑，保持原始形状）
//   · 整组按垂直位置排序 —— 插值必须发生在空间相邻的两条之间。
// 这一步不能只靠 UI 做：CLI 从预设文件读进来的顺序完全取决于文件内容，
// 顺序一乱就会在两段相距很远的曲线之间插值，位置全错。
std::vector<GuideCurve> prepareKeyCurves(const std::vector<GuideCurve>& in) {
    std::vector<GuideCurve> out;
    out.reserve(in.size());
    for (const GuideCurve& c : in) {
        if (!c.usable()) continue;
        GuideCurve n = c;
        n.normalize(0);
        if (!n.usable()) continue;
        out.push_back(std::move(n));
    }
    std::stable_sort(out.begin(), out.end(), [](const GuideCurve& a, const GuideCurve& b) {
        return guideMeanY(a) < guideMeanY(b);
    });
    return out;
}

} // namespace

std::vector<GuideCurve> LineGuideSet::build() const {
    std::vector<GuideCurve> out;
    if (keyCurves.empty()) return out;

    const std::vector<GuideCurve> keys = prepareKeyCurves(keyCurves);

    // 不插值：手绘/检测出几条就用几条
    if (!useInterpolation || keys.size() < 2 || lineCount < 2) {
        return keys;
    }

    // 插值前先把各条关键曲线重采样到相同点数，逐点线性混合才有意义
    const int N = 64;
    std::vector<std::vector<QPointF>> resampled;
    resampled.reserve(keys.size());
    for (const GuideCurve& c : keys) {
        resampled.push_back(c.resampleByArcLength(N));
    }

    const int K = static_cast<int>(resampled.size());
    out.reserve(static_cast<size_t>(lineCount));
    for (int i = 0; i < lineCount; ++i) {
        // 把 i 映射到关键曲线区间 [k, k+1] 上的局部比例 t
        const qreal f = static_cast<qreal>(i) / (lineCount - 1) * (K - 1);
        int k = static_cast<int>(std::floor(f));
        if (k > K - 2) k = K - 2;
        if (k < 0) k = 0;
        const qreal t = f - k;

        GuideCurve c;
        c.pts.reserve(static_cast<size_t>(N));
        for (int j = 0; j < N; ++j) {
            c.pts.push_back(resampled[static_cast<size_t>(k)][static_cast<size_t>(j)] * (1.0 - t)
                          + resampled[static_cast<size_t>(k) + 1][static_cast<size_t>(j)] * t);
        }
        out.push_back(std::move(c));
    }
    return out;
}

// =============================================================================
// 横线自动检测
// =============================================================================
// 全部是纯函数，可在单元测试里直接喂合成的 QImage 验证。

namespace {

// 印刷横线的亮度介于纸面（亮）与背景（暗，如桌面）之间 —— 三分类后
// 「中间亮度像素占比」在横线行最高、纸面与背景行都接近 0，
// 天然免疫光照不均和深色背景干扰（"找最暗行"的旧思路会被桌面带偏）。
struct LumClass {
    int paperLum;      // 纸面亮度（直方图最高峰）
    int midLo, midHi;  // 中间亮度区间：横线所在
};

LumClass classifyLum(const QImage& gray) {
    int hist[256] = {0};
    for (int y = 0; y < gray.height(); ++y) {
        const uchar* p = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); ++x) ++hist[p[x]];
    }
    int peak = 128;
    long long best = -1;
    for (int i = 0; i < 256; ++i) {
        if (hist[i] > best) { best = hist[i]; peak = i; }
    }
    LumClass c;
    c.paperLum = peak;
    c.midLo = std::max(1, static_cast<int>(peak * 0.45));
    c.midHi = std::max(c.midLo + 8, static_cast<int>(peak * 0.85));
    return c;
}

// 某行「中间亮度」像素占比
double rowMidRatio(const QImage& gray, const LumClass& lc, int y) {
    const uchar* p = gray.constScanLine(y);
    const int W = gray.width();
    int cnt = 0;
    for (int x = 0; x < W; ++x) {
        const int v = p[x];
        if (v >= lc.midLo && v < lc.midHi) ++cnt;
    }
    return static_cast<double>(cnt) / W;
}

// 某列块在第 y 行的中间亮度像素占比
double blockMidRatio(const QImage& gray, const LumClass& lc, int x0, int x1, int y) {
    const uchar* p = gray.constScanLine(y);
    int cnt = 0;
    for (int x = x0; x < x1; ++x) {
        const int v = p[x];
        if (v >= lc.midLo && v < lc.midHi) ++cnt;
    }
    const int n = x1 - x0;
    return (n > 0) ? static_cast<double>(cnt) / n : 0.0;
}

// 平滑（端点保持不变）。用加权核 [1,2,1]/4 而不是等权平均 ——
// 印刷横线常常只有 1px 粗，等权平均一次就把峰值从 1.0 压到 0.33，
// 直接掉到检测阈值以下（这个坑就是这么踩到的）。
void smoothInPlace(std::vector<double>& v, int passes) {
    if (v.size() < 3) return;
    for (int pass = 0; pass < passes; ++pass) {
        std::vector<double> next = v;
        for (size_t i = 1; i + 1 < v.size(); ++i) {
            next[i] = (v[i-1] + 2.0 * v[i] + v[i+1]) * 0.25;
        }
        v = std::move(next);
    }
}

// 局部极大值检测（合并间距过近的峰，保留更高的那个）
std::vector<int> findPeaks(const std::vector<double>& sig, double thresh) {
    std::vector<int> peaks;
    if (sig.size() < 3) return peaks;
    for (size_t i = 1; i + 1 < sig.size(); ++i) {
        if (sig[i] < thresh) continue;
        if (sig[i] < sig[i-1] || sig[i] < sig[i+1]) continue;
        if (!peaks.empty() && static_cast<int>(i) - peaks.back() < 4) {
            if (sig[i] > sig[static_cast<size_t>(peaks.back())]) peaks.back() = static_cast<int>(i);
            continue;
        }
        peaks.push_back(static_cast<int>(i));
    }
    return peaks;
}

double medianOf(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return (n % 2) ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) * 0.5;
}

} // namespace

LineDetectionResult HandwriteGenerator::detectHorizontalLines(const QImage& image,
                                                              int minLines, int maxLines) {
    LineDetectionResult res;

    if (image.isNull()) {
        res.message = QObject::tr("背景图片为空，无法检测横线");
        return res;
    }
    const int W = image.width(), H = image.height();
    if (W < 32 || H < 32) {
        res.message = QObject::tr("背景图片太小，无法检测横线");
        return res;
    }

    const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
    const LumClass lc = classifyLum(gray);

    // ---- 1. 行信号 = 中间亮度像素占比 ----
    // 横线行高（横线是中间亮度），纸面行与背景行都接近 0，
    // 所以天然免疫光照不均与深色背景，不需要再去趋势。
    std::vector<double> sig(static_cast<size_t>(H), 0.0);
    double maxSig = 0.0;
    for (int y = 0; y < H; ++y) {
        sig[static_cast<size_t>(y)] = rowMidRatio(gray, lc, y);
        maxSig = std::max(maxSig, sig[static_cast<size_t>(y)]);
    }
    if (maxSig < 0.05) {
        res.message = QObject::tr("没找到介于纸面与背景之间的横线颜色（可能需要手动描线）");
        return res;
    }
    for (double& s : sig) s /= maxSig;      // 归一化到 [0,1]
    smoothInPlace(sig, 1);

    // 阈值 0.45：最宽处的横线归一化为 1.0，纸面最窄处的横线也应有 0.5 以上
    const auto peaks = findPeaks(sig, 0.45);
#ifdef HANDWRITE_DETECT_DEBUG
    {
        QString dbg = QStringLiteral("peaks(%1): ").arg(peaks.size());
        for (int pk : peaks) dbg += QString::number(pk) + QLatin1Char(' ');
        std::fprintf(stderr, "[detect] maxSig=%.3f paper=%d mid=[%d,%d)\n%s\n",
                     maxSig, lc.paperLum, lc.midLo, lc.midHi, dbg.toUtf8().constData());
        // 行信号采样：每个峰附近 3 行 + 无峰处几行
        for (int y = 0; y < H; y += H / 24) {
            std::fprintf(stderr, "[detect]   y=%4d mid=%+.3f\n", y, sig[static_cast<size_t>(y)]);
        }
    }
#endif
    if (static_cast<int>(peaks.size()) < minLines) {
        res.message = QObject::tr("没检测到规则的横线（可能需要手动描线："
                                  "横线颜色太接近纸面、强烈阴影或文字过密都会导致检测失败）");
        return res;
    }

    // ---- 2. 等距规则化：间距取中位数，被字迹遮挡而漏检的线会被补回来 ----
    std::vector<double> gaps;
    gaps.reserve(peaks.size());
    for (size_t i = 1; i < peaks.size(); ++i) {
        gaps.push_back(static_cast<double>(peaks[i] - peaks[i-1]));
    }
    const double d = medianOf(gaps);
    if (d < 8.0) {
        res.message = QObject::tr("横线间距过小（%1 px），看起来不是作业本").arg(d, 0, 'f', 1);
        return res;
    }

    const int first = peaks.front();
    const int last  = peaks.back();
    int n = static_cast<int>((last - first) / d + 0.5) + 1;
    n = std::max(2, std::min(n, maxLines));

    // ---- 3. 逐列跟踪 —— 让结果是曲线，能表达纸张弯曲 ----
    // 上一步只有「行」分辨率，这里沿每条线在每个列块内找中间亮度占比最高的 y。
    // 关键：以上一个采样点的 y 为中心搜索，而不是固定的直线位置 ——
    // 搜索窗口只需覆盖相邻列之间的弯曲变化量，整条线弯几十像素也能跟过去。
    const int colStep = std::max(6, W / 48);          // 大约 48 个采样列
    const int search  = std::max(3, static_cast<int>(d / 3.0));

    res.curves.reserve(static_cast<size_t>(n));
    int dropped = 0;
    for (int i = 0; i < n; ++i) {
        int curY = static_cast<int>(first + i * d + 0.5);
        std::vector<QPointF> raw;
        std::vector<char> valid;
        for (int x = 0; x < W; x += colStep) {
            const int x1 = std::min(W, x + colStep);
            const int ya = std::max(0, curY - search);
            const int yb = std::min(H - 1, curY + search);
            int bestY = curY;
            double bestV = 0.0;
            for (int y = ya; y <= yb; ++y) {
                const double v = blockMidRatio(gray, lc, x, x1, y);
                if (v > bestV) { bestV = v; bestY = y; }
            }
            // 窗口内没有任何横线像素（bestV 仍为 0）时**保持 curY 不变**：
            // 该列块可能落在纸外/被遮挡，此时取窗口边界会把曲线拽到
            // 相邻的横线上，之后整条线就整体偏移一个线距（真实踩过）。
            const bool hit = (bestV > 0.0);
            if (hit) curY = bestY;
            raw.push_back(QPointF(x + (x1 - x) / 2.0, curY));
            valid.push_back(hit ? 1 : 0);
        }
        if (raw.size() < 3) { ++dropped; continue; }

        // 裁掉首尾「没找到横线」的列块。那里是纸外或空白，留下来的话
        // 曲线两端会带着等距初值（实测末条横线纸外那段偏了 10px）。
        int lo = 0, hi = static_cast<int>(raw.size()) - 1;
        while (lo <= hi && !valid[static_cast<size_t>(lo)]) ++lo;
        while (hi >= lo && !valid[static_cast<size_t>(hi)]) --hi;
        const int span = hi - lo + 1;
        if (span < 3) { ++dropped; continue; }

        int hits = 0;
        for (int k = lo; k <= hi; ++k) {
            if (valid[static_cast<size_t>(k)]) ++hits;
        }
        // 有效点不足三分之一（大片遮挡/阴影）说明这条线不可信，整条丢弃
        if (hits * 3 < span) { ++dropped; continue; }

        GuideCurve c;
        c.pts.reserve(static_cast<size_t>(span));
        for (int k = lo; k <= hi; ++k) {
            qreal yv = raw[static_cast<size_t>(k)].y();
            if (!valid[static_cast<size_t>(k)]) {
                // 中间被遮挡的列块：用前后最近的有效点线性插值补回来，
                // 这样"断裂"不会在渲染时变成一条平的直线段
                int a = k - 1;
                while (a >= lo && !valid[static_cast<size_t>(a)]) --a;
                int b = k + 1;
                while (b <= hi && !valid[static_cast<size_t>(b)]) ++b;
                if (a >= lo && b <= hi) {
                    const qreal t = static_cast<qreal>(k - a) / (b - a);
                    yv = raw[static_cast<size_t>(a)].y() * (1.0 - t)
                       + raw[static_cast<size_t>(b)].y() * t;
                } else if (a >= lo) {
                    yv = raw[static_cast<size_t>(a)].y();
                } else if (b <= hi) {
                    yv = raw[static_cast<size_t>(b)].y();
                }
            }
            c.pts.push_back(QPointF(raw[static_cast<size_t>(k)].x(), yv));
        }
        c.normalize(1);
        if (c.usable()) res.curves.push_back(std::move(c));
        else ++dropped;
    }

    if (res.curves.size() < 2) {
        res.message = QObject::tr("检测出的横线不足两条，请手动描线");
        return res;
    }

    // ---- 4. 挑关键曲线：首尾 2 条；中间鼓/凹时再补中间 1 条 ----
    // 弯曲是连续的，线性插值通常够用；但纸中间明显鼓起时插值会偏，
    // 这时把中间那条也作为关键曲线，插值就变成分段、误差大幅下降。
    res.keyCurves.push_back(res.curves.front());
    const int nCurves = static_cast<int>(res.curves.size());
    if (nCurves >= 3) {
        const int midIdx = (nCurves - 1) / 2;
        // 插值权重必须按 midIdx 在整组里的比例取，不能固定 0.5 ——
        // 条数为偶数时「正中间」没有线，固定 0.5 会天然差出半个线距，
        // 让直横线也被误判成「弯曲非线性」。
        const qreal t = (nCurves > 1) ? static_cast<qreal>(midIdx) / (nCurves - 1) : 0.5;
        const GuideCurve& a = res.curves.front();
        const GuideCurve& b = res.curves.back();
        const GuideCurve& mid = res.curves[static_cast<size_t>(midIdx)];
        // 只在三条曲线共有的 x 区间上比较 —— 裁掉纸外垃圾段之后，
        // 各条曲线的 x 范围可能不同，超出部分 yAt() 会夹取端点，比出来不准
        const qreal xLo = std::max(a.pts.front().x(), std::max(mid.pts.front().x(), b.pts.front().x()));
        const qreal xHi = std::min(a.pts.back().x(),  std::min(mid.pts.back().x(),  b.pts.back().x()));
        double diffSum = 0.0;
        int cnt = 0;
        const int probes = 32;
        for (int k = 0; k < probes && xHi > xLo; ++k) {
            const qreal x = xLo + (xHi - xLo) * k / (probes - 1.0);
            const qreal interp = a.yAt(x) * (1.0 - t) + b.yAt(x) * t;
            diffSum += std::abs(interp - mid.yAt(x));
            ++cnt;
        }
        const double avgDiff = (cnt > 0) ? diffSum / cnt : 0.0;
        res.midlineDeviation = (d > 1e-6) ? avgDiff / d : 0.0;
        // 偏差超过线距的 18% 才认为「弯曲不是线性的」，需要补一条中间关键曲线
        if (res.midlineDeviation > 0.18) res.keyCurves.push_back(mid);
    }
    res.keyCurves.push_back(res.curves.back());

    res.ok = true;
    res.suggestedCount = static_cast<int>(res.curves.size());
    res.spacing = d;
    res.message = QObject::tr("检测到 %1 条横线（间距约 %2 px，关键曲线 %3 条）")
                  .arg(res.suggestedCount).arg(d, 0, 'f', 1).arg(res.keyCurves.size());
    if (dropped > 0) {
        res.message += QObject::tr("，另有 %1 条因遮挡/出界被丢弃").arg(dropped);
    }
    return res;
}

//=============================================================================
// 分页布局
//=============================================================================

std::vector<PageRenderData> HandwriteGenerator::layoutPages(const std::string& text, QFont& font,
                                                              int scaledWidth, int scaledHeight,
                                                              int contentWidth) {
    std::vector<PageRenderData> pageDataList;
    
    // ---- 横排/竖排共用的正常布局 ----
    
    const int scaledTopMargin = m_params.topMargin * m_params.rate;
    const int scaledBottomMargin = m_params.bottomMargin * m_params.rate;
    const int lineHeight = m_params.lineSpacing * m_params.rate;
    const int paraSpacing = m_params.paragraphSpacing * m_params.rate;
    
    const QString qText = QString::fromStdString(text);
    const std::vector<LineLayout> textLines = layoutText(qText, font, contentWidth, lineHeight,
                                                         m_params.wordSpacing * m_params.rate);
    
    const QFontMetrics fm(font);
    
    // ---- 背景图整篇只解码 + 缩放一次 ----
    // 旧实现在 renderPageStatic 内解码，导致每一页都重新读盘并解码整张图，
    // 并行渲染时还会多线程同时读取同一个文件。
    QImage backgroundImage;
    QSize backgroundSourceSize;
    if (!m_params.backgroundImagePath.empty()) {
        const QImage raw = loadImageWithWebpFallback(m_params.backgroundImagePath);
        if (!raw.isNull() && raw.width() > 0 && raw.height() > 0) {
            backgroundSourceSize = raw.size();
            backgroundImage = raw.scaled(scaledWidth, scaledHeight,
                                         Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
    }
    const bool bgUsable = !backgroundImage.isNull();

    // ---- 横线导引：原图坐标 -> 画布坐标（只算一次，所有页共用） ----
    std::vector<GuideCurve> guideCurves;
    const bool useGuides = m_params.lineGuides.isValid() && bgUsable
                           && backgroundSourceSize.width() > 0
                           && backgroundSourceSize.height() > 0;
    if (useGuides) {
        const qreal gx = static_cast<qreal>(scaledWidth) / backgroundSourceSize.width();
        const qreal gy = static_cast<qreal>(scaledHeight) / backgroundSourceSize.height();
        for (GuideCurve& c : m_params.lineGuides.build()) {
            GuideCurve scaled;
            scaled.pts.reserve(c.pts.size());
            for (const QPointF& p : c.pts) {
                scaled.pts.push_back(QPointF(p.x() * gx, p.y() * gy));
            }
            guideCurves.push_back(std::move(scaled));
        }
    }
    const int guideCount = static_cast<int>(guideCurves.size());
    const int stepPerRow = std::max(1, m_params.lineGuides.linesPerRow);

    // ---- 水平透视补偿：锚点四角给出页面几何，据此算每一行的水平缩放 ----
    // 斜拍时纸是梯形 —— 同样的字数在窄的一侧应该占更窄的宽度。
    // 这里只算「文字块左边界→右边界」在照片上的实际宽度与页面标称宽度之比；
    // 正拍且锚点=画布四角时结果恰好是 1.0，不会改变任何既有行为。
    const qreal contentLeftPx = m_params.leftMargin * m_params.rate;
    const bool havePerspective = useGuides && m_params.bgCalibration.isValid()
                                 && backgroundSourceSize.width() > 0
                                 && backgroundSourceSize.height() > 0;
    QPointF perspTL, perspTR, perspBL, perspBR;
    if (havePerspective) {
        const auto& cal = m_params.bgCalibration;
        const qreal px = static_cast<qreal>(scaledWidth) / backgroundSourceSize.width();
        const qreal py = static_cast<qreal>(scaledHeight) / backgroundSourceSize.height();
        auto gp = [&](int r, int c) {
            const QPointF& p = cal.at(r, c);
            return QPointF(p.x() * px, p.y() * py);
        };
        perspTL = gp(0, 0);
        perspTR = gp(0, cal.cols - 1);
        perspBL = gp(cal.rows - 1, 0);
        perspBR = gp(cal.rows - 1, cal.cols - 1);
    }
    auto perspXAt = [&](qreal u, qreal v) {
        return (1 - v) * ((1 - u) * perspTL.x() + u * perspTR.x())
             + v       * ((1 - u) * perspBL.x() + u * perspBR.x());
    };
    // 归一化后的文字块左右边界（页面上 leftMargin / rightMargin 对应的位置）
    const qreal uTextL = (scaledWidth > 0)
        ? qBound(0.0, contentLeftPx / scaledWidth, 1.0) : 0.0;
    const qreal uTextR = (scaledWidth > 0)
        ? qBound(0.0, (scaledWidth - contentLeftPx) / scaledWidth, 1.0) : 1.0;
    const qreal refTextWidth = static_cast<qreal>(scaledWidth) - 2.0 * contentLeftPx;

    // 该行文字块在照片上的左边界（画布坐标）。无锚点时退回页面标称位置。
    auto horizontalOriginAt = [&](qreal yCanvas) -> qreal {
        if (!havePerspective || scaledWidth < 2 || scaledHeight < 2) return contentLeftPx;
        const qreal v = qBound(0.0, yCanvas / scaledHeight, 1.0);
        return perspXAt(uTextL, v);
    };
    // 该行的水平缩放 = 照片上文字块实际宽度 / 页面标称宽度
    auto horizontalScaleAt = [&](qreal yCanvas) -> qreal {
        if (!havePerspective || scaledWidth < 2 || scaledHeight < 2) return 1.0;
        const qreal v = qBound(0.0, yCanvas / scaledHeight, 1.0);
        const qreal xl = perspXAt(uTextL, v);
        const qreal xr = perspXAt(uTextR, v);
        if (refTextWidth < 1.0 || xr <= xl) return 1.0;
        // 限幅：锚点标歪时不至于把字拉成离谱宽度
        return qBound(0.35, (xr - xl) / refTextWidth, 2.8);
    };

    // 第 i 行文字的基线：坐在第 guideIdx 条与第 (guideIdx + stepPerRow) 条之间，
    // 位置由 baselineRatio 决定（0 = 贴上线，1 = 贴下线）
    auto guideBaseline = [&](int guideIdx, qreal xAt) -> qreal {
        const int hi = guideIdx + stepPerRow;
        const qreal upper = guideCurves[static_cast<size_t>(guideIdx)].yAt(xAt);
        qreal lower;
        if (hi < guideCount) {
            lower = guideCurves[static_cast<size_t>(hi)].yAt(xAt);
        } else if (guideIdx > 0) {
            // 最后一条：用前一条的间距外推，保证最后一行也有合理的行高
            const qreal prev = guideCurves[static_cast<size_t>(guideIdx) - 1].yAt(xAt);
            lower = upper + (upper - prev);
        } else {
            lower = upper;
        }
        return upper + (lower - upper) * m_params.lineGuides.baselineRatio
               + m_params.lineGuides.baselineOffset * m_params.rate;
    };

    // ---- 随机种子：0 = 每次随机；非 0 = 固定，预览与导出结果一致 ----
    std::mt19937 seedRng = (m_params.seed != 0)
        ? std::mt19937(static_cast<std::mt19937::result_type>(m_params.seed))
        : std::mt19937(std::random_device{}());
    
    std::vector<RenderLine> currentPage;
    std::vector<std::vector<int>> currentPageCharIndexMap;
    std::vector<qreal> currentPageYPositions;
    std::vector<int> currentPageParaIndex;
    std::vector<int> currentPageGuideIdx;   // 每行使用的导引曲线下标（-1 = 不跟随）
    std::vector<qreal> currentPageScaleX;   // 每行的水平缩放（透视补偿）
    std::vector<qreal> currentPageOriginX;  // 每行文字块的左边界（画布坐标）

    qreal y = scaledTopMargin + fm.ascent();
    int paraIndex = 0;
    int guideIdx = 0;                       // 下一条待用的导引曲线
    
    // 纹理网格对齐: 计算网格尺寸
    // 导引启用时强制关闭 —— 两套对齐机制同时开会得到无意义的位置
    const int textureGridSize = (m_params.paperTexture == PaperTexture::Composition) ? 28 : 25;
    const int scaledGrid = textureGridSize * m_params.rate;
    const bool alignToGrid = (m_params.paperTexture != PaperTexture::None) && !useGuides;
    
    if (alignToGrid) {
        // 首行对齐到最近网格线，至少到第一个实用网格线
        qreal snapped = std::round(y / scaledGrid) * scaledGrid;
        y = std::max(snapped, static_cast<qreal>(scaledGrid));
    }
    
    auto flushPage = [&]() {
        PageRenderData pageData;
        pageData.pageIndex = static_cast<int>(pageDataList.size());
        pageData.lines = std::move(currentPage);
        pageData.scaledWidth = scaledWidth;
        pageData.scaledHeight = scaledHeight;
        pageData.params = m_params;
        if (!bgUsable) {
            // 背景图无法解码：清掉路径并禁用校准，避免渲染阶段再次失败
            pageData.params.backgroundImagePath.clear();
            pageData.params.bgCalibration = BackgroundCalibration();
        }
        pageData.backgroundImage = backgroundImage;
        pageData.backgroundSourceSize = backgroundSourceSize;
        pageData.rng.seed(seedRng());
        pageData.charIndexMap = std::move(currentPageCharIndexMap);
        pageData.lineYPositions = std::move(currentPageYPositions);
        pageData.lineParagraphIndex = std::move(currentPageParaIndex);
        pageData.guideCurves = guideCurves;
        pageData.lineGuideIdx = std::move(currentPageGuideIdx);
        pageData.lineScaleX = std::move(currentPageScaleX);
        pageData.lineOriginX = std::move(currentPageOriginX);
        pageDataList.push_back(std::move(pageData));
        
        currentPage.clear();
        currentPageCharIndexMap.clear();
        currentPageYPositions.clear();
        currentPageParaIndex.clear();
        currentPageGuideIdx.clear();
        currentPageScaleX.clear();
        currentPageOriginX.clear();
        // 翻页后重新从第一条横线开始（作业本每页排版相同）
        guideIdx = 0;
    };
    
    for (const auto& line : textLines) {
        // 空行 = 段落分隔
        if (line.text.isEmpty()) {
            paraIndex++;
            continue;
        }
        
        const bool isSeparator = (line.text == QStringLiteral("---"));

        if (useGuides) {
            // 导引模式：行位置完全由横线决定，与 lineSpacing 无关
            if (guideIdx + stepPerRow > guideCount) {
                flushPage();            // 本页横线用尽
            }
            y = guideBaseline(guideIdx, contentLeftPx);

            // 横线可能被画到页面外（用户手抖），兜底翻页而不是画到纸外面
            if (y + fm.descent() > scaledHeight - scaledBottomMargin) {
                flushPage();
                y = guideBaseline(guideIdx, contentLeftPx);
            }
        } else if (!currentPage.empty()) {
            // 段间距：当前行是段首且上一行不属同一段
            const bool isNewPara = (currentPageParaIndex.empty() ||
                                    currentPageParaIndex.back() != paraIndex);
            int extraSpacing = 0;
            if (isNewPara && !currentPageParaIndex.empty()) {
                extraSpacing = paraSpacing;
            }
            
            const int perturbedLineSpacing = lineHeight + 
                gaussianRandomInt(m_params.lineSpacingSigma * m_params.rate);
            qreal nextY = y + perturbedLineSpacing + extraSpacing;
            
            // 对齐到纹理网格线
            if (alignToGrid) {
                nextY = std::round(nextY / scaledGrid) * scaledGrid;
                if (nextY < scaledGrid) nextY = scaledGrid;
            }
            
            // 标题等大字号行需要额外高度，否则会压出下边距
            qreal needed = fm.descent();
            if (line.fontSizeOverride > 0) {
                needed += (line.fontSizeOverride - m_params.fontSize) * m_params.rate;
            }
            
            if (nextY + needed > scaledHeight - scaledBottomMargin) {
                flushPage();
                y = scaledTopMargin + fm.ascent();
                if (alignToGrid) {
                    qreal snapped = std::round(y / scaledGrid) * scaledGrid;
                    y = std::max(snapped, static_cast<qreal>(scaledGrid));
                }
            } else {
                y = nextY;
            }
        }
        
        currentPageYPositions.push_back(y);
        currentPageParaIndex.push_back(paraIndex);
        currentPageGuideIdx.push_back(useGuides ? guideIdx : -1);
        currentPageScaleX.push_back(useGuides ? horizontalScaleAt(y) : 1.0);
        currentPageOriginX.push_back(useGuides ? horizontalOriginAt(y) : contentLeftPx);
        if (useGuides) guideIdx += stepPerRow;

        RenderLine renderLine;
        renderLine.text = line.text;
        renderLine.font = font;
        renderLine.separator = isSeparator;
        if (line.fontSizeOverride > 0) {
            renderLine.font.setPixelSize(line.fontSizeOverride * m_params.rate);
        }
        
        currentPage.push_back(std::move(renderLine));
        currentPageCharIndexMap.push_back(line.origIndices);
    }
    
    if (!currentPage.empty()) flushPage();
    
    if (pageDataList.empty()) {
        PageRenderData pageData;
        pageData.pageIndex = 0;
        pageData.scaledWidth = scaledWidth;
        pageData.scaledHeight = scaledHeight;
        pageData.params = m_params;
        if (!bgUsable) {
            pageData.params.backgroundImagePath.clear();
            pageData.params.bgCalibration = BackgroundCalibration();
        }
        pageData.backgroundImage = backgroundImage;
        pageData.backgroundSourceSize = backgroundSourceSize;
        pageData.guideCurves = guideCurves;
        pageData.rng.seed(seedRng());
        pageDataList.push_back(std::move(pageData));
    }
    
    return pageDataList;
}

//=============================================================================
// 带扰动的文本绘制（增强版：笔画粗细、混合字体、涂改模拟）
//=============================================================================

void HandwriteGenerator::drawTextWithPerturbationStatic(QPainter& painter, const QString& text,
                                                         qreal& x, qreal& y, const QFont& baseFont,
                                                         int scaledWordSpacing, const TemplateParams& params,
                                                         std::mt19937& rng,
                                                         int startCharIndex,
                                                         const std::vector<int>* charIndexMap,
                                                         TextWarp warp,
                                                         qreal warpPhaseBase,
                                                         qreal warpAmplitude,
                                                         qreal warpWavelength,
                                           const GuideCurve* guideCurve,
                                           bool followCurve) {
    Q_UNUSED(startCharIndex);
    QFontMetrics fm(baseFont);
    std::uniform_real_distribution<double> strikeDist(0.0, 1.0);
    // 逐字变形的相位累加器（旧实现用行首常量算相位，等于整行平移）
    qreal warpPhase = warpPhaseBase;
    const bool warpActive = (warp != TextWarp::None) && (warpWavelength > 1.0)
                            && (std::abs(warpAmplitude) > 0.0);

    // 横线导引：以行首 x 处的曲线高度为基准，行内的相对偏移逐字叠加。
    // 曲线本身已包含透视与纸张弯曲（用户在照片上直接描出来的），
    // 所以这里不再做任何逆映射 —— 见 docs/PLAN_LINE_GUIDES_2026-09-13.md
    const bool guideActive = (guideCurve != nullptr) && guideCurve->usable();
    const qreal guideY0 = guideActive ? guideCurve->yAt(x) : 0.0;
    
    for (int i = 0; i < text.length(); ++i) {
        QChar c = text[i];
        
        if (!fm.inFont(c)) {
            const qreal skipAdvance = fm.height() * 0.6 + scaledWordSpacing +
                 gaussianRandomStatic(params.wordSpacingSigma * params.rate, rng);
            x += skipAdvance;
            if (warpActive) warpPhase += skipAdvance / warpWavelength * 2.0 * M_PI;
            continue;
        }
        
        CharacterOverride override;
        if (charIndexMap && i < static_cast<int>(charIndexMap->size())) {
            override = findCharOverride((*charIndexMap)[i], params.charOverrides);
        }
        
        // 涂改模拟：随机跳过字符（划线删除）
        bool strikethrough = false;
        if (params.strikeThroughRate > 0 && strikeDist(rng) < params.strikeThroughRate) {
            strikethrough = true;
        }
        
        qreal perturbX = override.perturbX.has_value()
            ? *override.perturbX * params.rate
            : gaussianRandomStatic(params.perturbXSigma * params.rate, rng);
        qreal perturbY = override.perturbY.has_value()
            ? *override.perturbY * params.rate
            : gaussianRandomStatic(params.perturbYSigma * params.rate, rng);
        qreal perturbTheta = override.perturbTheta.has_value()
            ? *override.perturbTheta
            : gaussianRandomStatic(params.perturbThetaSigma, rng);
        
        // 逐字变形偏移：按当前字符在行内的累计位置计算
        qreal warpOffset = 0.0;
        if (warpActive) {
            switch (warp) {
                case TextWarp::Arc:
                    // 半个正弦拱：行首行尾低、中间高
                    warpOffset = -warpAmplitude * std::sin(warpPhase * 0.5);
                    break;
                case TextWarp::Wave:
                case TextWarp::Circle:
                    warpOffset = warpAmplitude * std::sin(warpPhase);
                    break;
                default: break;
            }
        }
        
        // 字体选择（支持混合字体）
        QFont activeFont = baseFont;
        if (!params.fontMixList.empty() && !override.fontSize.has_value()) {
            activeFont = pickMixedFont(baseFont, params.fontMixList, params.fontMixRate, rng);
        }
        
        QFont perturbedFont = activeFont;
        if (override.fontSize.has_value()) {
            perturbedFont.setPixelSize(*override.fontSize * params.rate);
        } else if (params.fontSizeSigma > 0) {
            int basePixelSize = activeFont.pixelSize();
            if (basePixelSize > 0) {
                int fontSizeDelta = gaussianRandomIntStatic(params.fontSizeSigma * params.rate, rng);
                int newPixelSize = qMax(1, basePixelSize + fontSizeDelta);
                perturbedFont.setPixelSize(newPixelSize);
            }
        }
        
        QColor fillColor = override.fillColor.has_value()
            ? QColor(override.fillColor->r, override.fillColor->g, override.fillColor->b, override.fillColor->a)
            : QColor(params.fillColor.r, params.fillColor.g, params.fillColor.b, params.fillColor.a);
        
        painter.save();
        painter.setFont(perturbedFont);
        
        // 笔画粗细扰动
        QPen pen(fillColor);
        double strokeVar = gaussianRandomStatic(params.strokeWidthSigma, rng);
        double penWidth = qMax(0.3, 1.0 + strokeVar);
        pen.setWidthF(penWidth);
        painter.setPen(pen);
        
        QFontMetrics perturbedFm(perturbedFont);
        
        // 横线导引：按当前 x 采样曲线，得到该字符的纵向偏移与切线角
        qreal curveY = 0.0, curveAngle = 0.0;
        if (guideActive) guideCurve->sample(x, &curveY, &curveAngle);
        const qreal guideOffset = guideActive ? (curveY - guideY0) : 0.0;
        // 切线角与逐字扰动角叠加（不覆盖），弯曲与手抖同时生效
        const qreal theta = perturbTheta + (guideActive && followCurve ? curveAngle : 0.0);

        // 涂改效果（drawY 已含逐字变形偏移与导引偏移）
        const qreal drawY = y + guideOffset + perturbY + warpOffset;
        if (strikethrough) {
            const bool rotated = std::abs(theta) > 0.001;
            if (rotated) {
                painter.translate(x + perturbX, drawY);
                painter.rotate(theta * 180.0 / M_PI);
                painter.drawText(0, 0, QString(c));
            } else {
                painter.drawText(x + perturbX, drawY, QString(c));
            }
            // 绘制横线删除标记（使用已变换坐标系）
            const int charW = perturbedFm.horizontalAdvance(c);
            QPen strikePen(QColor(80, 80, 80, 180));
            strikePen.setWidthF(1.5);
            painter.setPen(strikePen);
            if (rotated) {
                painter.drawLine(-2, -perturbedFm.ascent() / 2,
                                 charW + 2, -perturbedFm.ascent() / 2);
            } else {
                painter.drawLine(static_cast<int>(x + perturbX - 2),
                                 static_cast<int>(drawY - perturbedFm.ascent() / 2),
                                 static_cast<int>(x + perturbX + charW + 2),
                                 static_cast<int>(drawY - perturbedFm.ascent() / 2));
            }
        } else {
            if (std::abs(theta) > 0.001) {
                painter.translate(x + perturbX, drawY);
                painter.rotate(theta * 180.0 / M_PI);
                painter.drawText(0, 0, QString(c));
            } else {
                painter.drawText(x + perturbX, drawY, QString(c));
            }
        }
        
        painter.restore();
        
        const qreal advance = perturbedFm.horizontalAdvance(c) + scaledWordSpacing +
             gaussianRandomStatic(params.wordSpacingSigma * params.rate, rng);
        x += advance;
        if (warpActive) warpPhase += advance / warpWavelength * 2.0 * M_PI;
    }
}

//=============================================================================
// 页面渲染（增强版：纸张纹理、背景图片、墨水洇染）
//=============================================================================

QImage HandwriteGenerator::renderPageStatic(const PageRenderData& data) {
    QImage image(data.scaledWidth, data.scaledHeight, QImage::Format_ARGB32);
    image.fill(QColor(data.params.backgroundColor.r, data.params.backgroundColor.g,
                      data.params.backgroundColor.b, data.params.backgroundColor.a));
    
    // 背景图已在 layoutPages 里解码 + 缩放好，这里只负责绘制
    // （QImage 隐式共享，赋值零拷贝；旧实现此处每页重新读盘解码整张图）
    const QImage& bgImage = data.backgroundImage;
    const bool anchorsValid = data.params.bgCalibration.isValid()
                              && !bgImage.isNull()
                              && !data.params.backgroundImagePath.empty();

    // 横线导引优先于锚点 warp：导引曲线是用户在照片上直接描出来的，
    // 本身就包含透视 + 弯曲的全部形变。再走一遍 warp 等于绕一圈回来，
    // 只会被 3×3 网格的分片近似拖出误差（见规划文档 3.3）。
    const bool guidesValid = !data.guideCurves.empty();
    const bool useWarp = anchorsValid && !guidesValid;

    // 决定在哪个画布上绘制文字
    QImage* textCanvas = &image;  // 默认直接画在主图上
    QImage textOverlay;           // warp 模式下单独的文字画布

    if (useWarp) {
        textOverlay = QImage(data.scaledWidth, data.scaledHeight, QImage::Format_ARGB32);
        textOverlay.fill(Qt::transparent);
        textCanvas = &textOverlay;
    }
    
    QPainter painter(textCanvas);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    
    if (!bgImage.isNull()) {
        if (useWarp) {
            // warp 模式：背景画到主图（文字另画在 overlay，最后再贴上去）
            QPainter bgPainter(&image);
            bgPainter.drawImage(image.rect(), bgImage);
            bgPainter.end();
        } else {
            painter.drawImage(image.rect(), bgImage);
        }
    }
    
    // 纸张纹理：有背景照片时不画（纹理线会盖在照片上，也跟照片里的印刷横线对不上）
    if (!useWarp && !guidesValid) {
        drawPaperTexture(painter, data.scaledWidth, data.scaledHeight,
                         data.params.paperTexture, data.params.rate,
                         data.params.textureOpacity);
    }
    
    // 字体颜色
    QColor fillColor(data.params.fillColor.r, data.params.fillColor.g,
                     data.params.fillColor.b, data.params.fillColor.a);
    painter.setPen(fillColor);
    
    qreal contentLeft = data.params.leftMargin * data.params.rate;
    qreal contentBottom = data.scaledHeight - data.params.bottomMargin * data.params.rate;
    qreal contentTop = data.params.topMargin * data.params.rate;
    
    std::mt19937 localRng = data.rng;
    bool usePrecomputedY = (data.lineYPositions.size() == data.lines.size());
    
    const qreal lineBoxWidth = std::max<qreal>(1.0, data.scaledWidth - 2.0 * contentLeft);
    const qreal rate = static_cast<qreal>(data.params.rate);
    const qreal warpStrength = qMax(0.0, data.params.textWarpStrength);
    
    for (size_t lineIdx = 0; lineIdx < data.lines.size(); ++lineIdx) {
        const RenderLine& rl = data.lines[lineIdx];
        const QString& line = rl.text;
        const QFont& font = rl.font;
        QFontMetrics fm(font);
        
        // 横排
        qreal y;
        if (usePrecomputedY) {
            y = data.lineYPositions[lineIdx];
        } else {
            y = contentTop + fm.ascent();
            for (size_t j = 0; j < lineIdx; ++j) {
                y += data.params.lineSpacing * data.params.rate;
            }
        }
        
        if (y + fm.descent() > contentBottom) break;
        
        // Markdown 分割线：画水平线而非文字
        if (rl.separator) {
            QPen sepPen(QColor(data.params.fillColor.r, data.params.fillColor.g,
                               data.params.fillColor.b, data.params.fillColor.a));
            sepPen.setWidthF(qMax(1.0, 1.5 * rate));
            painter.setPen(sepPen);
            const qreal ly = y - fm.ascent() / 2.0;
            painter.drawLine(QPointF(contentLeft, ly),
                             QPointF(data.scaledWidth - contentLeft, ly));
            continue;
        }
        
        // 水平透视补偿：斜拍时纸是梯形。该行的文字块既要**平移到纸上的实际位置**
        // （左边界随透视左右移动），又要**按实际宽度缩放**（字宽与字距一起变）。
        // 变换：x -> originX + (x - contentLeft) * lineScale
        // 于是逻辑坐标 contentLeft 正好落在 originX 上。
        const qreal lineScale = (guidesValid && lineIdx < data.lineScaleX.size())
                              ? data.lineScaleX[lineIdx] : 1.0;
        const qreal lineOrigin = (guidesValid && lineIdx < data.lineOriginX.size())
                              ? data.lineOriginX[lineIdx] : contentLeft;
        const bool needScale = std::abs(lineScale - 1.0) > 1e-4
                            || std::abs(lineOrigin - contentLeft) > 0.5;
        if (needScale) {
            painter.save();
            painter.translate(lineOrigin - lineScale * contentLeft, 0.0);
            painter.scale(lineScale, 1.0);
        }

        qreal x = contentLeft;
        painter.setFont(font);
        
        // 文字变形：振幅/波长交给逐字绘制，由它按字符累计位置推进相位。
        // 旧实现用行首常量 x 算相位，sin() 结果恒定 → 只是整行上下平移，
        // 行内没有任何弯曲。
        TextWarp warp = data.params.textWarp;
        qreal warpAmplitude = 0.0, warpWavelength = 0.0;
        switch (warp) {
            case TextWarp::Arc:
                warpAmplitude  = data.scaledHeight * 0.10 * warpStrength;
                warpWavelength = lineBoxWidth * 2.0;   // 行内相位 0..π → 半个正弦拱
                break;
            case TextWarp::Wave:
                warpAmplitude  = data.params.lineSpacing * rate * 0.6 * warpStrength;
                warpWavelength = lineBoxWidth / 2.0;   // 约两个完整波
                break;
            case TextWarp::Circle:
                warpAmplitude  = data.scaledHeight * 0.06 * warpStrength;
                warpWavelength = lineBoxWidth;         // 一个完整波
                break;
            default: break;
        }
        const qreal warpPhaseBase = static_cast<qreal>(lineIdx) * 0.5;
        
        const std::vector<int>* lineCharIndexMap = nullptr;
        if (lineIdx < data.charIndexMap.size()) {
            lineCharIndexMap = &data.charIndexMap[lineIdx];
        }
        
        // 该行对应的导引曲线（画布空间）；-1 表示不跟随
        const GuideCurve* guideCurve = nullptr;
        if (lineIdx < data.lineGuideIdx.size()) {
            const int gi = data.lineGuideIdx[lineIdx];
            if (gi >= 0 && gi < static_cast<int>(data.guideCurves.size())) {
                guideCurve = &data.guideCurves[static_cast<size_t>(gi)];
            }
        }
        
        drawTextWithPerturbationStatic(painter, line, x, y, font,
                                       data.params.wordSpacing * data.params.rate,
                                       data.params, localRng, 0, lineCharIndexMap,
                                       warp, warpPhaseBase, warpAmplitude, warpWavelength,
                                       guideCurve, data.params.lineGuides.followCurve);

        if (needScale) painter.restore();
    }
    
    painter.end();
    
    // 网格形变：将文字画布映射到背景图片的校准区域
    if (useWarp) {
        auto& cal = data.params.bgCalibration;
        
        // 缩放锚点到渲染画布坐标（锚点存于背景「原图」坐标空间）
        int bw = data.backgroundSourceSize.width();  if (bw < 1) bw = 1;
        int bh = data.backgroundSourceSize.height(); if (bh < 1) bh = 1;
        qreal sx = static_cast<qreal>(data.scaledWidth) / bw;
        qreal sy = static_cast<qreal>(data.scaledHeight) / bh;
        
        int n = cal.rows * cal.cols;
        std::vector<QPointF> dstGrid(n);
        for (int i = 0; i < n; ++i) {
            dstGrid[i] = QPointF(cal.gridPoints[i].x() * sx, cal.gridPoints[i].y() * sy);
        }
        
        // 源网格：均匀分布
        std::vector<QPointF> srcGrid(n);
        for (int r = 0; r < cal.rows; ++r) {
            qreal y = data.scaledHeight * r / (cal.rows - 1.0);
            for (int c = 0; c < cal.cols; ++c) {
                qreal x = data.scaledWidth * c / (cal.cols - 1.0);
                srcGrid[r * cal.cols + c] = QPointF(x, y);
            }
        }
        
        QPainter finalPainter(&image);
        finalPainter.setRenderHint(QPainter::SmoothPixmapTransform);
        warpMesh(finalPainter, textOverlay, srcGrid, dstGrid, cal.rows, cal.cols);
        finalPainter.end();
    }
    
    // 墨水洇染（后处理）
    if (data.params.inkBleed && data.params.inkBleedRadius > 0) {
        applyInkBleed(image, data.params.inkBleedRadius * data.params.rate);
    }
    
    return image;
}

//=============================================================================
// 生成方法
//=============================================================================

static QFont loadFont(const std::string& fontPath, int pixelSize) {
    QFont font;
    if (!fontPath.empty()) {
        int fontId = QFontDatabase::addApplicationFont(QString::fromStdString(fontPath));
        if (fontId != -1) {
            QStringList families = QFontDatabase::applicationFontFamilies(fontId);
            if (!families.isEmpty()) font = QFont(families[0]);
        }
    }
    font.setPixelSize(pixelSize);
    return font;
}

//=============================================================================
// 字体可用性检查
//=============================================================================

bool HandwriteGenerator::checkFontAvailable(const std::string& fontPath, std::string* message) {
    if (fontPath.empty()) {
        if (message) *message = "未选择字体文件";
        return false;
    }
    if (!QFile::exists(QString::fromStdString(fontPath))) {
        if (message) *message = "字体文件不存在: " + fontPath;
        return false;
    }
    int fontId = QFontDatabase::addApplicationFont(QString::fromStdString(fontPath));
    if (fontId == -1) {
        if (message) *message = "字体无法加载（文件损坏或非 TTF/OTF）: " + fontPath;
        return false;
    }
    QStringList families = QFontDatabase::applicationFontFamilies(fontId);
    if (families.isEmpty()) {
        if (message) *message = "字体加载后没有可用字族: " + fontPath;
        return false;
    }
    return true;
}

//=============================================================================
// 内存预算
//=============================================================================

long long HandwriteGenerator::estimateSinglePageBytes(const TemplateParams& params) {
    long long w = static_cast<long long>(params.paperWidth) * params.rate;
    long long h = static_cast<long long>(params.paperHeight) * params.rate;
    if (params.textDirection == TextDirection::Vertical) std::swap(w, h);
    const long long px = std::max<long long>(0, w * h);
    
    // 4 = 主画布(ARGB32)；校准模式再开一张同尺寸文字图层
    int factor = 4;
    if (params.bgCalibration.isValid()) factor += 4;
    // 背景原图 + 缩放后副本
    if (!params.backgroundImagePath.empty()) factor += 8;
    // 洇染：Alpha8 缓冲 + 可分离滤波中间缓冲
    if (params.inkBleed && params.inkBleedRadius > 0) factor += 2;
    // 竖排旋转：临时图 + 结果图
    if (params.textDirection == TextDirection::Vertical) factor += 8;
    
    return px * factor + (32LL << 20);   // + 32 MB 固定开销
}

int HandwriteGenerator::clampThreadsForBudget(const TemplateParams& params, int requested,
                                              int pageCount, bool holdAllPages) {
    if (requested <= 0) {
        requested = QThread::idealThreadCount();
        if (requested <= 0) requested = 4;
    }
    const int pages = std::max(1, pageCount);
    if (holdAllPages) return 1;   // 需要同时持有所有页面，线程数不改变峰值
    const long long per = std::max<long long>(1, estimateSinglePageBytes(params));
    const int byBudget = static_cast<int>(std::max<long long>(1, MAX_TOTAL_BYTES / per));
    return std::max(1, std::min(std::min(requested, byBudget), pages));
}

bool HandwriteGenerator::checkRenderBudget(const TemplateParams& params, int threadCount,
                                            int pageCount, bool holdAllPages,
                                            std::string* message) {
    const long long per = estimateSinglePageBytes(params);
    const int pages = std::max(1, pageCount);
    
    auto mb = [](long long bytes) {
        return static_cast<double>(bytes) / (1024.0 * 1024.0);
    };
    
    if (per > MAX_SINGLE_PAGE_BYTES) {
        if (message) {
            char buf[512];
            std::snprintf(buf, sizeof(buf),
                "单页需约 %.1f GB 内存，超过 %.1f GB 上限。\n"
                "请降低分辨率倍率（当前 x%d）、缩小纸张尺寸或关闭墨水洇染/背景图。",
                mb(per) / 1024.0, mb(MAX_SINGLE_PAGE_BYTES) / 1024.0, params.rate);
            *message = buf;
        }
        return false;
    }
    
    const long long peak = holdAllPages
        ? per * pages
        : per * std::max(1, clampThreadsForBudget(params, threadCount, pages, false));
    
    if (peak > MAX_TOTAL_BYTES) {
        if (message) {
            char buf[512];
            std::snprintf(buf, sizeof(buf),
                "预计峰值内存约 %.1f GB（%d 页 × 单页 %.0f MB），超过 %.1f GB 上限。\n"
                "请降低分辨率倍率（当前 x%d）、缩短文本，或改用「导出」逐页写盘以降低占用。",
                mb(peak) / 1024.0, pages, mb(per), mb(MAX_TOTAL_BYTES) / 1024.0, params.rate);
            *message = buf;
        }
        return false;
    }
    
    if (message) message->clear();
    return true;
}

//=============================================================================
// 生成方法
//=============================================================================

QImage HandwriteGenerator::rotateToVertical(const QImage& src, int targetWidth, int targetHeight) {
    QTransform rot;
    rot.rotate(90);
    QImage out = src.transformed(rot, Qt::SmoothTransformation);
    const int cw = qMin(out.width(),  targetWidth);
    const int ch = qMin(out.height(), targetHeight);
    const int cx = (out.width()  - cw) / 2;
    const int cy = (out.height() - ch) / 2;
    return out.copy(cx, cy, cw, ch);
}

std::vector<QImage> HandwriteGenerator::generatePreview(const std::string& text) {
    int scaledWidth = m_params.paperWidth * m_params.rate;
    int scaledHeight = m_params.paperHeight * m_params.rate;
    const int pageWidth  = scaledWidth;
    const int pageHeight = scaledHeight;
    
    // 竖排：交换宽高，文本横向填充后再旋转
    if (m_params.textDirection == TextDirection::Vertical) {
        std::swap(scaledWidth, scaledHeight);
    }
    
    int contentWidth = scaledWidth - (m_params.leftMargin + m_params.rightMargin) * m_params.rate;
    
    QFont font = loadFont(m_params.fontPath, m_params.fontSize * m_params.rate);
    auto pageDataList = layoutPages(text, font, scaledWidth, scaledHeight, contentWidth);
    
    // 预算检查放在布局之后，此时页数与真实内存需求都已确定
    std::string budgetMsg;
    if (!checkRenderBudget(m_params, 1, static_cast<int>(pageDataList.size()), true, &budgetMsg)) {
        throw std::runtime_error(budgetMsg);
    }
    
    std::vector<QImage> images;
    images.reserve(pageDataList.size());
    for (const auto& pd : pageDataList) images.push_back(renderPageStatic(pd));
    
    // 竖排：旋转每页后居中裁切
    if (m_params.textDirection == TextDirection::Vertical) {
        for (auto& img : images) {
            img = rotateToVertical(img, pageWidth, pageHeight);
        }
    }
    
    if (images.empty()) {
        QImage empty(scaledWidth, scaledHeight, QImage::Format_ARGB32);
        empty.fill(QColor(m_params.backgroundColor.r, m_params.backgroundColor.g,
                          m_params.backgroundColor.b, m_params.backgroundColor.a));
        images.push_back(empty);
    }
    return images;
}

std::vector<QImage> HandwriteGenerator::renderPagesParallel(const std::vector<PageRenderData>& pages,
                                                            int threadCount,
                                                            const std::function<void(int, int)>& onProgress,
                                                            const std::function<bool()>& isCanceled) {
    const int total = static_cast<int>(pages.size());
    if (total <= 0) return {};
    
    const int threads = std::max(1, std::min(threadCount, total));
    std::vector<QImage> results(total);
    std::atomic<int> done{0};
    std::atomic<bool> canceled{false};
    
    QThreadPool pool;
    pool.setMaxThreadCount(threads);   // 局部线程池，不再改全局池
    
    for (int i = 0; i < total; ++i) {
        pool.start([&, i]() {
            if (canceled.load() || (isCanceled && isCanceled())) {
                canceled.store(true);
                return;
            }
            results[i] = HandwriteGenerator::renderPageStatic(pages[i]);
            const int n = done.fetch_add(1) + 1;
            if (onProgress) onProgress(n, total);
        });
    }
    pool.waitForDone();
    
    if (canceled.load() || (isCanceled && isCanceled())) return {};
    return results;
}

std::vector<QImage> HandwriteGenerator::generatePreviewParallel(const std::string& text, int threadCount) {
    int scaledWidth = m_params.paperWidth * m_params.rate;
    int scaledHeight = m_params.paperHeight * m_params.rate;
    const int pageWidth  = scaledWidth;
    const int pageHeight = scaledHeight;
    
    if (m_params.textDirection == TextDirection::Vertical) {
        std::swap(scaledWidth, scaledHeight);
    }
    
    int contentWidth = scaledWidth - (m_params.leftMargin + m_params.rightMargin) * m_params.rate;
    
    QFont font = loadFont(m_params.fontPath, m_params.fontSize * m_params.rate);
    auto pageDataList = layoutPages(text, font, scaledWidth, scaledHeight, contentWidth);
    
    std::string budgetMsg;
    if (!checkRenderBudget(m_params, threadCount, static_cast<int>(pageDataList.size()), true, &budgetMsg)) {
        throw std::runtime_error(budgetMsg);
    }
    
    const int threads = clampThreadsForBudget(m_params, threadCount,
                                              static_cast<int>(pageDataList.size()), true);
    auto images = renderPagesParallel(pageDataList, threads, nullptr, nullptr);
    
    if (m_params.textDirection == TextDirection::Vertical) {
        for (auto& img : images) img = rotateToVertical(img, pageWidth, pageHeight);
    }
    
    return images;
}

std::map<int, std::string> HandwriteGenerator::renderAndSaveParallel(
        const std::vector<PageRenderData>& pages,
        const std::string& outputDir,
        int threadCount,
        const std::function<void(int, int)>& onProgress,
        const std::function<bool()>& isCanceled) {
    const int total = static_cast<int>(pages.size());
    if (total <= 0) return {};
    
    // 静态成员函数没有 this：纸张参数取自页面数据本身（每页都带一份 params）
    const TemplateParams& pp = pages[0].params;
    const bool vertical = (pp.textDirection == TextDirection::Vertical);
    const int pageWidth  = pp.paperWidth * pp.rate;
    const int pageHeight = pp.paperHeight * pp.rate;
    
    const int threads = std::max(1, std::min(threadCount > 0 ? threadCount : QThread::idealThreadCount(), total));
    std::vector<std::string> pathPerPage(total);
    std::atomic<int> done{0};
    std::atomic<bool> canceled{false};
    
    QThreadPool pool;
    pool.setMaxThreadCount(threads);   // 局部线程池
    
    for (int i = 0; i < total; ++i) {
        pool.start([&, i]() {
            if (canceled.load() || (isCanceled && isCanceled())) {
                canceled.store(true);
                return;
            }
            // 逐页渲染并立即落盘，QImage 出作用域即释放：
            // 峰值内存 = 线程数 × 单页，而不是页数 × 单页
            QImage img = HandwriteGenerator::renderPageStatic(pages[i]);
            if (vertical) img = rotateToVertical(img, pageWidth, pageHeight);
            const QString path = QString::fromStdString(outputDir) + "/" + QString::number(i) + ".png";
            if (img.save(path, "PNG")) pathPerPage[i] = path.toStdString();
            const int n = done.fetch_add(1) + 1;
            if (onProgress) onProgress(n, total);
        });
    }
    pool.waitForDone();
    
    std::map<int, std::string> filePaths;
    if (canceled.load() || (isCanceled && isCanceled())) return filePaths;
    for (int i = 0; i < total; ++i) {
        if (!pathPerPage[i].empty()) filePaths[i] = pathPerPage[i];
    }
    return filePaths;
}

std::map<int, std::string> HandwriteGenerator::generateImageParallel(const std::string& text,
                                                                      const std::string& outputDir,
                                                                      int threadCount,
                                                                      std::function<void(int, int)> progressCallback,
                                                                      std::function<bool()> cancelCallback) {
    std::map<int, std::string> filePaths;
    QDir dir;
    const QString qDir = QString::fromStdString(outputDir);
    dir.mkpath(qDir);
    cleanGeneratedPages(qDir);
    
    if (threadCount <= 0) { threadCount = QThread::idealThreadCount(); if (threadCount <= 0) threadCount = 4; }
    
    const int scaledWidth = m_params.paperWidth * m_params.rate;
    const int scaledHeight = m_params.paperHeight * m_params.rate;
    const int contentWidth = scaledWidth - (m_params.leftMargin + m_params.rightMargin) * m_params.rate;
    
    QFont font = loadFont(m_params.fontPath, m_params.fontSize * m_params.rate);
    auto pageDataList = layoutPages(text, font, scaledWidth, scaledHeight, contentWidth);
    
    const int totalPages = static_cast<int>(pageDataList.size());
    const int totalSteps = totalPages * 2;
    
    // 逐页写盘模式下峰值 = 线程数 × 单页，用 holdAllPages=false 计算
    std::string budgetMsg;
    if (!checkRenderBudget(m_params, threadCount, totalPages, false, &budgetMsg)) {
        throw std::runtime_error(budgetMsg);
    }
    
    if (progressCallback) progressCallback(0, totalSteps);
    
    const int threads = clampThreadsForBudget(m_params, threadCount, totalPages, false);
    filePaths = renderAndSaveParallel(pageDataList, outputDir, threads,
        [&](int n, int) { if (progressCallback) progressCallback(n, totalSteps); },
        cancelCallback);
    
    if (progressCallback) progressCallback(totalSteps, totalSteps);
    return filePaths;
}

//=============================================================================
// PDF 导出
//=============================================================================

bool HandwriteGenerator::exportPdf(const std::string& text, const std::string& pdfPath) {
    std::vector<QImage> images;
    try {
        images = generatePreview(text);   // 内含内存预算检查，超限抛异常
    } catch (const std::exception&) {
        return false;
    }
    if (images.empty()) return false;
    
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(QString::fromStdString(pdfPath));
    printer.setPageSize(QPageSize(QSizeF(m_params.paperWidth * 0.3528, m_params.paperHeight * 0.3528),
                                   QPageSize::Millimeter));
    
    QPainter painter;
    if (!painter.begin(&printer)) return false;
    
    for (size_t i = 0; i < images.size(); ++i) {
        if (i > 0) printer.newPage();
        QRectF pageRect = printer.pageLayout().paintRectPixels(printer.resolution());
        QImage scaled = images[i].scaled(static_cast<int>(pageRect.width()),
                                          static_cast<int>(pageRect.height()),
                                          Qt::KeepAspectRatio, Qt::SmoothTransformation);
        int x = static_cast<int>((pageRect.width() - scaled.width()) / 2);
        int y = static_cast<int>((pageRect.height() - scaled.height()) / 2);
        painter.drawImage(x, y, scaled);
    }
    painter.end();
    return true;
}

//=============================================================================
// SVG 导出
//=============================================================================

bool HandwriteGenerator::exportSvg(const std::string& text, const std::string& svgPath) {
    std::vector<QImage> images;
    try {
        images = generatePreview(text);
    } catch (const std::exception&) {
        return false;
    }
    if (images.empty()) return false;
    
    // 一个 SVG 文件只能装一页：首页写 svgPath，其余写 svgPath-2.svg、-3.svg …
    // （旧实现只导第 1 页，多页文档会静默丢内容）
    const QString base = QString::fromStdString(svgPath);
    const QString stem = base.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive)
                       ? base.left(base.length() - 4) : base;
    
    for (size_t i = 0; i < images.size(); ++i) {
        const QString file = (i == 0)
            ? base
            : QStringLiteral("%1-%2.svg").arg(stem).arg(i + 1);
        
        QSvgGenerator generator;
        generator.setFileName(file);
        generator.setSize(QSize(m_params.paperWidth * m_params.rate,
                                m_params.paperHeight * m_params.rate));
        generator.setViewBox(QRect(0, 0, m_params.paperWidth * m_params.rate,
                                    m_params.paperHeight * m_params.rate));
        generator.setTitle("HandWrite Export");
        
        QPainter painter;
        if (!painter.begin(&generator)) return false;
        painter.drawImage(0, 0, images[i]);
        painter.end();
    }
    return true;
}

//=============================================================================
// 生僻字检测
//=============================================================================

std::vector<QChar> HandwriteGenerator::findUnsupportedCharsStatic(const std::string& text,
                                                                   const std::string& fontPath) {
    std::vector<QChar> unsupportedChars;
    std::set<QChar> seenChars;
    
    QFont font = loadFont(fontPath, 16);
    QFontMetrics fm(font);
    QString qText = QString::fromStdString(text);
    
    for (const QChar& c : qText) {
        if (c.isSpace() || c.category() == QChar::Other_Control) continue;
        if (!fm.inFont(c) && seenChars.find(c) == seenChars.end()) {
            unsupportedChars.push_back(c);
            seenChars.insert(c);
        }
    }
    return unsupportedChars;
}

//=============================================================================
// 网格形变渲染
//=============================================================================

void HandwriteGenerator::warpMesh(QPainter& painter, const QImage& source,
                                   const std::vector<QPointF>& srcGrid,
                                   const std::vector<QPointF>& dstGrid,
                                   int rows, int cols) {
    if (rows < 2 || cols < 2) return;
    
    for (int r = 0; r < rows - 1; ++r) {
        for (int c = 0; c < cols - 1; ++c) {
            QPointF s00 = srcGrid[r * cols + c];
            QPointF s10 = srcGrid[r * cols + c + 1];
            QPointF s11 = srcGrid[(r + 1) * cols + c + 1];
            QPointF s01 = srcGrid[(r + 1) * cols + c];
            
            QPointF d00 = dstGrid[r * cols + c];
            QPointF d10 = dstGrid[r * cols + c + 1];
            QPointF d11 = dstGrid[(r + 1) * cols + c + 1];
            QPointF d01 = dstGrid[(r + 1) * cols + c];
            
            QPolygonF srcQuad; srcQuad << s00 << s10 << s11 << s01;
            QPolygonF dstQuad; dstQuad << d00 << d10 << d11 << d01;
            
            QTransform warp;
            if (!QTransform::quadToQuad(srcQuad, dstQuad, warp)) continue;
            
            // 仅取该 cell 对应的源子矩形，避免每格重绘整图
            qreal sx0 = std::min(s00.x(), s01.x());
            qreal sx1 = std::max(s10.x(), s11.x());
            qreal sy0 = std::min(s00.y(), s10.y());
            qreal sy1 = std::max(s01.y(), s11.y());
            QRectF srcCell(sx0, sy0, sx1 - sx0, sy1 - sy0);
            srcCell = srcCell.intersected(QRectF(source.rect()));
            
            QPainterPath clip; clip.addPolygon(dstQuad);
            painter.save();
            painter.setClipPath(clip);
            painter.setTransform(warp);
            painter.drawImage(srcCell.topLeft(), source, srcCell);
            painter.restore();
        }
    }
}

} // namespace HandWrite
