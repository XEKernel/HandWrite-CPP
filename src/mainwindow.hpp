#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include <QMainWindow>
#include <QSplitter>
#include <QDialog>
#include <QAction>
#include <QMenuBar>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QFileDialog>
#include <QMessageBox>
#include <QImage>
#include <QMouseEvent>
#include <functional>
#include <atomic>
#include <memory>
#include <QPixmap>
#include <QProgressDialog>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QWheelEvent>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QListWidget>
#include <QSlider>
#include <QVBoxLayout>
#include <map>

#include "config.hpp"
#include "tools.hpp"
#include "core.hpp"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

namespace HandWrite {

//=============================================================================
// 后台渲染结果（携带错误信息，避免用异常跨线程传递）
//=============================================================================
struct RenderOutcome {
    std::vector<QImage> images;
    QString error;
};

struct ExportOutcome {
    std::map<int, std::string> files;
    QString error;
};

class CharacterOverrideDialog : public QDialog {
    Q_OBJECT
public:
    explicit CharacterOverrideDialog(QWidget *parent = nullptr);
    ~CharacterOverrideDialog() = default;
    void setOverride(const CharacterOverride &override);
    CharacterOverride getOverride() const;
signals:
    void applied();
private slots:
    void onApplyClicked();
private:
    QCheckBox *m_fontSizeCheck; QSpinBox *m_fontSizeSpin;
    QCheckBox *m_perturbXCheck; QDoubleSpinBox *m_perturbXSpin;
    QCheckBox *m_perturbYCheck; QDoubleSpinBox *m_perturbYSpin;
    QCheckBox *m_perturbThetaCheck; QDoubleSpinBox *m_perturbThetaSpin;
    QCheckBox *m_colorCheck; QSpinBox *m_colorR, *m_colorG, *m_colorB, *m_colorA;
};

//=============================================================================
// 字体混合选择对话框
//=============================================================================
class FontMixDialog : public QDialog {
    Q_OBJECT
public:
    FontMixDialog(const std::vector<std::string>& fontNames,
                  const std::vector<std::string>& fontPaths,
                  const std::vector<std::string>& selected,
                  QWidget* parent = nullptr);
    std::vector<std::string> selectedPaths() const;
private:
    QListWidget* m_list;
    std::vector<std::string> m_fontPaths;
};

//=============================================================================
// 图片画布对话框基类
//=============================================================================
// 负责：载入背景图、等比缩放居中、widget 坐标 <-> 图片坐标换算、随窗口尺寸重算。
// CalibrationDialog（锚点校准）与 LineGuideDialog（横线导引）共用，
// 避免同一套画布逻辑出现第三份实现。
class ImageCanvasDialog : public QDialog {
    Q_OBJECT
public:
    explicit ImageCanvasDialog(QWidget* parent = nullptr);
protected:
    // 载入背景图；失败时弹提示并返回 false（调用方自行 reject）
    bool loadCanvasImage(const QString& imagePath);
    // 建画布控件并加入 root（鼠标事件穿透到对话框，命中测试统一在对话框里做）
    void installCanvas(QVBoxLayout* root, int minHeight = 240);
    // 在图片区下方画一行提示
    void drawCanvasTip(QPainter& p, const QString& tip);
    void relayoutCanvas();
    QPointF toImageCoords(const QPoint& widgetPos) const;
    QPoint toWidgetCoords(const QPointF& imgPos) const;
    bool inCanvas(const QPoint& widgetPos) const { return m_drawRect.contains(widgetPos); }
    void resizeEvent(QResizeEvent*) override;

    QImage m_image;
    QPixmap m_scaledPixmap;
    QWidget* m_canvas = nullptr;   // 图片绘制区（交给布局管理，避免绝对定位）
    QRect m_drawRect;              // 图片在对话框内的实际绘制矩形
};

//=============================================================================
// 背景图片网格校准对话框（锚点）
//=============================================================================
class CalibrationDialog : public ImageCanvasDialog {
    Q_OBJECT
public:
    explicit CalibrationDialog(const QString& imagePath, const BackgroundCalibration& calib, QWidget* parent = nullptr);
    BackgroundCalibration getCalibration() const;
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
private:
    void buildUniformGrid();
    void applyMode(bool cornerMode, int newRows = 3, int newCols = 3);

