#ifndef CORE_HPP
#define CORE_HPP

#include "tools.hpp"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <random>
#include <functional>
#include <QImage>
#include <QPainter>
#include <QFont>
#include <QFile>
#include <QPen>
#include <QPointF>
#include <optional>

namespace HandWrite {

//=============================================================================
// 纸张纹理类型
//=============================================================================
enum class PaperTexture {
    None,           // 纯色背景
    HorizontalLine, // 横线纸
    Grid,           // 方格纸（5mm 标准）
    TianZiGe,       // 田字格
    Composition,    // 作文纸（方格+评分区）
    DotGrid         // 点阵纸
};

//=============================================================================
// 字符级别覆盖设置
//=============================================================================
struct CharacterOverride {
    std::optional<int> fontSize;
    std::optional<double> perturbX;
    std::optional<double> perturbY;
    std::optional<double> perturbTheta;
    std::optional<Color> fillColor;

    bool isEmpty() const {
        return !fontSize.has_value() &&
               !perturbX.has_value() &&
               !perturbY.has_value() &&
               !perturbTheta.has_value() &&
               !fillColor.has_value();
    }
};

struct CharacterOverrideRange {
    int startIndex;
    int endIndex;
    CharacterOverride override;

    bool contains(int index) const {
        return index >= startIndex && index <= endIndex;
    }
};

//=============================================================================
// Markdown 轻标记解析结果
//=============================================================================
enum class TextStyle { Normal, Heading, SubHeading, Strikethrough, Separator };

struct StyledSpan {
    TextStyle style = TextStyle::Normal;
    QString text;           // 纯文本内容
    int fontSizeOverride = 0; // 0 表示使用默认
    bool strikethrough = false;
};

//=============================================================================
// 模板参数（大幅扩展）
//=============================================================================
//=============================================================================
// 文字方向
//=============================================================================
enum class TextDirection { Horizontal, Vertical };

//=============================================================================
// 文字变形模板
//=============================================================================
enum class TextWarp { None, Arc, Wave, Circle };

//=============================================================================
// 背景图片网格校准（NxM 锚点，支持弯曲纸面）
//=============================================================================
struct BackgroundCalibration {
    bool enabled = false;
    int rows = 3;                    // 网格行数
    int cols = 3;                    // 网格列数
    std::vector<QPointF> gridPoints; // rows*cols 个点，行主序，图片坐标空间
    
    bool isValid() const {
        return enabled && rows >= 2 && cols >= 2 
            && static_cast<int>(gridPoints.size()) == rows * cols;
    }
    
    // row r, col c 处的网格点（r 从 0 开始）
    QPointF at(int r, int c) const { return gridPoints[r * cols + c]; }
    void set(int r, int c, const QPointF& p) { gridPoints[r * cols + c] = p; }
    
    // 初始化均匀网格
    void initUniform(int imgWidth, int imgHeight) {
        gridPoints.resize(rows * cols);
        for (int r = 0; r < rows; ++r) {
            qreal y = imgHeight * r / (rows - 1.0);
            for (int c = 0; c < cols; ++c) {
                qreal x = imgWidth * c / (cols - 1.0);
                set(r, c, QPointF(x, y));
            }
        }
    }
};

struct TemplateParams {
    // --- 纸张 ---
    int rate = 4;
    int paperWidth = 667;
    int paperHeight = 945;
    PaperTexture paperTexture = PaperTexture::None;
    double textureOpacity = 0.3;          // 纹理透明度
    std::string backgroundImagePath;      // 背景图片路径（空=不使用）
    BackgroundCalibration bgCalibration;   // 背景图片锚点校准

    // --- 字体 ---
    std::string fontPath;
    int fontSize = 30;
    std::vector<std::string> fontMixList; // 混合字体列表（随机切换）

    // --- 排版 ---
    int lineSpacing = 70;
    int wordSpacing = 1;
    int topMargin = 10;
    int bottomMargin = 10;
    int leftMargin = 10;
    int rightMargin = 10;
    bool paragraphIndent = true;          // 段首缩进两字符
    int paragraphSpacing = 0;             // 段间距（额外行间距像素）
    TextDirection textDirection = TextDirection::Horizontal;  // 文字方向
    TextWarp textWarp = TextWarp::None;                       // 文字变形模板

    // --- 扰动 ---
    double lineSpacingSigma = 1.0;
    double fontSizeSigma = 1.0;
    double wordSpacingSigma = 1.0;
    double perturbXSigma = 1.0;
    double perturbYSigma = 1.0;
    double perturbThetaSigma = 0.05;
    double strokeWidthSigma = 0.3;        // 笔画粗细扰动

    // --- 笔触效果 ---
    bool inkBleed = false;                // 墨水洇染
    double inkBleedRadius = 1.5;          // 洇染半径（缩放后像素）

