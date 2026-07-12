#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include <QMainWindow>
#include <QSplitter>
#include <QDialog>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QFileDialog>
#include <QMessageBox>
#include <QImage>
#include <QMouseEvent>
#include <functional>
#include <QPixmap>
#include <QProgressDialog>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QWheelEvent>
#include <QDialog>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QListWidget>
#include <map>

#include "config.hpp"
#include "tools.hpp"
#include "core.hpp"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

namespace HandWrite {

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
// 背景图片锚点校准对话框
//=============================================================================
class CalibrationDialog : public QDialog {
    Q_OBJECT
public:
    explicit CalibrationDialog(const QString& imagePath, const BackgroundCalibration& calib, QWidget* parent = nullptr);
    BackgroundCalibration getCalibration() const;
private:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    QPointF toImageCoords(const QPoint& widgetPos) const;
    QPoint toWidgetCoords(const QPointF& imgPos) const;
    QImage m_image;
    QPixmap m_scaledPixmap;
    QRectF m_imageRect;
    QPointF m_points[4];
    int m_currentPoint = 0;
    int m_dragPoint = -1;
    std::function<void()> m_pointCompleteCallback;
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

private slots:
    // 按钮
    void onPushButtonPreviewClicked();
    void onPushButtonExportClicked();
    void onPushButtonPrintClicked();
    void onPushButtonExportPdfClicked();
    void onPushButtonSaveConfigClicked();
    void onPushButtonLoadConfigClicked();
    void onPushButtonCharOverrideClicked();
    void onPushButtonClearOverridesClicked();
    void onPushButtonSelectBgImageClicked();
    void onPushButtonClearBgImageClicked();
    void onPushButtonCalibrateBgClicked();
    
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
    
    QFutureWatcher<std::vector<QImage>> *m_previewWatcher;
    QFutureWatcher<std::map<int, std::string>> *m_exportWatcher;
    QProgressDialog *m_progressDialog;
    int m_exportRate = 4;
    
    std::vector<CharacterOverrideRange> m_charOverrides;
    std::vector<std::string> m_cachedFontNames, m_cachedFontPaths;
    
    // 自动预览
    QTimer *m_autoPreviewTimer;
    
    // 动态创建的控件
    QComboBox *m_comboTexture;
    QDoubleSpinBox *m_spinTextureOpacity;
    QCheckBox *m_checkParagraphIndent;
    QSpinBox *m_spinParagraphSpacing;
    QCheckBox *m_checkInkBleed;
    QDoubleSpinBox *m_spinInkBleedRadius;
    QDoubleSpinBox *m_spinStrikeThroughRate;
    QDoubleSpinBox *m_spinStrokeWidthSigma;
    QLabel *m_labelBgImage;
    BackgroundCalibration m_bgCalibration;
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
    
    std::vector<QImage> generatePreviewAsync(TemplateParams params, QString text, int previewRate);
    std::map<int, std::string> generateExportAsync(TemplateParams params, QString text, QString outputDir);
    void onPreviewFinished(); void onExportFinished();
    void setupProgressDialog(const QString &title, int maximum = 0);
    
    TemplateParams getParamsFromForm();
    QString getTextFromTextEdit();
    
    void saveConfiguration(const QString &path);
    void loadConfiguration(const QString &path);
    
    // 预设管理
    void refreshPresetList();
    QString presetDir() const;
};

} // namespace HandWrite
#endif // MAINWINDOW_HPP