    int m_rows, m_cols;
    std::vector<QPointF> m_points;  // 行主序，图片坐标
    int m_dragIdx = -1;
    bool m_cornerMode = true;
    QPushButton *m_modeBtn = nullptr;
    QPushButton *m_btnColP = nullptr, *m_btnColM = nullptr, *m_btnRowP = nullptr, *m_btnRowM = nullptr, *m_btnReset = nullptr;
    QLabel *m_lblCol = nullptr, *m_lblRow = nullptr;
};

//=============================================================================
// 横线导引对话框（作业本横线）
//=============================================================================
// 交互流程对应实际使用顺序：先「锚点」定出页面透视，再来这里沿照片上的
// 印刷横线拖动描线。因为纸张弯曲时横线是曲线，只能手绘。
// 为降低操作成本，只要求描 2 条关键曲线（首、尾），中间按弧长参数插值生成。
class LineGuideDialog : public ImageCanvasDialog {
    Q_OBJECT
public:
    explicit LineGuideDialog(const QString& imagePath, const LineGuideSet& guides, QWidget* parent = nullptr);
    LineGuideSet getGuides() const;
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
private:
    enum class Drag { None, Curve, Endpoint };

    void refresh();                 // 由关键曲线 + 条数重算预览曲线
    void sortKeyCurves();           // 按垂直位置排序，插值才正确
    void pushUndo();
    void updateUI();
    void beginDraw(int replaceIndex);   // replaceIndex >= 0 表示重画该条
    int hitCurve(const QPoint& widgetPos, int* endpoint) const;

    std::vector<GuideCurve> m_keyCurves;   // 用户手绘的关键曲线（图片坐标）
    std::vector<GuideCurve> m_preview;     // build() 结果，仅用于显示
    std::vector<std::vector<GuideCurve>> m_undo;

    int  m_lineCount = 20;
    bool m_interpolate = true;

    // 绘制中的临时状态
    bool m_drawing = false;
    int  m_replaceIndex = -1;
    std::vector<QPointF> m_drawPts;

    // 拖拽
    Drag m_drag = Drag::None;
    int  m_selCurve = -1;
    int  m_dragEndpoint = -1;
    QPointF m_dragGrab;       // 抓取时鼠标的图片坐标
    std::vector<QPointF> m_dragSnapshot;

    QPoint m_mousePos;
    bool m_mouseIn = false;

    QSpinBox*   m_spinCount = nullptr;
    QCheckBox*  m_checkInterp = nullptr;
    QSlider*    m_sliderRatio = nullptr;
    QLabel*     m_lblRatio = nullptr;
    QSpinBox*   m_spinOffset = nullptr;
    QCheckBox*  m_checkFollow = nullptr;
    QSpinBox*   m_spinPerRow = nullptr;
    QLabel*     m_lblStatus = nullptr;
    QPushButton* m_btnDetect = nullptr;
    QPushButton* m_btnFirst = nullptr;
    QPushButton* m_btnLast = nullptr;
    QPushButton* m_btnExtra = nullptr;
    QPushButton* m_btnRedraw = nullptr;
    QPushButton* m_btnDelete = nullptr;
    QPushButton* m_btnClear = nullptr;
    QPushButton* m_btnUndo = nullptr;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
protected:
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    // 按钮
    void onPushButtonPreviewClicked();
    void onPushButtonExportClicked();
    void onPushButtonExportSvgClicked();
    void onPushButtonPrintClicked();
    void onPushButtonExportPdfClicked();
    void onPushButtonSaveConfigClicked();
    void onPushButtonLoadConfigClicked();
    void onPushButtonCharOverrideClicked();
    void onPushButtonClearOverridesClicked();
    void onPushButtonSelectBgImageClicked();
    void onPushButtonClearBgImageClicked();
    void onPushButtonCalibrateBgClicked();
    void onPushButtonLineGuideClicked();
    void onPushButtonFontMixClicked();
    void onPushButtonNewSeedClicked();
    void showAboutDialog();
    void onMenuFileNew();
    void onMenuFileOpen();
    void onMenuFileSave();
    
    // 预设
    void onPushButtonPresetSaveClicked();
    void onPushButtonPresetLoadClicked();
    void onPushButtonPresetDeleteClicked();
    
    // 分页
    void onPushButtonFirstPageClicked();
    void onPushButtonPrevPageClicked();
    void onPushButtonNextPageClicked();
    void onPushButtonLastPageClicked();
    void onSpinBoxPageValueChanged(int value);
    
