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
    
    // ---- 随机种子：0 = 每次随机；非 0 = 固定，预览与导出结果一致 ----
    std::mt19937 seedRng = (m_params.seed != 0)
        ? std::mt19937(static_cast<std::mt19937::result_type>(m_params.seed))
        : std::mt19937(std::random_device{}());
    
    std::vector<RenderLine> currentPage;
    std::vector<std::vector<int>> currentPageCharIndexMap;
    std::vector<qreal> currentPageYPositions;
    std::vector<int> currentPageParaIndex;
    
    qreal y = scaledTopMargin + fm.ascent();
    int paraIndex = 0;
    
    // 纹理网格对齐: 计算网格尺寸
    const int textureGridSize = (m_params.paperTexture == PaperTexture::Composition) ? 28 : 25;
    const int scaledGrid = textureGridSize * m_params.rate;
    const bool alignToGrid = (m_params.paperTexture != PaperTexture::None);
    
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
        pageDataList.push_back(std::move(pageData));
        
        currentPage.clear();
        currentPageCharIndexMap.clear();
        currentPageYPositions.clear();
        currentPageParaIndex.clear();
    };
    
    for (const auto& line : textLines) {
        // 空行 = 段落分隔
        if (line.text.isEmpty()) {
            paraIndex++;
            continue;
        }
        
        const bool isSeparator = (line.text == QStringLiteral("---"));
        
        if (!currentPage.empty()) {
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
                                                         qreal warpWavelength) {
    Q_UNUSED(startCharIndex);
    QFontMetrics fm(baseFont);
    std::uniform_real_distribution<double> strikeDist(0.0, 1.0);
    // 逐字变形的相位累加器（旧实现用行首常量算相位，等于整行平移）
    qreal warpPhase = warpPhaseBase;
    const bool warpActive = (warp != TextWarp::None) && (warpWavelength > 1.0)
                            && (std::abs(warpAmplitude) > 0.0);
    
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
        
        // 涂改效果（drawY 已含逐字变形偏移）
        const qreal drawY = y + perturbY + warpOffset;
        if (strikethrough) {
            const bool rotated = std::abs(perturbTheta) > 0.001;
            if (rotated) {
                painter.translate(x + perturbX, drawY);
                painter.rotate(perturbTheta * 180.0 / M_PI);
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
            if (std::abs(perturbTheta) > 0.001) {
                painter.translate(x + perturbX, drawY);
                painter.rotate(perturbTheta * 180.0 / M_PI);
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
    bool useCalibration = data.params.bgCalibration.isValid()
                          && !bgImage.isNull()
                          && !data.params.backgroundImagePath.empty();
    
    // 决定在哪个画布上绘制文字
    QImage* textCanvas = &image;  // 默认直接画在主图上
    QImage textOverlay;           // 校准模式下单独的文字画布
    
    if (useCalibration) {
        textOverlay = QImage(data.scaledWidth, data.scaledHeight, QImage::Format_ARGB32);
        textOverlay.fill(Qt::transparent);
        textCanvas = &textOverlay;
    }
    
    QPainter painter(textCanvas);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    
    if (!bgImage.isNull()) {
        if (useCalibration) {
            // 校准模式：背景画到主图
            QPainter bgPainter(&image);
            bgPainter.drawImage(image.rect(), bgImage);
            bgPainter.end();
        } else {
            painter.drawImage(image.rect(), bgImage);
        }
    }
    
    // 纸张纹理（校准模式下跳过，因为纹理线无法透视变换）
    if (!useCalibration) {
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
        
        drawTextWithPerturbationStatic(painter, line, x, y, font,
                                       data.params.wordSpacing * data.params.rate,
                                       data.params, localRng, 0, lineCharIndexMap,
                                       warp, warpPhaseBase, warpAmplitude, warpWavelength);
    }
    
    painter.end();
    
    // 网格形变：将文字画布映射到背景图片的校准区域
    if (useCalibration) {
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
