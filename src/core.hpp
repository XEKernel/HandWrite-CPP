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
#include <QSize>
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
        return index >= 0 && index >= startIndex && index <= endIndex;
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
    // 该 span 内首个字符在原文中的索引（-1 = 无对应，如程序插入的换行）
    // 用于把「渲染索引」正确映射回「原文索引」，避免字符级覆盖错位
    int origStart = -1;
};

//=============================================================================
// 文字方向 / 文字变形
//=============================================================================
enum class TextDirection { Horizontal, Vertical };
enum class TextWarp { None, Arc, Wave, Circle };

//=============================================================================
// 背景图横线导引（Line Guides）
//=============================================================================
// 让文字排布在作业本照片的印刷横线上。纸张会弯曲，所以横线是**曲线**，
// 用采样折线表示（而非两点式直线，也非贝塞尔 —— 后者拖控制点不直观）。
//
// 坐标空间：与 BackgroundCalibration::gridPoints 一致，存「背景原图」坐标，
// 渲染前统一乘 sx/sy 换算到画布空间。
struct GuideCurve {
    std::vector<QPointF> pts;   // 采样点，按 x 升序

    bool usable() const { return pts.size() >= 2; }

    // 按 x 采样：线性插值出 y，并给出该处切线角（弧度，正 = 顺时针）。
    // x 超出范围时夹到端点（不做外推，避免边缘乱飞）。
    void sample(qreal x, qreal* y, qreal* angle) const;

    qreal yAt(qreal x) const { qreal v = 0, a = 0; sample(x, &v, &a); return v; }
    qreal angleAt(qreal x) const { qreal v = 0, a = 0; sample(x, &v, &a); return a; }

    // 手绘后调用：按 x 排序 -> 去重 -> 移动平均平滑
    void normalize(int smoothPasses = 2);

    // 按弧长均匀重采样为 n 个点（不同曲线点数不一致时靠它对齐后才能插值）
    std::vector<QPointF> resampleByArcLength(int n) const;
};

struct LineGuideSet {
    bool enabled = false;

    // 关键曲线：用户手绘的 K 条（K >= 2），中间按弧长参数插值自动补全。
    // 因为纸张弯曲是连续形变，画 2 条（首、尾）再插值通常就够了，
    // 中间鼓起时再补画 1 条即可分段插值 —— 交互成本从「画 20 条」降到「画 2~3 条」。
    std::vector<GuideCurve> keyCurves;
    int  lineCount = 20;             // 最终条数（含首尾关键曲线）
    bool useInterpolation = true;    // false = 只用手绘的那几条

    // --- 文字落位 ---
    double baselineRatio   = 0.82;   // 0 = 贴上线，1 = 贴下线
    int    baselineOffset  = 0;      // 未乘 rate 的像素微调
    bool   followCurve     = true;   // 逐字跟随曲线弯曲（关闭则退化为直线 + 行首倾角）
    int    linesPerRow     = 1;      // 一行文字占几条横线的高度

    bool isValid() const {
        if (!enabled || keyCurves.empty()) return false;
        if (!useInterpolation) return true;
        return keyCurves.size() >= 2 && lineCount >= 2;
    }

    // 由关键曲线 + lineCount 生成完整曲线列表（弧长参数插值）
    std::vector<GuideCurve> build() const;
};

// 导引曲线 <-> 配置用的平铺 double 数组。
// 编码：每条曲线 = [n, x0,y0, x1,y1, ...]，依次拼接。
// 之所以不直接存字符串，是为了复用 Config 现有的 double[] 读写（含引号/转义处理）。
std::vector<GuideCurve> parseGuideCurves(const std::vector<double>& flat);
std::vector<double> flattenGuideCurves(const std::vector<GuideCurve>& curves);

//=============================================================================
// 单行排版结果
//=============================================================================
struct LineLayout {
    QString text;                  // 该行的渲染文本（含缩进用全角空格）
    int fontSizeOverride = 0;      // >0 = 标题字号（未乘 rate 的像素值）
    std::vector<int> origIndices;  // text[i] 对应原文索引；-1 = 程序插入的缩进字符

    bool isHeading() const { return fontSizeOverride > 0; }
};