    // --- 涂改模拟 ---
    double strikeThroughRate = 0.0;       // 划线删除概率 (0~1)

    // --- 排版增强 ---
    bool preserveChinesePunctuation = false;

    // --- 字符 ---
    std::string startChars = "\"（[<";
    std::string endChars = "，。！？";

    // --- 颜色 ---
    Color fillColor = Color(0, 0, 0, 255);
    Color backgroundColor = Color(0, 0, 0, 0);

    // --- 覆盖 ---
    std::vector<CharacterOverrideRange> charOverrides;
};

//=============================================================================
// 单页渲染数据
//=============================================================================
struct PageRenderData {
    int pageIndex;
    std::vector<std::pair<QString, QFont>> lines;
    int scaledWidth;
    int scaledHeight;
    TemplateParams params;
    std::mt19937 rng;

    std::vector<std::vector<int>> charIndexMap;
    std::vector<qreal> lineYPositions;
    
    // 段落信息：lineIdx -> paragraph index，用于段间距
    std::vector<int> lineParagraphIndex;
};

//=============================================================================
// 手写生成器引擎
//=============================================================================
class HandwriteGenerator {
public:
    HandwriteGenerator();
    ~HandwriteGenerator() = default;

    const TemplateParams& templateParams() const { return m_params; }
    TemplateParams& templateParams() { return m_params; }

    void modifyTemplateParams(const TemplateParams& params);
    void setPaperSize(int width, int height);
    void setFont(const std::string& path, int size);
    void setMargins(int top, int bottom, int left, int right);
    void setSpacing(int lineSpacing, int wordSpacing);
    void setColors(const Color& fill, const Color& background);
    void setPerturbations(double lineSpacing, double fontSize, double wordSpacing,
                          double perturbX, double perturbY, double perturbTheta);
    void setRate(int rate);

    // 生成
    std::map<int, std::string> generateImage(const std::string& text,
                                              const std::string& outputDir = "outputs");
    std::vector<QImage> generatePreview(const std::string& text);
    std::vector<QImage> generatePreviewParallel(const std::string& text, int threadCount = 0);
    std::map<int, std::string> generateImageParallel(const std::string& text,
                                                      const std::string& outputDir = "outputs",
                                                      int threadCount = 0,
                                                      std::function<void(int, int)> progressCallback = nullptr);
    
    // PDF 导出
    bool exportPdf(const std::string& text, const std::string& pdfPath);
    
    // SVG 导出
    bool exportSvg(const std::string& text, const std::string& svgPath);

    // 渲染和布局
    static QImage renderPageStatic(const PageRenderData& data);
    
    // 网格形变渲染
    static void warpMesh(QPainter& painter, const QImage& source,
                         const std::vector<QPointF>& srcGrid,
                         const std::vector<QPointF>& dstGrid,
                         int rows, int cols);
    std::vector<PageRenderData> layoutPages(const std::string& text, QFont& font,
                                             int scaledWidth, int scaledHeight,
                                             int contentWidth);

    // 生僻字检测
    std::vector<QChar> findUnsupportedChars(const std::string& text);
    static std::vector<QChar> findUnsupportedCharsStatic(const std::string& text,
                                                          const std::string& fontPath);

private:
    TemplateParams m_params;
    std::mt19937 m_rng;

    // 随机数
    double gaussianRandom(double sigma);
    int gaussianRandomInt(double sigma);
    static double gaussianRandomStatic(double sigma, std::mt19937& rng);
    static int gaussianRandomIntStatic(double sigma, std::mt19937& rng);

    // Markdown 解析
    static std::vector<StyledSpan> parseMarkdown(const QString& text);

    // 文本布局
    std::vector<std::string> layoutText(const QString& text, const QFont& font,
                                         int maxLineWidth, int scaledLineSpacing,
                                         int scaledWordSpacing = 0);
    bool isStartChar(QChar c) const;
    bool isEndChar(QChar c) const;

    // 纸张纹理绘制
    static void drawPaperTexture(QPainter& painter, int width, int height,
                                  PaperTexture texture, int rate, double opacity);

    // 渲染辅助
    static void drawTextWithPerturbationStatic(QPainter& painter, const QString& text,
                                                qreal& x, qreal& y, const QFont& baseFont,
                                                int scaledWordSpacing, const TemplateParams& params,
                                                std::mt19937& rng,
                                                int startCharIndex = 0,
                                                const std::vector<int>* charIndexMap = nullptr);
    
    // 墨水洇染效果
    static void applyInkBleed(QImage& image, double radius);
    
    // 混合字体选择
    static QFont pickMixedFont(const QFont& baseFont, 
                               const std::vector<std::string>& fontMixList);
};

} // namespace HandWrite

#endif // CORE_HPP
