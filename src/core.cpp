#include "core.hpp"
#include <QDir>
#include <QFontDatabase>
#include <QThread>
#include <QtConcurrent>
#include <QThreadPool>
#include <QPrinter>
#include <QPainterPath>
#include <QLinearGradient>
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>
#include <QImageReader>

namespace HandWrite {

//=============================================================================
// 工具函数
//=============================================================================

static QString convertChinesePunctuation(const QString& text) {
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

static CharacterOverride findCharOverride(int globalCharIndex, const std::vector<CharacterOverrideRange>& overrides) {
    for (const auto& range : overrides) {
        if (range.contains(globalCharIndex)) {
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
    
    auto flush = [&]() {
        if (!current.isEmpty()) {
            StyledSpan span;
            span.style = currentStyle;
            span.text = current;
            if (currentStyle == TextStyle::Heading) span.fontSizeOverride = 48;
            else if (currentStyle == TextStyle::SubHeading) span.fontSizeOverride = 36;
            else if (currentStyle == TextStyle::Strikethrough) span.strikethrough = true;
            spans.push_back(span);
            current.clear();
        }
    };
    
    while (i < text.length()) {
        // 分割线
        if (text.mid(i, 3) == "---" || text.mid(i, 3) == "***") {
            flush();
            StyledSpan sep;
            sep.style = TextStyle::Separator;
            sep.text = "---";
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
        if (i + 1 < text.length() && text[i] == '~' && text[i+1] == '~') {
            flush();
            currentStyle = TextStyle::Strikethrough;
            i += 2;
            continue;
        }
        if (currentStyle == TextStyle::Strikethrough && 
            i + 1 < text.length() && text[i] == '~' && text[i+1] == '~') {
            flush();
            currentStyle = TextStyle::Normal;
            i += 2;
            continue;
        }
        
        // 换行重置样式
        if (text[i] == '\n') {
            flush();
            currentStyle = TextStyle::Normal;
            StyledSpan newline;
            newline.style = TextStyle::Normal;
            newline.text = "\n";
            spans.push_back(newline);
            i++;
            continue;
        }
        
        current += text[i];
        i++;
    }
    flush();
    return spans;
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
void HandwriteGenerator::setPaperSize(int width, int height) { m_params.paperWidth = width; m_params.paperHeight = height; }
void HandwriteGenerator::setFont(const std::string& path, int size) { m_params.fontPath = path; m_params.fontSize = size; }
void HandwriteGenerator::setMargins(int top, int bottom, int left, int right) {
    m_params.topMargin = top; m_params.bottomMargin = bottom;
    m_params.leftMargin = left; m_params.rightMargin = right;
}
void HandwriteGenerator::setSpacing(int lineSpacing, int wordSpacing) {
    m_params.lineSpacing = lineSpacing; m_params.wordSpacing = wordSpacing;
}
void HandwriteGenerator::setColors(const Color& fill, const Color& background) {
    m_params.fillColor = fill; m_params.backgroundColor = background;
}
void HandwriteGenerator::setPerturbations(double lineSpacing, double fontSize, double wordSpacing,
                                          double perturbX, double perturbY, double perturbTheta) {
    m_params.lineSpacingSigma = lineSpacing; m_params.fontSizeSigma = fontSize;
    m_params.wordSpacingSigma = wordSpacing; m_params.perturbXSigma = perturbX;
    m_params.perturbYSigma = perturbY; m_params.perturbThetaSigma = perturbTheta;
}
void HandwriteGenerator::setRate(int rate) { m_params.rate = rate; }

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

bool HandwriteGenerator::isStartChar(QChar c) const { return m_params.startChars.find(c.toLatin1()) != std::string::npos; }
bool HandwriteGenerator::isEndChar(QChar c) const { return m_params.endChars.find(c.toLatin1()) != std::string::npos; }
bool HandwriteGenerator::isStartCharStatic(QChar c, const std::string& startChars) { return startChars.find(c.toLatin1()) != std::string::npos; }
bool HandwriteGenerator::isEndCharStatic(QChar c, const std::string& endChars) { return endChars.find(c.toLatin1()) != std::string::npos; }

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

QFont HandwriteGenerator::pickMixedFont(const QFont& baseFont,
                                         const std::vector<std::string>& fontMixList) {
    if (fontMixList.empty()) return baseFont;
    
    static thread_local std::mt19937 mixRng(std::random_device{}());
    static thread_local std::uniform_real_distribution<double> mixDist(0.0, 1.0);
    
    // 80% 概率使用基础字体，20% 概率随机切换到混合字体
    if (mixDist(mixRng) > 0.2) return baseFont;
    
    int idx = static_cast<int>(mixDist(mixRng) * fontMixList.size()) % fontMixList.size();
    QString path = QString::fromStdString(fontMixList[idx]);
    int fontId = QFontDatabase::addApplicationFont(path);
    if (fontId != -1) {
        QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        if (!families.isEmpty()) {
            QFont mixed(families[0]);
            mixed.setPixelSize(baseFont.pixelSize());
            return mixed;
        }
    }
    return baseFont;
}

//=============================================================================
// 墨水洇染效果
//=============================================================================

void HandwriteGenerator::applyInkBleed(QImage& image, double radius) {
    if (radius <= 0) return;
    // 使用小范围模糊模拟墨水洇染
    // 简化实现：对非透明像素周围添加半透明颜色
    QImage src = image.copy();
    int r = static_cast<int>(std::ceil(radius));
    
    for (int y = r; y < image.height() - r; ++y) {
        for (int x = r; x < image.width() - r; ++x) {
            QRgb pixel = src.pixel(x, y);
            if (qAlpha(pixel) == 0) {
                // 检查周围是否有非透明像素
                bool hasInk = false;
                int rSum = 0, gSum = 0, bSum = 0, count = 0;
                for (int dy = -r; dy <= r; ++dy) {
                    for (int dx = -r; dx <= r; ++dx) {
                        QRgb np = src.pixel(x + dx, y + dy);
                        if (qAlpha(np) > 128) {
                            hasInk = true;
                            rSum += qRed(np);
                            gSum += qGreen(np);
                            bSum += qBlue(np);
                            count++;
                        }
                    }
                }
                if (hasInk && count > 0) {
                    int alpha = qMin(40, 255 / (r * 2));
                    image.setPixel(x, y, qRgba(rSum/count, gSum/count, bSum/count, alpha));
                }
            }
        }
    }
}

//=============================================================================
// 文本布局（支持段落缩进和段间距）
//=============================================================================

std::vector<std::string> HandwriteGenerator::layoutText(const QString& text, const QFont& font,
                                                         int maxLineWidth, int scaledLineSpacing,
                                                         int scaledWordSpacing) {
    std::vector<std::string> lines;
    QFontMetrics fm(font);
    
    // Markdown 解析
    auto spans = parseMarkdown(text);
    
    int perturbationMargin = static_cast<int>(m_params.fontSizeSigma * m_params.rate * 0.5) +
                             static_cast<int>(m_params.perturbXSigma * m_params.rate) +
                             static_cast<int>(m_params.wordSpacingSigma * m_params.rate * 2);
    int effectiveWidth = maxLineWidth - perturbationMargin;
    if (effectiveWidth < maxLineWidth / 2) effectiveWidth = maxLineWidth / 2;
    
    // 段首缩进宽度（两个汉字）
    int indentWidth = m_params.paragraphIndent ? fm.horizontalAdvance("哈") * 2 : 0;
    
    QString currentLine;
    int currentWidth = 0;
    bool newParagraph = true; // 第一行是段首
    
    for (const auto& span : spans) {
        if (span.style == TextStyle::Separator) {
            if (!currentLine.isEmpty()) { lines.push_back(currentLine.toStdString()); currentLine.clear(); currentWidth = 0; }
            lines.push_back("---");
            newParagraph = true;
            continue;
        }
        
        QString converted = convertChinesePunctuation(span.text);
        int fs = span.fontSizeOverride > 0 ? span.fontSizeOverride * m_params.rate : 0;
        
        for (int i = 0; i < converted.length(); ++i) {
            QChar c = converted[i];
            
            if (c == '\n') {
                lines.push_back(currentLine.toStdString());
                currentLine.clear();
                currentWidth = 0;
                newParagraph = true;
                continue;
            }
            
            int charWidth = fm.horizontalAdvance(c) + scaledWordSpacing;
            
            // 段首缩进
            if (newParagraph && currentLine.isEmpty() && indentWidth > 0) {
                // 使用全角空格模拟缩进
                currentLine += QString(QChar(0x3000)) + QChar(0x3000);
                currentWidth = indentWidth;
                newParagraph = false;
            }
            
            if (currentWidth + charWidth > effectiveWidth && !currentLine.isEmpty()) {
                if (isEndChar(c)) {
                    currentLine += c;
                    lines.push_back(currentLine.toStdString());
                    currentLine.clear(); currentWidth = 0;
                    continue;
                }
                if (!currentLine.isEmpty() && isStartChar(currentLine.back())) {
                    QChar lastChar = currentLine.back();
                    currentLine.chop(1);
                    lines.push_back(currentLine.toStdString());
                    currentLine = lastChar;
                    currentWidth = fm.horizontalAdvance(lastChar) + scaledWordSpacing;
                } else {
                    lines.push_back(currentLine.toStdString());
                    currentLine.clear(); currentWidth = 0;
                }
            }
            
            currentLine += c;
            currentWidth += charWidth;
        }
    }
    
    if (!currentLine.isEmpty()) lines.push_back(currentLine.toStdString());
    return lines;
}

//=============================================================================
// 分页布局
//=============================================================================

std::vector<PageRenderData> HandwriteGenerator::layoutPages(const std::string& text, QFont& font,
                                                              int scaledWidth, int scaledHeight,
                                                              int contentWidth) {
    std::vector<PageRenderData> pageDataList;
    
    // 竖排模式
    if (m_params.textDirection == TextDirection::Vertical) {
        int scaledTopMargin = m_params.topMargin * m_params.rate;
        int scaledBottomMargin = m_params.bottomMargin * m_params.rate;
        int scaledLeftMargin = m_params.leftMargin * m_params.rate;
        int scaledRightMargin = m_params.rightMargin * m_params.rate;
        int lineHeight = m_params.lineSpacing * m_params.rate;
        int wordSpacing = m_params.wordSpacing * m_params.rate;
        
        QFontMetrics fm(font);
        int columnWidth = fm.maxWidth() + wordSpacing;
        int contentHeight = scaledHeight - scaledTopMargin - scaledBottomMargin;
        int charsPerColumn = std::max(1, contentHeight / lineHeight);
        
        QString qText = QString::fromStdString(text);
        QStringList paragraphs = qText.split("\n\n");
        
        std::vector<std::pair<QString, QFont>> currentPage;
        std::vector<qreal> currentPageYPositions;
        std::vector<std::vector<int>> currentPageCharIndexMap;
        
        qreal colX = scaledWidth - scaledRightMargin - columnWidth;
        int globalCharIndex = 0;
        
        std::random_device rd;
        std::mt19937 seedRng(rd());
        
        auto flushPage = [&]() {
            PageRenderData pd;
            pd.pageIndex = static_cast<int>(pageDataList.size());
            pd.lines = std::move(currentPage);
            pd.scaledWidth = scaledWidth;
            pd.scaledHeight = scaledHeight;
            pd.params = m_params;
            pd.rng.seed(seedRng());
            pd.charIndexMap = std::move(currentPageCharIndexMap);
            pd.lineYPositions = std::move(currentPageYPositions);
            pageDataList.push_back(std::move(pd));
            currentPage.clear(); currentPageYPositions.clear(); currentPageCharIndexMap.clear();
            colX = scaledWidth - scaledRightMargin - columnWidth;
        };
        
        for (const QString& para : paragraphs) {
            if (para.trimmed().isEmpty()) continue;
            QString col;
            std::vector<int> colIndices;
            
            for (QChar ch : para) {
                if (ch == '\n') continue;
                if (col.size() >= charsPerColumn) {
                    if (colX < scaledLeftMargin) flushPage();
                    currentPage.push_back({col, font});
                    currentPageYPositions.push_back(colX);
                    currentPageCharIndexMap.push_back(std::move(colIndices));
                    colX -= columnWidth;
                    col.clear();
                    colIndices.clear();
                }
                col += ch;
                colIndices.push_back(globalCharIndex++);
            }
            // 段落末：保留当前列，下一段从新列开始
            if (!col.isEmpty()) {
                if (colX < scaledLeftMargin) flushPage();
                currentPage.push_back({col, font});
                currentPageYPositions.push_back(colX);
                currentPageCharIndexMap.push_back(std::move(colIndices));
                colX -= columnWidth;
            }
        }
        
        if (!currentPage.empty()) flushPage();
        if (pageDataList.empty()) {
            PageRenderData pd; pd.pageIndex = 0;
            pd.scaledWidth = scaledWidth; pd.scaledHeight = scaledHeight;
            pd.params = m_params; pd.rng.seed(seedRng());
            pageDataList.push_back(std::move(pd));
        }
        return pageDataList;
    }
    
    // ---- 横排模式（原有逻辑）----
    
    int scaledTopMargin = m_params.topMargin * m_params.rate;
    int scaledBottomMargin = m_params.bottomMargin * m_params.rate;
    int lineHeight = m_params.lineSpacing * m_params.rate;
    int paraSpacing = m_params.paragraphSpacing * m_params.rate;
    
    QString qText = QString::fromStdString(text);
    std::vector<std::string> textLines = layoutText(qText, font, contentWidth, lineHeight,
                                                     m_params.wordSpacing * m_params.rate);
    
    QFontMetrics fm(font);
    
    std::vector<std::pair<QString, QFont>> currentPage;
    std::vector<std::vector<int>> currentPageCharIndexMap;
    std::vector<qreal> currentPageYPositions;
    std::vector<int> currentPageParaIndex;
    
    qreal y = scaledTopMargin + fm.ascent();
    int globalCharIndex = 0;
    int paraIndex = 0;
    
    // 纹理网格对齐: 计算网格尺寸
    int textureGridSize = 25;  // 默认横线纸/方格纸/田字格/点阵纸
    if (m_params.paperTexture == PaperTexture::Composition) textureGridSize = 28;
    int scaledGrid = textureGridSize * m_params.rate;
    bool alignToGrid = (m_params.paperTexture != PaperTexture::None);
    
    if (alignToGrid) {
        // 首行对齐到最近网格线，至少到第一个实用网格线
        qreal snapped = std::round(y / scaledGrid) * scaledGrid;
        y = std::max(snapped, static_cast<qreal>(scaledGrid));
    }
    
    std::random_device rd;
    std::mt19937 seedRng(rd());
    
    for (const auto& lineStr : textLines) {
        // 检测空行（段落分隔）
        bool isEmptyLine = lineStr.empty() || lineStr == "---";
        
        if (isEmptyLine) {
            // 分割线也视为新段落标记
            paraIndex++;
            continue;
        }
        
        if (!currentPage.empty()) {
            // 段间距：当前行是段首且上一行不是空行
            bool isNewPara = (currentPageParaIndex.empty() || 
                             currentPageParaIndex.back() != paraIndex);
            int extraSpacing = 0;
            if (isNewPara && !currentPageParaIndex.empty()) {
                extraSpacing = paraSpacing;
            }
            
            int perturbedLineSpacing = lineHeight + 
                gaussianRandomInt(m_params.lineSpacingSigma * m_params.rate);
            qreal nextY = y + perturbedLineSpacing + extraSpacing;
            
            // 对齐到纹理网格线
            if (alignToGrid) {
                nextY = std::round(nextY / scaledGrid) * scaledGrid;
                if (nextY < scaledGrid) nextY = scaledGrid;
            }
            
            if (nextY + fm.descent() > scaledHeight - scaledBottomMargin) {
                PageRenderData pageData;
                pageData.pageIndex = static_cast<int>(pageDataList.size());
                pageData.lines = std::move(currentPage);
                pageData.scaledWidth = scaledWidth;
                pageData.scaledHeight = scaledHeight;
                pageData.params = m_params;
                pageData.rng.seed(seedRng());
                pageData.charIndexMap = std::move(currentPageCharIndexMap);
                pageData.lineYPositions = std::move(currentPageYPositions);
                pageData.lineParagraphIndex = std::move(currentPageParaIndex);
                pageDataList.push_back(std::move(pageData));
                
                currentPage.clear();
                currentPageCharIndexMap.clear();
                currentPageYPositions.clear();
                currentPageParaIndex.clear();
                y = scaledTopMargin + fm.ascent();
            } else {
                y = nextY;
            }
        }
        
        currentPageYPositions.push_back(y);
        currentPageParaIndex.push_back(paraIndex);
        
        QString lineText = QString::fromStdString(lineStr);
        std::vector<int> lineCharIndices;
        for (int i = 0; i < lineText.length(); ++i) {
            lineCharIndices.push_back(globalCharIndex++);
        }
        
        currentPage.push_back({lineText, font});
        currentPageCharIndexMap.push_back(std::move(lineCharIndices));
    }
    
    if (!currentPage.empty()) {
        PageRenderData pageData;
        pageData.pageIndex = static_cast<int>(pageDataList.size());
        pageData.lines = std::move(currentPage);
        pageData.scaledWidth = scaledWidth;
        pageData.scaledHeight = scaledHeight;
        pageData.params = m_params;
        pageData.rng.seed(seedRng());
        pageData.charIndexMap = std::move(currentPageCharIndexMap);
        pageData.lineYPositions = std::move(currentPageYPositions);
        pageData.lineParagraphIndex = std::move(currentPageParaIndex);
        pageDataList.push_back(std::move(pageData));
    }
    
    if (pageDataList.empty()) {
        PageRenderData pageData;
        pageData.pageIndex = 0;
        pageData.scaledWidth = scaledWidth;
        pageData.scaledHeight = scaledHeight;
        pageData.params = m_params;
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
                                                         const std::vector<int>* charIndexMap) {
    QFontMetrics fm(baseFont);
    std::uniform_real_distribution<double> strikeDist(0.0, 1.0);
    
    for (int i = 0; i < text.length(); ++i) {
        QChar c = text[i];
        
        if (!fm.inFont(c)) {
            x += fm.height() * 0.6 + scaledWordSpacing +
                 gaussianRandomStatic(params.wordSpacingSigma * params.rate, rng);
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
        
        // 字体选择（支持混合字体）
        QFont activeFont = baseFont;
        if (!params.fontMixList.empty() && !override.fontSize.has_value()) {
            activeFont = pickMixedFont(baseFont, params.fontMixList);
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
        
        // 涂改效果
        if (strikethrough) {
            // 先绘制字符
            if (std::abs(perturbTheta) > 0.001) {
                painter.translate(x + perturbX, y + perturbY);
                painter.rotate(perturbTheta * 180.0 / M_PI);
                painter.drawText(0, 0, QString(c));
            } else {
                painter.drawText(x + perturbX, y + perturbY, QString(c));
            }
            // 绘制横线删除标记
            int charW = perturbedFm.horizontalAdvance(c);
            int midY = static_cast<int>(y + perturbY - perturbedFm.ascent() / 2);
            QPen strikePen(QColor(80, 80, 80, 180));
            strikePen.setWidthF(1.5);
            painter.setPen(strikePen);
            painter.drawLine(static_cast<int>(x + perturbX - 2), midY,
                             static_cast<int>(x + perturbX + charW + 2), midY);
        } else {
            if (std::abs(perturbTheta) > 0.001) {
                painter.translate(x + perturbX, y + perturbY);
                painter.rotate(perturbTheta * 180.0 / M_PI);
                painter.drawText(0, 0, QString(c));
            } else {
                painter.drawText(x + perturbX, y + perturbY, QString(c));
            }
        }
        
        painter.restore();
        
        x += perturbedFm.horizontalAdvance(c) + scaledWordSpacing +
             gaussianRandomStatic(params.wordSpacingSigma * params.rate, rng);
    }
}

//=============================================================================
// 页面渲染（增强版：纸张纹理、背景图片、墨水洇染）
//=============================================================================

QImage HandwriteGenerator::renderPageStatic(const PageRenderData& data) {
    QImage image(data.scaledWidth, data.scaledHeight, QImage::Format_ARGB32);
    image.fill(QColor(data.params.backgroundColor.r, data.params.backgroundColor.g,
                      data.params.backgroundColor.b, data.params.backgroundColor.a));
    
    bool useCalibration = data.params.bgCalibration.isValid();
    
    // 决定在哪个画布上绘制文字
    QImage* textCanvas = &image;  // 默认直接画在主图上
    QImage textOverlay;           // 校准模式下单独的文字画布
    QPoint textOffset(0, 0);
    
    if (useCalibration) {
        // 文字先画到独立的透明画布，再透视变换到主图
        textOverlay = QImage(data.scaledWidth, data.scaledHeight, QImage::Format_ARGB32);
        textOverlay.fill(Qt::transparent);
        textCanvas = &textOverlay;
    }
    
    QPainter painter(textCanvas);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    
    // 背景图片（画在主图上或文字画布上）
    QImage bgImage;
    if (!data.params.backgroundImagePath.empty()) {
        bgImage = loadImageWithWebpFallback(data.params.backgroundImagePath);
        if (bgImage.isNull() || bgImage.width() < 1 || bgImage.height() < 1) {
            bgImage = QImage();  // 确保为 null，禁用校准
            useCalibration = false;
        }
    } else {
        useCalibration = false;
    }
    
    if (useCalibration) {
        // 校准模式：背景画到主图
        QPainter bgPainter(&image);
        bgPainter.drawImage(image.rect(), bgImage.scaled(data.scaledWidth, data.scaledHeight,
                             Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        bgPainter.end();
    } else if (!bgImage.isNull()) {
        painter.drawImage(image.rect(), bgImage.scaled(data.scaledWidth, data.scaledHeight,
                           Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
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
    bool isVertical = (data.params.textDirection == TextDirection::Vertical);
    
    for (size_t lineIdx = 0; lineIdx < data.lines.size(); ++lineIdx) {
        const auto& [line, font] = data.lines[lineIdx];
        QFontMetrics fm(font);
        
        if (isVertical) {
            // 竖排：每条 "line" 是一列，lineYPositions[i] = 列 X 坐标
            if (!usePrecomputedY) continue;
            qreal colX = data.lineYPositions[lineIdx];
            qreal charY = contentTop + fm.ascent();
            int charIdxBase = 0;
            if (lineIdx < data.charIndexMap.size()) {
                charIdxBase = data.charIndexMap[lineIdx].empty() ? 0 : data.charIndexMap[lineIdx][0];
            }
            
            for (int ci = 0; ci < line.size(); ++ci) {
                if (charY + fm.descent() > contentBottom) break;
                painter.setFont(font);
                qreal dx = 0, dy = 0;
                painter.save();
                drawTextWithPerturbationStatic(painter, QString(line[ci]), colX, charY, font,
                    data.params.wordSpacing * data.params.rate,
                    data.params, localRng, charIdxBase + ci, nullptr);
                painter.restore();
                charY += data.params.lineSpacing * data.params.rate +
                    gaussianRandomStatic(data.params.lineSpacingSigma * data.params.rate, localRng);
            }
            continue;
        }
        
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
        
        qreal x = contentLeft;
        painter.setFont(font);
        
        const std::vector<int>* lineCharIndexMap = nullptr;
        if (lineIdx < data.charIndexMap.size()) {
            lineCharIndexMap = &data.charIndexMap[lineIdx];
        }
        
        drawTextWithPerturbationStatic(painter, line, x, y, font,
                                       data.params.wordSpacing * data.params.rate,
                                       data.params, localRng, 0, lineCharIndexMap);
    }
    
    painter.end();
    
    // 网格形变：将文字画布映射到背景图片的校准区域
    if (useCalibration) {
        auto& cal = data.params.bgCalibration;
        
        // 缩放锚点到渲染画布坐标
        int bw = bgImage.width(); if (bw < 1) bw = 1;
        int bh = bgImage.height(); if (bh < 1) bh = 1;
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

std::vector<QImage> HandwriteGenerator::generatePreview(const std::string& text) {
    int scaledWidth = m_params.paperWidth * m_params.rate;
    int scaledHeight = m_params.paperHeight * m_params.rate;
    int contentWidth = scaledWidth - (m_params.leftMargin + m_params.rightMargin) * m_params.rate;
    
    QFont font = loadFont(m_params.fontPath, m_params.fontSize * m_params.rate);
    auto pageDataList = layoutPages(text, font, scaledWidth, scaledHeight, contentWidth);
    
    std::vector<QImage> images;
    images.reserve(pageDataList.size());
    for (const auto& pd : pageDataList) images.push_back(renderPageStatic(pd));
    
    if (images.empty()) {
        QImage empty(scaledWidth, scaledHeight, QImage::Format_ARGB32);
        empty.fill(QColor(m_params.backgroundColor.r, m_params.backgroundColor.g,
                          m_params.backgroundColor.b, m_params.backgroundColor.a));
        images.push_back(empty);
    }
    return images;
}

std::map<int, std::string> HandwriteGenerator::generateImage(const std::string& text,
                                                              const std::string& outputDir) {
    std::map<int, std::string> filePaths;
    QDir dir;
    if (!dir.exists(QString::fromStdString(outputDir))) dir.mkpath(QString::fromStdString(outputDir));
    QDir(QString::fromStdString(outputDir)).removeRecursively();
    dir.mkpath(QString::fromStdString(outputDir));
    
    auto images = generatePreview(text);
    for (size_t i = 0; i < images.size(); ++i) {
        QString path = QString::fromStdString(outputDir) + "/" + QString::number(i) + ".png";
        if (images[i].save(path, "PNG")) filePaths[static_cast<int>(i)] = path.toStdString();
    }
    return filePaths;
}

std::vector<QImage> HandwriteGenerator::generatePreviewParallel(const std::string& text, int threadCount) {
    if (threadCount <= 0) { threadCount = QThread::idealThreadCount(); if (threadCount <= 0) threadCount = 4; }
    
    int scaledWidth = m_params.paperWidth * m_params.rate;
    int scaledHeight = m_params.paperHeight * m_params.rate;
    int contentWidth = scaledWidth - (m_params.leftMargin + m_params.rightMargin) * m_params.rate;
    
    QFont font = loadFont(m_params.fontPath, m_params.fontSize * m_params.rate);
    auto pageDataList = layoutPages(text, font, scaledWidth, scaledHeight, contentWidth);
    
    QThreadPool::globalInstance()->setMaxThreadCount(threadCount);
    return QtConcurrent::blockingMapped<std::vector<QImage>>(pageDataList, renderPageStatic);
}

std::map<int, std::string> HandwriteGenerator::generateImageParallel(const std::string& text,
                                                                      const std::string& outputDir,
                                                                      int threadCount,
                                                                      std::function<void(int, int)> progressCallback) {
    std::map<int, std::string> filePaths;
    QDir dir;
    if (!dir.exists(QString::fromStdString(outputDir))) dir.mkpath(QString::fromStdString(outputDir));
    QDir(QString::fromStdString(outputDir)).removeRecursively();
    dir.mkpath(QString::fromStdString(outputDir));
    
    if (threadCount <= 0) { threadCount = QThread::idealThreadCount(); if (threadCount <= 0) threadCount = 4; }
    
    int scaledWidth = m_params.paperWidth * m_params.rate;
    int scaledHeight = m_params.paperHeight * m_params.rate;
    int contentWidth = scaledWidth - (m_params.leftMargin + m_params.rightMargin) * m_params.rate;
    
    QFont font = loadFont(m_params.fontPath, m_params.fontSize * m_params.rate);
    auto pageDataList = layoutPages(text, font, scaledWidth, scaledHeight, contentWidth);
    
    int totalPages = static_cast<int>(pageDataList.size());
    int totalSteps = totalPages * 2;
    if (progressCallback) progressCallback(0, totalSteps);
    
    QThreadPool::globalInstance()->setMaxThreadCount(threadCount);
    QFuture<QImage> future = QtConcurrent::mapped(pageDataList, renderPageStatic);
    
    int renderCompleted = 0;
    while (!future.isFinished()) {
        QThread::msleep(50);
        int completed = 0;
        for (int i = 0; i < totalPages; ++i)
            if (future.isResultReadyAt(i)) completed++;
        if (completed != renderCompleted && progressCallback) {
            progressCallback(completed, totalSteps);
            renderCompleted = completed;
        }
    }
    
    QList<QImage> images = future.results();
    for (int i = 0; i < images.size() && i < totalPages; ++i) {
        QString path = QString::fromStdString(outputDir) + "/" + QString::number(i) + ".png";
        if (images[i].save(path, "PNG")) filePaths[i] = path.toStdString();
        if (progressCallback) progressCallback(totalPages + i + 1, totalSteps);
    }
    return filePaths;
}

//=============================================================================
// PDF 导出
//=============================================================================

bool HandwriteGenerator::exportPdf(const std::string& text, const std::string& pdfPath) {
    auto images = generatePreview(text);
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
// 生僻字检测
//=============================================================================

std::vector<QChar> HandwriteGenerator::findUnsupportedChars(const std::string& text) {
    return findUnsupportedCharsStatic(text, m_params.fontPath);
}

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
            // 获取源网格四角
            QPointF s00 = srcGrid[r * cols + c];
            QPointF s10 = srcGrid[r * cols + c + 1];
            QPointF s11 = srcGrid[(r + 1) * cols + c + 1];
            QPointF s01 = srcGrid[(r + 1) * cols + c];
            
            // 目标网格四角
            QPointF d00 = dstGrid[r * cols + c];
            QPointF d10 = dstGrid[r * cols + c + 1];
            QPointF d11 = dstGrid[(r + 1) * cols + c + 1];
            QPointF d01 = dstGrid[(r + 1) * cols + c];
            
            QPolygonF srcQuad;
            srcQuad << s00 << s10 << s11 << s01;
            QPolygonF dstQuad;
            dstQuad << d00 << d10 << d11 << d01;
            
            QTransform warp;
            QTransform::quadToQuad(srcQuad, dstQuad, warp);
            
            if (!warp.isIdentity()) {
                // 裁剪到目标四边形区域
                QPainterPath clip;
                clip.addPolygon(dstQuad);
                painter.save();
                painter.setClipPath(clip);
                painter.setTransform(warp);
                painter.drawImage(0, 0, source);
                painter.restore();
            }
        }
    }
}

} // namespace HandWrite