//=============================================================================
// 背景图片网格校准（NxM 锚点，支持弯曲纸面）
//=============================================================================
struct BackgroundCalibration {
    bool enabled = false;
    int rows = 3;                    // 网格行数
    int cols = 3;                    // 网格列数
    std::vector<QPointF> gridPoints; // rows*cols 个点，行主序，图片坐标空间
    
    // ⚠️ 注意：这个判断**包含 enabled**。构造时请先把 enabled 置真再调用校验，
    // 否则会得到「刚填好网格却判定无效」的结果（CLI 加载预设时踩过）。
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
    LineGuideSet lineGuides;              // 背景图横线导引（作业本横线）

    // --- 字体 ---
    std::string fontPath;
    int fontSize = 30;
    std::vector<std::string> fontMixList; // 混合字体列表（随机切换）
    double fontMixRate = 0.2;             // 混合字体出现概率 (0~1)

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
    double textWarpStrength = 1.0;                            // 变形强度倍率

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
    bool preserveChinesePunctuation = true;  // 保留中文标点（false = 转成 ASCII）

    // --- 字符 ---
    // 行尾禁则 / 行首禁则字符集。同时含全角与 ASCII 形式，
    // 因为 preserveChinesePunctuation=false 时标点会被替换成 ASCII 等价字符
    std::string startChars = "\"'（([【<“‘";
    std::string endChars = "。，、；：！？,.!?;:)]}）】>”’";

    // --- 颜色 ---
    Color fillColor = Color(0, 0, 0, 255);
    Color backgroundColor = Color(0, 0, 0, 0);

    // --- 复现 ---
    // 0 = 每次随机；非 0 = 固定种子，保证预览与导出结果完全一致
    unsigned int seed = 0;

    // --- 覆盖 ---
    std::vector<CharacterOverrideRange> charOverrides;
};

//=============================================================================
// 单页渲染数据
//=============================================================================
// 一行待渲染内容
struct RenderLine {
    QString text;
    QFont font;
    bool separator = false;   // true = Markdown 分割线，渲染为水平线而非文字
};

struct PageRenderData {
    int pageIndex;
    std::vector<RenderLine> lines;
    int scaledWidth;
    int scaledHeight;
    TemplateParams params;
    std::mt19937 rng;

    std::vector<std::vector<int>> charIndexMap;
    std::vector<qreal> lineYPositions;
    
    // 段落信息：lineIdx -> paragraph index，用于段间距
    std::vector<int> lineParagraphIndex;

    // 横线导引：已从「原图坐标」换算到画布空间（乘 sx/sy）
    // lineGuideIdx[i] = 第 i 行使用的曲线下标，-1 = 该行不跟随曲线
    std::vector<GuideCurve> guideCurves;
    std::vector<int> lineGuideIdx;
    // 每行的水平缩放（斜拍补偿）：斜向拍摄时纸是梯形，同样多的字在窄的一侧
    // 应占更窄的宽度。由锚点四角双线性插值得到；无锚点或正拍时为 1.0
    std::vector<qreal> lineScaleX;
    // 每行文字块左边界在画布上的实际 x（同样由锚点插值而来）。
    // 缩放要围绕它进行 —— 否则行首仍固定在页面标称位置，会画到纸外面去
    std::vector<qreal> lineOriginX;

    // 已按页面尺寸解码并缩放好的背景图（所有页共享同一份，QImage 隐式共享零拷贝）
    QImage backgroundImage;
    // 背景原图尺寸（校准锚点换算需要）
    QSize backgroundSourceSize;
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
    void setFont(const std::string& path, int size);

    // --- 纯文本工具（公开以便单元测试） ---
    // 中文标点 → ASCII 等价字符
    static QString convertChinesePunctuation(const QString& text);
    // Markdown 轻标记解析
    static std::vector<StyledSpan> parseMarkdown(const QString& text);

    // --- 文本布局（公开以便单元测试） ---
    // 返回按行切分结果，含每行字号覆盖与「渲染索引 → 原文索引」映射
    std::vector<LineLayout> layoutText(const QString& text, const QFont& font,
                                       int maxLineWidth, int scaledLineSpacing,
                                       int scaledWordSpacing);