    // 缩放
    void onPushButtonZoomInClicked(); void onPushButtonZoomOutClicked();
    void onPushButtonZoomResetClicked(); void onSliderZoomValueChanged(int value);
    
    // 组合框
    void onComboBoxPaperTemplateCurrentIndexChanged(int index);
    
    // 自动预览
    void onParameterChanged();
    void onTextureChanged(int index);
    void checkPendingPreview();
    void triggerAutoPreview();

private:
    Ui::MainWindow *ui;
    BasicTools m_tools;
    HandwriteGenerator m_generator;
    
    std::map<int, std::string> m_previewImagePaths;
    std::vector<QImage> m_previewImages;
    int m_currentPage = 0, m_totalPages = 0;
    
    QGraphicsScene *m_scene;
    QGraphicsPixmapItem *m_pixmapItem;
    
    double m_zoomFactor = 1.0;
    static constexpr double ZOOM_MIN = 0.1, ZOOM_MAX = 5.0, ZOOM_STEP = 0.1;
    
    QFutureWatcher<RenderOutcome> *m_previewWatcher;
    QFutureWatcher<ExportOutcome> *m_exportWatcher;
    QProgressDialog *m_progressDialog;
    // 主线程与渲染线程共享的取消标志（true = 用户已取消）
    std::shared_ptr<std::atomic<bool>> m_cancelFlag;
    
    std::vector<CharacterOverrideRange> m_charOverrides;
    std::vector<std::string> m_cachedFontNames, m_cachedFontPaths;
    std::vector<std::string> m_fontMixPaths;
    
    // 自动预览
    QTimer *m_autoPreviewTimer;
    bool m_previewPending = false;  // 有新的预览请求等待当前渲染完成
    
    // 动态创建的控件
    QComboBox *m_comboTexture;
    QDoubleSpinBox *m_spinTextureOpacity;
    QCheckBox *m_checkParagraphIndent;
    QComboBox *m_comboTextDirection;
    QComboBox *m_comboTextWarp;
    QSpinBox *m_spinParagraphSpacing;
    QCheckBox *m_checkInkBleed;
    QDoubleSpinBox *m_spinInkBleedRadius;
    QDoubleSpinBox *m_spinStrikeThroughRate;
    QDoubleSpinBox *m_spinStrokeWidthSigma;
    QCheckBox *m_checkPreservePunct;
    QDoubleSpinBox *m_spinTextWarpStrength;
    QDoubleSpinBox *m_spinFontMixRate;
    QLabel *m_labelFontMix;
    QLineEdit *m_lineEditSeed;
    QLabel *m_labelBgImage;
    BackgroundCalibration m_bgCalibration;
    // 横线导引：批次 1 还没有编辑 UI，只做「预设加载 → 保存」的原样保留，
    // 避免 GUI 保存预设时把它丢掉（v2.7.0 刚修过这类「预设不完整」问题）
    LineGuideSet m_lineGuides;
    QString m_bgImagePath;
    
    QListWidget *m_presetList;
    
    void setupDefaults();
    void setupConnections();
    void populateComboBoxes();
    void setupDynamicUi();
    void showImage(const QString &imagePath);
    void showImage(const QImage &image);
    void updatePreview();
    void updatePaginationUI(); void updatePageButtons();
    void goToPage(int page);
    void updateZoomDisplay(); void applyZoom();
    void updateCharOverrideLabel();
    void updateFontMixLabel();
    void requestCancel();
    
    RenderOutcome generatePreviewAsync(TemplateParams params, QString text, int previewRate);
    ExportOutcome generateExportAsync(TemplateParams params, QString text, QString outputDir);
    void onPreviewFinished(); void onExportFinished();
    void setupProgressDialog(const QString &title, int maximum = 0);
    
    TemplateParams getParamsFromForm();
    QString getTextFromTextEdit();
    // 渲染前检查「单页」内存是否超硬上限；超限时提示用户并返回 false
    // （页数相关的峰值检查在引擎内部完成，错误通过 RenderOutcome::error 回传）
    bool guardRenderBudget(const TemplateParams& params);
    
    void saveConfiguration(const QString &path);
    void loadConfiguration(const QString &path);
    
    // 预设管理
    void refreshPresetList();
    QString presetDir() const;
};

} // namespace HandWrite
#endif // MAINWINDOW_HPP