    // --- 字符覆盖序列化（GUI 与 CLI 共用，避免重复实现） ---
    // 格式: start,end,fontSize,perturbX,perturbY,perturbTheta,r,g,b,a（空字段 = 未设置）
    static std::string serializeCharOverride(const CharacterOverrideRange& r);
    // 解析失败或全部字段为空时返回 nullopt
    static std::optional<CharacterOverrideRange> deserializeCharOverride(const std::string& s);

    // --- 内存预算（避免高倍率 OOM） ---
    static constexpr long long MAX_SINGLE_PAGE_BYTES = 1024LL * 1024 * 1024;   // 单页 1 GB
    static constexpr long long MAX_TOTAL_BYTES       = 2048LL * 1024 * 1024;   // 峰值 2 GB
    // 单页峰值字节估算（主画布 + 校准图层 + 背景 + 洇染缓冲 + 竖排旋转）
    static long long estimateSinglePageBytes(const TemplateParams& params);
    // 按内存预算收窄线程数
    static int clampThreadsForBudget(const TemplateParams& params, int requested,
                                     int pageCount, bool holdAllPages);
    // 返回 false 表示必然 OOM，message 说明原因与建议
    static bool checkRenderBudget(const TemplateParams& params, int threadCount, int pageCount,
                                  bool holdAllPages, std::string* message = nullptr);

    // --- 字体可用性 ---
    // 返回 false 表示字体文件无法加载（message 给出路径）
    static bool checkFontAvailable(const std::string& fontPath, std::string* message = nullptr);

    // 生成
    std::vector<QImage> generatePreview(const std::string& text);
    std::vector<QImage> generatePreviewParallel(const std::string& text, int threadCount = 0);
    std::map<int, std::string> generateImageParallel(const std::string& text,
                                                      const std::string& outputDir = "outputs",
                                                      int threadCount = 0,
                                                      std::function<void(int, int)> progressCallback = nullptr,
                                                      std::function<bool()> cancelCallback = nullptr);
    
    // PDF 导出
    bool exportPdf(const std::string& text, const std::string& pdfPath);
    
    // SVG 导出（多页：首页写 svgPath，其余写 svgPath-2.svg、-3.svg …）
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

    bool isStartChar(QChar c) const;
    bool isEndChar(QChar c) const;

    // 纸张纹理绘制
    static void drawPaperTexture(QPainter& painter, int width, int height,
                                  PaperTexture texture, int rate, double opacity);

    // 渲染辅助
    // warp* 参数用于逐字变形：warpPhaseBase 为行首相位，warpAmplitude/Wavelength 定义波形
    static void drawTextWithPerturbationStatic(QPainter& painter, const QString& text,
                                                qreal& x, qreal& y, const QFont& baseFont,
                                                int scaledWordSpacing, const TemplateParams& params,
                                                std::mt19937& rng,
                                                int startCharIndex = 0,
                                                const std::vector<int>* charIndexMap = nullptr,
                                                TextWarp warp = TextWarp::None,
                                                qreal warpPhaseBase = 0.0,
                                                qreal warpAmplitude = 0.0,
                                                qreal warpWavelength = 0.0,
                                                // 横线导引：非空时逐字沿曲线定位（画布空间坐标）
                                                const GuideCurve* guideCurve = nullptr,
                                                bool followCurve = true);
    
    // 墨水洇染效果
    static void applyInkBleed(QImage& image, double radius);

    // 混合字体选择（使用页面级 rng，保证可复现）
    static QFont pickMixedFont(const QFont& baseFont,
                               const std::vector<std::string>& fontMixList,
                               double mixRate, std::mt19937& rng);

    // 竖排：旋转 90° 后居中裁切到目标尺寸
    static QImage rotateToVertical(const QImage& src, int targetWidth, int targetHeight);

    // 并行渲染（保留全部页面）
    static std::vector<QImage> renderPagesParallel(const std::vector<PageRenderData>& pages,
                                                   int threadCount,
                                                   const std::function<void(int, int)>& onProgress,
                                                   const std::function<bool()>& isCanceled);
    // 并行渲染并直接落盘（逐页释放，峰值 = 线程数 × 单页）
    static std::map<int, std::string> renderAndSaveParallel(const std::vector<PageRenderData>& pages,
                                                            const std::string& outputDir,
                                                            int threadCount,
                                                            const std::function<void(int, int)>& onProgress,
                                                            const std::function<bool()>& isCanceled);
};

} // namespace HandWrite

#endif // CORE_HPP
