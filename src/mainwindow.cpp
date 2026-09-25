#include "mainwindow.hpp"
#include "ui_mainwindow.h"
#include <QDir>
#include <QFile>
#include <QDebug>
#include <QScrollBar>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QThread>
#include <QDesktopServices>
#include <QUrl>
#include <QClipboard>
#include <QMimeData>
#include <QApplication>
#include <QGuiApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QAbstractButton>
#include <QPrinter>
#include <QPrintDialog>
#include <QImageReader>
#include <QIntValidator>
#include <QDoubleValidator>
#include <QGroupBox>
#include <QGridLayout>
#include <QInputDialog>
#include <QFileInfo>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QResizeEvent>
#include <QLineF>
#include <algorithm>
#include <sstream>
#include <optional>

namespace HandWrite {

namespace {

// 生成 1..2e9 范围内的随机种子（留在 int 正区间内，便于以整数写入配置）
unsigned int makeRandomSeed() {
    return static_cast<unsigned int>(QRandomGenerator::global()->bounded(1, 2000000000));
}

} // namespace

//=============================================================================
// CharacterOverrideDialog
//=============================================================================

CharacterOverrideDialog::CharacterOverrideDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("设置字符属性")); setModal(false);
    auto *mainLayout = new QVBoxLayout(this);
    auto *formLayout = new QFormLayout();
    
    auto mkLayout = [&](auto*& check, auto*& spin, const QString& label, double min, double max, double val, int decimals) {
        using T = std::remove_pointer_t<std::remove_reference_t<decltype(spin)>>;
        auto *lay = new QHBoxLayout(); check = new QCheckBox(tr("覆盖"), this);
        spin = new T(this); spin->setRange(min, max); spin->setValue(val);
        if constexpr (std::is_same_v<T, QDoubleSpinBox>) spin->setDecimals(decimals);
        spin->setEnabled(false); lay->addWidget(check); lay->addWidget(spin); lay->addStretch();
        formLayout->addRow(label, lay);
        connect(check, &QCheckBox::toggled, spin, &QWidget::setEnabled);
    };
    
    auto *fsLay = new QHBoxLayout(); m_fontSizeCheck = new QCheckBox(tr("覆盖"), this);
    m_fontSizeSpin = new QSpinBox(this);
    m_fontSizeSpin->setRange(1, 200); m_fontSizeSpin->setValue(30); m_fontSizeSpin->setEnabled(false);
    fsLay->addWidget(m_fontSizeCheck); fsLay->addWidget(m_fontSizeSpin); fsLay->addStretch();
    formLayout->addRow(tr("字体大小:"), fsLay);
    connect(m_fontSizeCheck, &QCheckBox::toggled, m_fontSizeSpin, &QSpinBox::setEnabled);
    
    mkLayout(m_perturbXCheck, m_perturbXSpin, tr("横向偏移:"), -100, 100, 0, 1);
    mkLayout(m_perturbYCheck, m_perturbYSpin, tr("纵向偏移:"), -100, 100, 0, 1);
    mkLayout(m_perturbThetaCheck, m_perturbThetaSpin, tr("旋转角度:"), -180, 180, 0, 2);
    m_perturbThetaSpin->setSuffix(tr("°"));
    
    auto *colorLayout = new QHBoxLayout();
    m_colorCheck = new QCheckBox(tr("覆盖"), this);
    m_colorR = new QSpinBox(this); m_colorR->setRange(0,255); m_colorR->setValue(0); m_colorR->setEnabled(false);
    m_colorG = new QSpinBox(this); m_colorG->setRange(0,255); m_colorG->setValue(0); m_colorG->setEnabled(false);
    m_colorB = new QSpinBox(this); m_colorB->setRange(0,255); m_colorB->setValue(0); m_colorB->setEnabled(false);
    m_colorA = new QSpinBox(this); m_colorA->setRange(0,255); m_colorA->setValue(255); m_colorA->setEnabled(false);
    colorLayout->addWidget(m_colorCheck);
    colorLayout->addWidget(new QLabel("R:",this)); colorLayout->addWidget(m_colorR);
    colorLayout->addWidget(new QLabel("G:",this)); colorLayout->addWidget(m_colorG);
    colorLayout->addWidget(new QLabel("B:",this)); colorLayout->addWidget(m_colorB);
    colorLayout->addWidget(new QLabel("A:",this)); colorLayout->addWidget(m_colorA);
    formLayout->addRow(tr("字体颜色:"), colorLayout);
    connect(m_colorCheck, &QCheckBox::toggled, m_colorR, &QSpinBox::setEnabled);
    connect(m_colorCheck, &QCheckBox::toggled, m_colorG, &QSpinBox::setEnabled);
    connect(m_colorCheck, &QCheckBox::toggled, m_colorB, &QSpinBox::setEnabled);
    connect(m_colorCheck, &QCheckBox::toggled, m_colorA, &QSpinBox::setEnabled);
    
    mainLayout->addLayout(formLayout);
    auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Apply|QDialogButtonBox::Ok|QDialogButtonBox::Cancel, this);
    connect(btnBox, &QDialogButtonBox::clicked, this, [this,btnBox](QAbstractButton* b){
        auto r = btnBox->buttonRole(b);
        if(r==QDialogButtonBox::ApplyRole){onApplyClicked();}
        else if(r==QDialogButtonBox::AcceptRole){onApplyClicked();accept();}
        else if(r==QDialogButtonBox::RejectRole){reject();}
    });
    mainLayout->addWidget(btnBox);
}
void CharacterOverrideDialog::onApplyClicked() { emit applied(); }
void CharacterOverrideDialog::setOverride(const CharacterOverride& o) {
    if(o.fontSize){m_fontSizeCheck->setChecked(true);m_fontSizeSpin->setValue(*o.fontSize);}
    if(o.perturbX){m_perturbXCheck->setChecked(true);m_perturbXSpin->setValue(*o.perturbX);}
    if(o.perturbY){m_perturbYCheck->setChecked(true);m_perturbYSpin->setValue(*o.perturbY);}
    if(o.perturbTheta){m_perturbThetaCheck->setChecked(true);m_perturbThetaSpin->setValue(*o.perturbTheta*180.0/M_PI);}
    if(o.fillColor){m_colorCheck->setChecked(true);m_colorR->setValue(o.fillColor->r);m_colorG->setValue(o.fillColor->g);m_colorB->setValue(o.fillColor->b);m_colorA->setValue(o.fillColor->a);}
}
CharacterOverride CharacterOverrideDialog::getOverride() const {
    CharacterOverride o;
    if(m_fontSizeCheck->isChecked())o.fontSize=m_fontSizeSpin->value();
    if(m_perturbXCheck->isChecked())o.perturbX=m_perturbXSpin->value();
    if(m_perturbYCheck->isChecked())o.perturbY=m_perturbYSpin->value();
    if(m_perturbThetaCheck->isChecked())o.perturbTheta=m_perturbThetaSpin->value()*M_PI/180.0;
    if(m_colorCheck->isChecked())o.fillColor=Color(m_colorR->value(),m_colorG->value(),m_colorB->value(),m_colorA->value());
    return o;
}

//=============================================================================
// MainWindow
//=============================================================================

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), m_scene(new QGraphicsScene(this)),
      m_pixmapItem(nullptr), m_previewWatcher(new QFutureWatcher<RenderOutcome>(this)),
      m_exportWatcher(new QFutureWatcher<ExportOutcome>(this)), m_progressDialog(nullptr),
      m_cancelFlag(std::make_shared<std::atomic<bool>>(false)) {
    ui->setupUi(this);
    
    // 菜单栏
    auto* fileMenu = menuBar()->addMenu(tr("文件(&F)"));
    auto* actNew = fileMenu->addAction(tr("新建(&N)"), this, &MainWindow::onMenuFileNew);
    actNew->setShortcut(QKeySequence::New);
    // 注意：Qt 6 推荐 addAction(text, shortcut, object, slot) 的参数顺序，
    // 旧的 addAction(text, object, slot, shortcut) 已标记废弃（-Wdeprecated）
    fileMenu->addAction(tr("打开文本(&O)..."), QKeySequence::Open, this, &MainWindow::onMenuFileOpen);
    fileMenu->addAction(tr("保存文本(&S)"), QKeySequence::Save, this, &MainWindow::onMenuFileSave);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("退出(&X)"), QKeySequence::Quit, this, &MainWindow::close);
    
    auto* editMenu = menuBar()->addMenu(tr("编辑(&E)"));
    editMenu->addAction(tr("撤销(&U)"), QKeySequence::Undo, ui->textEditMain, &QTextEdit::undo);
    editMenu->addAction(tr("重做(&R)"), QKeySequence::Redo, ui->textEditMain, &QTextEdit::redo);
    
    auto* presetMenu = menuBar()->addMenu(tr("导出(&P)"));
    presetMenu->addAction(tr("导出 PNG..."), QKeySequence("Ctrl+E"), this, &MainWindow::onPushButtonExportClicked);
    presetMenu->addAction(tr("导出 PDF..."), QKeySequence("Ctrl+P"), this, &MainWindow::onPushButtonExportPdfClicked);
    presetMenu->addAction(tr("导出 SVG..."), this, &MainWindow::onPushButtonExportSvgClicked);
    presetMenu->addSeparator();
    presetMenu->addAction(tr("保存预设..."), QKeySequence("Ctrl+Shift+S"), this, &MainWindow::onPushButtonSaveConfigClicked);
    presetMenu->addAction(tr("加载预设..."), QKeySequence("Ctrl+Shift+O"), this, &MainWindow::onPushButtonLoadConfigClicked);
    
    auto* helpMenu = menuBar()->addMenu(tr("帮助(&H)"));
    helpMenu->addAction(tr("关于(&A)"), this, &MainWindow::showAboutDialog);
    
    ui->imgPreview->setScene(m_scene);
    ui->imgPreview->setDragMode(QGraphicsView::ScrollHandDrag);
    ui->imgPreview->setRenderHint(QPainter::Antialiasing);
    ui->imgPreview->setRenderHint(QPainter::SmoothPixmapTransform);
    ui->imgPreview->installEventFilter(this);
    ui->textEditMain->installEventFilter(this);
    ui->textEditMain->setUndoRedoEnabled(true);
    setAcceptDrops(true);
    
    setupConnections();
    
    // 提前创建 timer，避免 populateComboBoxes 中的信号触发空指针
    m_autoPreviewTimer = new QTimer(this);
    m_autoPreviewTimer->setSingleShot(true);
    m_autoPreviewTimer->setInterval(500);
    connect(m_autoPreviewTimer, &QTimer::timeout, this, &MainWindow::triggerAutoPreview);
    
    setupDefaults();
    populateComboBoxes();
    setupDynamicUi();
    
    // QSplitter: 左侧预览区与右侧设置栏可拖拽调节
    {
        auto* mainLayout = ui->horizontalLayout_main;
        auto* displayLayout = mainLayout->takeAt(0)->layout();
        auto* scrollItem = mainLayout->takeAt(0);
        auto* scrollWidget = scrollItem->widget();
        delete scrollItem;
        
        auto* leftContainer = new QWidget(this);
        leftContainer->setLayout(displayLayout);
        
        auto* splitter = new QSplitter(Qt::Horizontal, this);
        splitter->setHandleWidth(5);
        splitter->setChildrenCollapsible(false);
        splitter->addWidget(leftContainer);
        splitter->addWidget(scrollWidget);
        splitter->setSizes({750, 320});
        
        mainLayout->addWidget(splitter);
    }
    
    connect(m_previewWatcher, &QFutureWatcher<RenderOutcome>::finished, this, &MainWindow::onPreviewFinished);
    connect(m_exportWatcher, &QFutureWatcher<ExportOutcome>::finished, this, &MainWindow::onExportFinished);
    
    // 监听所有参数变化
    auto watchLineEdit = [this](QLineEdit* e){ connect(e,&QLineEdit::textChanged,this,&MainWindow::onParameterChanged); };
    watchLineEdit(ui->lineEditWidth); watchLineEdit(ui->lineEditHeight);
    watchLineEdit(ui->lineEditFontSize); watchLineEdit(ui->lineEditLineSpacing);
    watchLineEdit(ui->lineEditCharDistance);
    watchLineEdit(ui->lineEditMarginTop); watchLineEdit(ui->lineEditMarginBottom);
    watchLineEdit(ui->lineEditMarginLeft); watchLineEdit(ui->lineEditMarginRight);
    watchLineEdit(ui->lineEditLineSpacingSigma); watchLineEdit(ui->lineEditFontSizeSigma);
    watchLineEdit(ui->lineEditWordSpacingSigma); watchLineEdit(ui->lineEditPerturbXSigma);
    watchLineEdit(ui->lineEditPerturbYSigma); watchLineEdit(ui->lineEditPerturbThetaSigma);
    connect(ui->comboBoxFont, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onParameterChanged);
    connect(ui->comboBoxCharColor, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onParameterChanged);
    connect(ui->comboBoxBackgroundColor, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onParameterChanged);
    connect(ui->comboBoxResolution, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onParameterChanged);
    connect(ui->textEditMain, &QTextEdit::textChanged, this, &MainWindow::onParameterChanged);
    
    updatePreview();
}

MainWindow::~MainWindow() {
    if(m_previewWatcher&&m_previewWatcher->isRunning()){m_previewWatcher->cancel();m_previewWatcher->waitForFinished();}
    if(m_exportWatcher&&m_exportWatcher->isRunning()){m_exportWatcher->cancel();m_exportWatcher->waitForFinished();}
    QCoreApplication::processEvents();
    delete ui;
}

//=============================================================================
// 动态UI创建
//=============================================================================

void MainWindow::setupDynamicUi() {
    auto *scrollContent = ui->scrollAreaWidgetContents;
    auto *infoLayout = qobject_cast<QVBoxLayout*>(scrollContent->layout());
    if(!infoLayout) return;
    
    // 在纸张设置后面插入纹理选择
    auto *textureGroup = new QGroupBox(tr("纸张纹理"), scrollContent);
    auto *textureLayout = new QHBoxLayout(textureGroup);
    m_comboTexture = new QComboBox(textureGroup);
    for(const auto& t : m_tools.paperTextures()) m_comboTexture->addItem(QString::fromStdString(t.displayName));
    connect(m_comboTexture, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onTextureChanged);
    textureLayout->addWidget(new QLabel(tr("类型:"), textureGroup));
    textureLayout->addWidget(m_comboTexture);
    m_spinTextureOpacity = new QDoubleSpinBox(textureGroup);
    m_spinTextureOpacity->setRange(0,1); m_spinTextureOpacity->setSingleStep(0.05); m_spinTextureOpacity->setValue(0.3);
    m_spinTextureOpacity->setDecimals(2);
    connect(m_spinTextureOpacity, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onParameterChanged);
    textureLayout->addWidget(new QLabel(tr("透明度:"), textureGroup));
    textureLayout->addWidget(m_spinTextureOpacity);
    
    // 背景图片
    auto *bgGroup = new QGroupBox(tr("背景图片"), scrollContent);
    auto *bgLayout = new QHBoxLayout(bgGroup);
    auto *btnSelectBg = new QPushButton(tr("选择..."), bgGroup);
    connect(btnSelectBg, &QPushButton::clicked, this, &MainWindow::onPushButtonSelectBgImageClicked);
    auto *btnClearBg = new QPushButton(tr("清除"), bgGroup);
    connect(btnClearBg, &QPushButton::clicked, this, &MainWindow::onPushButtonClearBgImageClicked);
    auto *btnCalibrateBg = new QPushButton(tr("锚点"), bgGroup);
    btnCalibrateBg->setToolTip(tr("设置文字在背景图片上的显示区域"));
    connect(btnCalibrateBg, &QPushButton::clicked, this, &MainWindow::onPushButtonCalibrateBgClicked);
    auto *btnLineGuide = new QPushButton(tr("横线..."), bgGroup);
    btnLineGuide->setToolTip(tr("沿作业本上的印刷横线描线，让文字排布在横线内\n"
                                "（纸张弯曲时横线是弯的，需先标定锚点再描线）"));
    connect(btnLineGuide, &QPushButton::clicked, this, &MainWindow::onPushButtonLineGuideClicked);
    m_labelBgImage = new QLabel(tr("未设置"), bgGroup);
    m_labelBgImage->setWordWrap(true);
    bgLayout->addWidget(btnSelectBg);
    bgLayout->addWidget(btnClearBg);
    bgLayout->addWidget(btnCalibrateBg);
    bgLayout->addWidget(btnLineGuide);
    bgLayout->addWidget(m_labelBgImage, 1);
    
    // 排版设置
    auto *paraGroup = new QGroupBox(tr("段落排版"), scrollContent);
    auto *paraLayout = new QVBoxLayout(paraGroup);
    m_checkParagraphIndent = new QCheckBox(tr("段首缩进两字符"), paraGroup);
    m_checkParagraphIndent->setChecked(true);
    connect(m_checkParagraphIndent, &QCheckBox::toggled, this, &MainWindow::onParameterChanged);
    auto *psLayout = new QHBoxLayout();
    psLayout->addWidget(new QLabel(tr("段间距:"), paraGroup));
    m_spinParagraphSpacing = new QSpinBox(paraGroup);
    m_spinParagraphSpacing->setRange(0, 200); m_spinParagraphSpacing->setValue(0);
    m_spinParagraphSpacing->setSuffix(tr(" px"));
    connect(m_spinParagraphSpacing, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onParameterChanged);
    psLayout->addWidget(m_spinParagraphSpacing); psLayout->addStretch();
    paraLayout->addWidget(m_checkParagraphIndent);
    paraLayout->addLayout(psLayout);
    
    // 文字方向
    auto *dirLayout = new QHBoxLayout();
    dirLayout->addWidget(new QLabel(tr("文字方向:"), paraGroup));
    auto *comboDir = new QComboBox(paraGroup);
    comboDir->addItem(tr("横排"), 0);
    comboDir->addItem(tr("竖排"), 1);
    connect(comboDir, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onParameterChanged);
    m_comboTextDirection = comboDir;
    dirLayout->addWidget(comboDir); dirLayout->addStretch();
    paraLayout->addLayout(dirLayout);
    
    // 文字变形模板
    auto *warpLayout = new QHBoxLayout();
    warpLayout->addWidget(new QLabel(tr("文字变形:"), paraGroup));
    auto *comboWarp = new QComboBox(paraGroup);
    comboWarp->addItem(tr("无"), 0);
    comboWarp->addItem(tr("弧形"), 1);
    comboWarp->addItem(tr("波浪"), 2);
    comboWarp->addItem(tr("圆形"), 3);
    connect(comboWarp, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onParameterChanged);
    m_comboTextWarp = comboWarp;
    warpLayout->addWidget(comboWarp); warpLayout->addStretch();
    paraLayout->addLayout(warpLayout);
    
    // 变形强度
    auto *wsLayout = new QHBoxLayout();
    wsLayout->addWidget(new QLabel(tr("变形强度:"), paraGroup));
    m_spinTextWarpStrength = new QDoubleSpinBox(paraGroup);
    m_spinTextWarpStrength->setRange(0.0, 3.0);
    m_spinTextWarpStrength->setSingleStep(0.1);
    m_spinTextWarpStrength->setDecimals(1);
    m_spinTextWarpStrength->setValue(1.0);
    connect(m_spinTextWarpStrength, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MainWindow::onParameterChanged);
    wsLayout->addWidget(m_spinTextWarpStrength); wsLayout->addStretch();
    paraLayout->addLayout(wsLayout);
    
    // 中文标点开关（此前该参数存在但从未在界面上暴露，
    // 导致 。，！？ 等标点被静默替换成 ASCII 等价字符）
    m_checkPreservePunct = new QCheckBox(tr("保留中文标点（。，！？）"), paraGroup);
    m_checkPreservePunct->setChecked(true);
    m_checkPreservePunct->setToolTip(tr("关闭后中文标点会替换为 . , ! ? 等 ASCII 字符，"
                                        "适合字库里没有中文标点的字体"));
    connect(m_checkPreservePunct, &QCheckBox::toggled, this, &MainWindow::onParameterChanged);
    paraLayout->addWidget(m_checkPreservePunct);
    
    // 笔触效果
    auto *effectGroup = new QGroupBox(tr("笔触效果"), scrollContent);
    auto *effectLayout = new QVBoxLayout(effectGroup);
    m_checkInkBleed = new QCheckBox(tr("墨水洇染"), effectGroup);
    connect(m_checkInkBleed, &QCheckBox::toggled, this, &MainWindow::onParameterChanged);
    auto *ibLayout = new QHBoxLayout();
    ibLayout->addWidget(new QLabel(tr("洇染半径:"), effectGroup));
    m_spinInkBleedRadius = new QDoubleSpinBox(effectGroup);
    m_spinInkBleedRadius->setRange(0, 5); m_spinInkBleedRadius->setValue(1.5);
    m_spinInkBleedRadius->setSingleStep(0.5); m_spinInkBleedRadius->setDecimals(1);
    connect(m_spinInkBleedRadius, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onParameterChanged);
    ibLayout->addWidget(m_spinInkBleedRadius); ibLayout->addStretch();
    effectLayout->addWidget(m_checkInkBleed);
    effectLayout->addLayout(ibLayout);
    
    // 涂改模拟
    auto *stLayout = new QHBoxLayout();
    stLayout->addWidget(new QLabel(tr("涂改概率:"), effectGroup));
    m_spinStrikeThroughRate = new QDoubleSpinBox(effectGroup);
    m_spinStrikeThroughRate->setRange(0, 0.5); m_spinStrikeThroughRate->setValue(0);
    m_spinStrikeThroughRate->setSingleStep(0.01); m_spinStrikeThroughRate->setDecimals(2);
    m_spinStrikeThroughRate->setSuffix(tr(" (0=关闭)"));
    connect(m_spinStrikeThroughRate, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onParameterChanged);
    stLayout->addWidget(m_spinStrikeThroughRate); stLayout->addStretch();
    effectLayout->addLayout(stLayout);
    
    // 笔画粗细扰动（添加到扰动组）
    auto *swLayout = new QHBoxLayout();
    swLayout->addWidget(new QLabel(tr("笔画粗细:"), effectGroup));
    m_spinStrokeWidthSigma = new QDoubleSpinBox(effectGroup);
    m_spinStrokeWidthSigma->setRange(0, 5); m_spinStrokeWidthSigma->setValue(0.3);
    m_spinStrokeWidthSigma->setSingleStep(0.1); m_spinStrokeWidthSigma->setDecimals(2);
    connect(m_spinStrokeWidthSigma, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onParameterChanged);
    swLayout->addWidget(m_spinStrokeWidthSigma); swLayout->addStretch();
    effectLayout->addLayout(swLayout);
    
    // ── 混合字体 ──
    // 此前 fontMixList 从未被任何代码填充，README 宣称的「混合字体」实际是死功能
    if (auto *fontGroup = scrollContent->findChild<QGroupBox*>("groupBoxFont")) {
        if (auto *fl = qobject_cast<QGridLayout*>(fontGroup->layout())) {
            const int row = fl->rowCount();
            auto *mixBtn = new QPushButton(tr("选择…"), fontGroup);
            connect(mixBtn, &QPushButton::clicked, this, &MainWindow::onPushButtonFontMixClicked);
            m_labelFontMix = new QLabel(tr("未启用"), fontGroup);
            m_labelFontMix->setWordWrap(true);
            auto *mixRow = new QHBoxLayout();
            mixRow->addWidget(mixBtn);
            mixRow->addWidget(m_labelFontMix, 1);
            fl->addWidget(new QLabel(tr("混合字体:"), fontGroup), row, 0);
            fl->addLayout(mixRow, row, 1, 1, 3);
            
            auto *rateRow = new QHBoxLayout();
            m_spinFontMixRate = new QDoubleSpinBox(fontGroup);
            m_spinFontMixRate->setRange(0.0, 1.0);
            m_spinFontMixRate->setSingleStep(0.05);
            m_spinFontMixRate->setDecimals(2);
            m_spinFontMixRate->setValue(0.20);
            connect(m_spinFontMixRate, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                    this, &MainWindow::onParameterChanged);
            rateRow->addWidget(m_spinFontMixRate);
            rateRow->addWidget(new QLabel(tr("（出现概率）"), fontGroup));
            rateRow->addStretch();
            fl->addWidget(new QLabel(tr("混合比例:"), fontGroup), row + 1, 0);
            fl->addLayout(rateRow, row + 1, 1, 1, 3);
        }
    }
    
    // ── 随机种子：固定后预览与导出结果一致 ──
    auto *seedGroup = new QGroupBox(tr("随机种子"), scrollContent);
    auto *seedLayout = new QHBoxLayout(seedGroup);
    m_lineEditSeed = new QLineEdit(seedGroup);
    m_lineEditSeed->setValidator(new QIntValidator(0, 2000000000, this));
    m_lineEditSeed->setToolTip(tr("相同种子 + 相同参数 = 完全相同的结果；0 表示每次随机"));
    connect(m_lineEditSeed, &QLineEdit::textChanged, this, &MainWindow::onParameterChanged);
    auto *btnNewSeed = new QPushButton(tr("重新随机"), seedGroup);
    connect(btnNewSeed, &QPushButton::clicked, this, &MainWindow::onPushButtonNewSeedClicked);
    seedLayout->addWidget(new QLabel(tr("种子:"), seedGroup));
    seedLayout->addWidget(m_lineEditSeed, 1);
    seedLayout->addWidget(btnNewSeed);
    
    // 预设管理
    auto *presetGroup = new QGroupBox(tr("预设管理"), scrollContent);
    auto *presetLayout = new QVBoxLayout(presetGroup);
    m_presetList = new QListWidget(presetGroup);
    m_presetList->setMaximumHeight(100);
    auto *presetBtnLayout = new QHBoxLayout();
    auto *btnSavePreset = new QPushButton(tr("保存"), presetGroup); connect(btnSavePreset, &QPushButton::clicked, this, &MainWindow::onPushButtonPresetSaveClicked);
    auto *btnLoadPreset = new QPushButton(tr("加载"), presetGroup); connect(btnLoadPreset, &QPushButton::clicked, this, &MainWindow::onPushButtonPresetLoadClicked);
    auto *btnDelPreset = new QPushButton(tr("删除"), presetGroup); connect(btnDelPreset, &QPushButton::clicked, this, &MainWindow::onPushButtonPresetDeleteClicked);
    presetBtnLayout->addWidget(btnSavePreset); presetBtnLayout->addWidget(btnLoadPreset);
    presetBtnLayout->addWidget(btnDelPreset); presetBtnLayout->addStretch();
    presetLayout->addWidget(m_presetList); presetLayout->addLayout(presetBtnLayout);
    
    // PDF / SVG 按钮（添加到导出布局）
    auto *pdfBtn = new QPushButton(tr("导出PDF"), scrollContent);
    connect(pdfBtn, &QPushButton::clicked, this, &MainWindow::onPushButtonExportPdfClicked);
    auto *svgBtn = new QPushButton(tr("导出SVG"), scrollContent);
    svgBtn->setToolTip(tr("矢量格式，多页文档会输出多个 .svg 文件"));
    connect(svgBtn, &QPushButton::clicked, this, &MainWindow::onPushButtonExportSvgClicked);
    
    // 插入位置：在扰动组之后、预设之前
    int perturbIdx = -1;
    for(int i=0;i<infoLayout->count();++i){
        auto* item = infoLayout->itemAt(i);
        auto* w = item ? item->widget() : nullptr;
        if(w && w->objectName()=="groupBoxPerturbation"){ perturbIdx=i; break; }
    }
    
    int insertPos = perturbIdx >= 0 ? perturbIdx + 1 : infoLayout->count() - 2;
    infoLayout->insertWidget(insertPos++, textureGroup);
    infoLayout->insertWidget(insertPos++, bgGroup);
    infoLayout->insertWidget(insertPos++, paraGroup);
    infoLayout->insertWidget(insertPos++, effectGroup);
    infoLayout->insertWidget(insertPos++, seedGroup);
    infoLayout->insertWidget(insertPos++, presetGroup);
    // PDF / SVG 按钮插入到导出按钮旁
    for(int i=0;i<infoLayout->count();++i){
        auto* item = infoLayout->itemAt(i);
        if(item && item->layout()){
            auto* hl = qobject_cast<QHBoxLayout*>(item->layout());
            if(hl && hl->objectName()=="exportLayout"){
                hl->addWidget(pdfBtn); hl->addWidget(svgBtn); break;
            }
        }
    }
    
    refreshPresetList();
    updateFontMixLabel();
    if(m_lineEditSeed && m_lineEditSeed->text().isEmpty()) onPushButtonNewSeedClicked();
}

//=============================================================================
// Setup
//=============================================================================

void MainWindow::setupConnections() {
    connect(ui->pushButtonPreview,&QPushButton::clicked,this,&MainWindow::onPushButtonPreviewClicked);
    connect(ui->pushButtonExport,&QPushButton::clicked,this,&MainWindow::onPushButtonExportClicked);
    connect(ui->pushButtonPrint,&QPushButton::clicked,this,&MainWindow::onPushButtonPrintClicked);
    connect(ui->pushButtonSaveConfig,&QPushButton::clicked,this,&MainWindow::onPushButtonSaveConfigClicked);
    connect(ui->pushButtonLoadConfig,&QPushButton::clicked,this,&MainWindow::onPushButtonLoadConfigClicked);
    connect(ui->pushButtonCharOverride,&QPushButton::clicked,this,&MainWindow::onPushButtonCharOverrideClicked);
    connect(ui->pushButtonClearOverrides,&QPushButton::clicked,this,&MainWindow::onPushButtonClearOverridesClicked);
    connect(ui->comboBoxPaperTemplate,QOverload<int>::of(&QComboBox::currentIndexChanged),this,&MainWindow::onComboBoxPaperTemplateCurrentIndexChanged);
    connect(ui->pushButtonFirstPage,&QPushButton::clicked,this,&MainWindow::onPushButtonFirstPageClicked);
    connect(ui->pushButtonPrevPage,&QPushButton::clicked,this,&MainWindow::onPushButtonPrevPageClicked);
    connect(ui->pushButtonNextPage,&QPushButton::clicked,this,&MainWindow::onPushButtonNextPageClicked);
    connect(ui->pushButtonLastPage,&QPushButton::clicked,this,&MainWindow::onPushButtonLastPageClicked);
    connect(ui->spinBoxPage,QOverload<int>::of(&QSpinBox::valueChanged),this,&MainWindow::onSpinBoxPageValueChanged);
    connect(ui->pushButtonZoomIn,&QPushButton::clicked,this,&MainWindow::onPushButtonZoomInClicked);
    connect(ui->pushButtonZoomOut,&QPushButton::clicked,this,&MainWindow::onPushButtonZoomOutClicked);
    connect(ui->pushButtonZoomReset,&QPushButton::clicked,this,&MainWindow::onPushButtonZoomResetClicked);
    connect(ui->sliderZoom,&QSlider::valueChanged,this,&MainWindow::onSliderZoomValueChanged);
    
    auto *intV = new QIntValidator(1,99999,this);
    auto *dv = new QDoubleValidator(0,1000,4,this); dv->setNotation(QDoubleValidator::StandardNotation);
    ui->lineEditWidth->setValidator(intV); ui->lineEditHeight->setValidator(intV);
    ui->lineEditFontSize->setValidator(new QIntValidator(1,500,this));
    ui->lineEditLineSpacing->setValidator(new QIntValidator(1,500,this));
    ui->lineEditCharDistance->setValidator(new QIntValidator(0,500,this));
    ui->lineEditMarginTop->setValidator(new QIntValidator(0,500,this));
    ui->lineEditMarginBottom->setValidator(new QIntValidator(0,500,this));
    ui->lineEditMarginLeft->setValidator(new QIntValidator(0,500,this));
    ui->lineEditMarginRight->setValidator(new QIntValidator(0,500,this));
    ui->lineEditLineSpacingSigma->setValidator(dv);
    ui->lineEditFontSizeSigma->setValidator(dv);
    ui->lineEditWordSpacingSigma->setValidator(dv);
    ui->lineEditPerturbXSigma->setValidator(dv);
    ui->lineEditPerturbYSigma->setValidator(dv);
    ui->lineEditPerturbThetaSigma->setValidator(new QDoubleValidator(0,100,4,this));
}

void MainWindow::setupDefaults() {
    m_zoomFactor=1.0; updateZoomDisplay();
    ui->textEditMain->setPlainText("使用 C++ 编写的手写字生成器，旨在完成一些无用的手写作业任务。本项目提供了丰富的参数设置，以满足您在生成手写字时的个性化需求。");
    m_currentPage=0; m_totalPages=0; updatePaginationUI();
}

void MainWindow::populateComboBoxes() {
    QString exeDir=QCoreApplication::applicationDirPath();
    m_tools.setTtfLibraryPath((exeDir+"/ttf_library").toStdString());
    auto [fontNames,fontPaths]=m_tools.getTtfFiles();
    m_cachedFontNames=fontNames; m_cachedFontPaths=fontPaths;
    ui->comboBoxFont->clear();
    for(size_t i=0;i<m_cachedFontNames.size();++i){
        QString label = QString::fromStdString(m_cachedFontNames[i]);
        std::string err;
        if(!HandwriteGenerator::checkFontAvailable(m_cachedFontPaths[i], &err)){
            // 字体加载失败时引擎会静默回退到系统默认字体，这里在界面上显式标注
            label += tr("（无法加载）");
            ui->comboBoxFont->addItem(label);
            ui->comboBoxFont->setItemData(static_cast<int>(i), QString::fromStdString(err), Qt::ToolTipRole);
        } else {
            ui->comboBoxFont->addItem(label);
        }
    }
    if(!m_cachedFontPaths.empty()) m_generator.setFont(m_cachedFontPaths[0],m_generator.templateParams().fontSize);
    
    ui->comboBoxCharColor->clear();
    for(const auto& [n,c]:m_tools.fontColors()) ui->comboBoxCharColor->addItem(QString::fromStdString(n));
    ui->comboBoxBackgroundColor->clear();
    for(const auto& [n,c]:m_tools.backgroundColors()) ui->comboBoxBackgroundColor->addItem(QString::fromStdString(n));
    ui->comboBoxResolution->clear();
    for(const auto& [n,r]:m_tools.resolutionRates()) ui->comboBoxResolution->addItem(QString::fromStdString(n));
    ui->comboBoxResolution->setCurrentIndex(2);
    
    // 断开信号避免初始化时触发 onParameterChanged
    ui->comboBoxPaperTemplate->blockSignals(true);
    ui->comboBoxPaperTemplate->clear();
    for(const auto& [k,s]:m_tools.paperSizes()) ui->comboBoxPaperTemplate->addItem(QString::fromStdString(s.name));
    int di=ui->comboBoxPaperTemplate->findText("默认");
    ui->comboBoxPaperTemplate->setCurrentIndex(di>=0?di:0);
    ui->comboBoxPaperTemplate->blockSignals(false);
}

//=============================================================================
// 自动预览
//=============================================================================

void MainWindow::onParameterChanged() { 
    if (m_autoPreviewTimer) m_autoPreviewTimer->start(); 
}

void MainWindow::onTextureChanged(int index) {
    if (index < 0) return;
    
    // 纹理网格尺寸 (rate=1 时的像素)
    int cellSize = 25;  // 默认: 横线纸/方格纸/田字格/点阵纸
    if (index == 4) cellSize = 28;  // 作文纸
    
    if (index > 0) {  // 有纹理
        int fs = ui->lineEditFontSize->text().toInt();
        if (fs <= 0) fs = 30;
        
        // 行间距 = cellSize 的整数倍, 至少容纳字符
        int newSpacing = cellSize * static_cast<int>(std::ceil(fs * 1.2 / cellSize));
        if (newSpacing < cellSize) newSpacing = cellSize;
        
        ui->lineEditLineSpacing->setText(QString::number(newSpacing));
        
        // 上边距: 对齐到第一条纹理线
        int newMargin = 0;
        ui->lineEditMarginTop->setText(QString::number(newMargin));
    }
    
    onParameterChanged();
}

void MainWindow::triggerAutoPreview() {
    if(m_previewWatcher->isRunning()||m_exportWatcher->isRunning()) {
        m_previewPending = true;  // 标记需要重新预览，当前渲染完成后再触发
        return;
    }
    TemplateParams params=getParamsFromForm();
    QString text=getTextFromTextEdit();
    int previewRate=qMin(params.rate,4);
    m_cancelFlag->store(false);
    m_previewWatcher->setFuture(QtConcurrent::run([this,params,text,previewRate](){
        return generatePreviewAsync(params,text,previewRate);
    }));
}

//=============================================================================
// 图片显示
//=============================================================================

void MainWindow::showImage(const QString& p){ QImage img(p); if(!img.isNull())showImage(img); }
void MainWindow::showImage(const QImage& img){
    QPixmap pm=QPixmap::fromImage(img);
    m_scene->clear();
    m_pixmapItem=m_scene->addPixmap(pm);
    m_scene->setSceneRect(pm.rect());
    applyZoom();
    ui->imgPreview->centerOn(m_pixmapItem);
}

//=============================================================================
// 参数获取
//=============================================================================

TemplateParams MainWindow::getParamsFromForm() {
    TemplateParams p=m_generator.templateParams();
    p.paperWidth=ui->lineEditWidth->text().toInt();
    p.paperHeight=ui->lineEditHeight->text().toInt();
    
    int fi=ui->comboBoxFont->currentIndex();
    if(fi>=0&&fi<static_cast<int>(m_cachedFontPaths.size())) p.fontPath=m_cachedFontPaths[fi];
    p.fontSize=ui->lineEditFontSize->text().toInt();
    p.lineSpacing=ui->lineEditLineSpacing->text().toInt();
    p.wordSpacing=ui->lineEditCharDistance->text().toInt();
    p.topMargin=ui->lineEditMarginTop->text().toInt();
    p.bottomMargin=ui->lineEditMarginBottom->text().toInt();
    p.leftMargin=ui->lineEditMarginLeft->text().toInt();
    p.rightMargin=ui->lineEditMarginRight->text().toInt();
    p.fillColor=m_tools.getFontColor(ui->comboBoxCharColor->currentText().toStdString());
    p.backgroundColor=m_tools.getBackgroundColor(ui->comboBoxBackgroundColor->currentText().toStdString());
    p.rate=m_tools.getResolutionRate(ui->comboBoxResolution->currentText().toStdString());
    p.lineSpacingSigma=ui->lineEditLineSpacingSigma->text().toDouble();
    p.fontSizeSigma=ui->lineEditFontSizeSigma->text().toDouble();
    p.wordSpacingSigma=ui->lineEditWordSpacingSigma->text().toDouble();
    p.perturbXSigma=ui->lineEditPerturbXSigma->text().toDouble();
    p.perturbYSigma=ui->lineEditPerturbYSigma->text().toDouble();
    p.perturbThetaSigma=ui->lineEditPerturbThetaSigma->text().toDouble();
    
    int ti=m_comboTexture->currentIndex();
    if(ti==0)p.paperTexture=PaperTexture::None;
    else if(ti==1)p.paperTexture=PaperTexture::HorizontalLine;
    else if(ti==2)p.paperTexture=PaperTexture::Grid;
    else if(ti==3)p.paperTexture=PaperTexture::TianZiGe;
    else if(ti==4)p.paperTexture=PaperTexture::Composition;
    else if(ti==5)p.paperTexture=PaperTexture::DotGrid;
    p.textureOpacity=m_spinTextureOpacity->value();
    p.backgroundImagePath=m_bgImagePath.toStdString();
    p.bgCalibration=m_bgCalibration;
    p.lineGuides=m_lineGuides;
    p.paragraphIndent=m_checkParagraphIndent->isChecked();
    p.paragraphSpacing=m_spinParagraphSpacing->value();
    p.textDirection = (m_comboTextDirection->currentIndex() == 1) ? TextDirection::Vertical : TextDirection::Horizontal;
    p.textWarp = static_cast<TextWarp>(m_comboTextWarp->currentIndex());
    p.inkBleed=m_checkInkBleed->isChecked();
    p.inkBleedRadius=m_spinInkBleedRadius->value();
    p.strikeThroughRate=m_spinStrikeThroughRate->value();
    p.strokeWidthSigma=m_spinStrokeWidthSigma->value();
    p.preserveChinesePunctuation=m_checkPreservePunct->isChecked();
    p.textWarpStrength=m_spinTextWarpStrength->value();
    p.fontMixList=m_fontMixPaths;
    p.fontMixRate=m_spinFontMixRate->value();
    p.seed=static_cast<unsigned int>(qMax(0, m_lineEditSeed->text().toInt()));
    p.charOverrides=m_charOverrides;
    return p;
}

QString MainWindow::getTextFromTextEdit() { return ui->textEditMain->toPlainText(); }

//=============================================================================
// 预览/导出
//=============================================================================

void MainWindow::updatePreview() { triggerAutoPreview(); }

bool MainWindow::guardRenderBudget(const TemplateParams& params) {
    // 页数相关的峰值检查在引擎内部完成；这里只拦「单页就超上限」的情况，
    // 可以在启动渲染前就给出提示，而不是等异常。
    const long long per = HandwriteGenerator::estimateSinglePageBytes(params);
    if (per <= HandwriteGenerator::MAX_SINGLE_PAGE_BYTES) return true;
    const double perGb   = static_cast<double>(per) / (1024.0*1024*1024);
    const double limitGb = static_cast<double>(HandwriteGenerator::MAX_SINGLE_PAGE_BYTES) / (1024.0*1024*1024);
    QMessageBox::warning(this, tr("内存不足"), tr(
        "当前设置单页需要约 %1 GB 内存，超过 %2 GB 上限。\n\n"
        "请降低分辨率倍率（当前 x%3）、缩小纸张尺寸，或关闭墨水洇染 / 背景图片。")
        .arg(perGb, 0, 'f', 1).arg(limitGb, 0, 'f', 0).arg(params.rate));
    return false;
}

void MainWindow::requestCancel() {
    m_cancelFlag->store(true);
    statusBar()->showMessage(tr("正在取消…"), 3000);
}

void MainWindow::onPushButtonPreviewClicked() {
    if(m_previewWatcher->isRunning()||m_exportWatcher->isRunning()){
        QMessageBox::warning(this,tr("请稍候"),tr("正在处理中...")); return;
    }
    TemplateParams p=getParamsFromForm(); QString text=getTextFromTextEdit();
    const int pr=qMin(p.rate,4);
    TemplateParams pp=p; pp.rate=pr;
    if(!guardRenderBudget(pp)) return;
    m_cancelFlag->store(false);
    setupProgressDialog(tr("正在生成预览..."));
    m_previewWatcher->setFuture(QtConcurrent::run([this,p,text,pr](){return generatePreviewAsync(p,text,pr);}));
}

RenderOutcome MainWindow::generatePreviewAsync(TemplateParams params, QString text, int previewRate) {
    RenderOutcome out;
    try {
        HandwriteGenerator g; params.rate=previewRate; g.modifyTemplateParams(params);
        out.images = g.generatePreviewParallel(text.toStdString());
    } catch (const std::exception& e) {
        out.error = QString::fromUtf8(e.what());
    }
    return out;
}

void MainWindow::onPreviewFinished() {
    if(m_progressDialog)m_progressDialog->close();
    if(m_previewWatcher->isCanceled()){ checkPendingPreview(); return; }
    const RenderOutcome out=m_previewWatcher->result();
    if(!out.error.isEmpty()){
        statusBar()->showMessage(tr("预览失败"),5000);
        QMessageBox::warning(this,tr("无法渲染"),out.error);
        checkPendingPreview(); return;
    }
    m_previewImages=out.images;
    m_totalPages=static_cast<int>(m_previewImages.size());m_currentPage=0;
    updatePaginationUI();
    if(!m_previewImages.empty())showImage(m_previewImages[0]);
    
    TemplateParams p=getParamsFromForm();
    auto uc=HandwriteGenerator::findUnsupportedCharsStatic(getTextFromTextEdit().toStdString(),p.fontPath);
    if(m_totalPages>0)statusBar()->showMessage(tr("预览已生成，共 %1 页").arg(m_totalPages),3000);
    if(!uc.empty()){
        QString cl; for(size_t i=0;i<uc.size()&&i<20;++i){cl+=uc[i];if(i<uc.size()-1&&i<19)cl+=" ";}
        if(uc.size()>20)cl+=tr(" ...等共 %1 个").arg(uc.size());
        QMessageBox::warning(this,tr("生僻字提示"),tr("以下 %1 个字符不存在:\n\n%2").arg(uc.size()).arg(cl));
    }
    checkPendingPreview();
}

void MainWindow::checkPendingPreview() {
    if (m_previewPending) {
        m_previewPending = false;
        m_autoPreviewTimer->start();  // 短暂延迟后触发新预览
    }
}

void MainWindow::onPushButtonExportClicked() {
    if(m_previewWatcher->isRunning()||m_exportWatcher->isRunning()){QMessageBox::warning(this,tr("请稍候"),tr("正在处理中..."));return;}
    const QString od="outputs"; QDir d(od);
    if(!d.exists()) d.mkpath(".");
    // 注意：这里不再清空输出目录。
    // 旧实现在此处遍历删除 outputs/ 下「所有」文件（不限扩展名），
    // 既与 v2.5.1 的数据安全修复自相矛盾，也会误删用户自己放进来的文件。
    // 现在清理职责只由引擎内部的 cleanGeneratedPages() 承担——它只删本程序
    // 生成的、以纯数字命名的页码 PNG。
    TemplateParams p=getParamsFromForm(); QString text=getTextFromTextEdit();
    if(!guardRenderBudget(p)) return;
    if(p.rate>=16){
        const double mb = static_cast<double>(HandwriteGenerator::estimateSinglePageBytes(p))/(1024.0*1024.0);
        if(QMessageBox::question(this,tr("高分辨率"),
              tr("x%1 单页约需 %2 MB 内存，可能较慢，继续?").arg(p.rate).arg(static_cast<int>(mb)))
           !=QMessageBox::Yes) return;
    }
    m_cancelFlag->store(false);
    setupProgressDialog(tr("正在导出..."));
    m_exportWatcher->setFuture(QtConcurrent::run([this,p,text,od](){return generateExportAsync(p,text,od);}));
}

ExportOutcome MainWindow::generateExportAsync(TemplateParams params, QString text, QString outputDir) {
    ExportOutcome out;
    auto cancelFlag=m_cancelFlag;
    try {
        HandwriteGenerator g; g.modifyTemplateParams(params);
        out.files = g.generateImageParallel(text.toStdString(),outputDir.toStdString(),0,
            [this](int cur,int tot){
                QMetaObject::invokeMethod(this,[this,cur,tot](){
                    if(m_progressDialog){
                        m_progressDialog->setMaximum(qMax(1,tot));
                        m_progressDialog->setValue(cur);
                        m_progressDialog->setLabelText(tr("渲染 %1/%2").arg(cur).arg(tot));
                    }
                },Qt::QueuedConnection);
            },
            [cancelFlag](){ return cancelFlag->load(); });
    } catch (const std::exception& e) {
        out.error = QString::fromUtf8(e.what());
    }
    return out;
}

void MainWindow::onExportFinished() {
    if(m_exportWatcher->isCanceled()){if(m_progressDialog)m_progressDialog->close();return;}
    const ExportOutcome out=m_exportWatcher->result();
    if(m_progressDialog){m_progressDialog->close();}
    
    if(!out.error.isEmpty()){
        QMessageBox::warning(this,tr("导出失败"),out.error);
        return;
    }
    if(out.files.empty()){
        QMessageBox::information(this,tr("已取消"),tr("导出已取消，未生成任何文件"));
        return;
    }
    
    m_previewImagePaths=out.files;
    int tp=static_cast<int>(m_previewImagePaths.size());
    m_totalPages=tp;m_currentPage=0;updatePaginationUI();
    if(!m_previewImagePaths.empty())showImage(QString::fromStdString(m_previewImagePaths[0]));
    QMessageBox msgBox(this); msgBox.setWindowTitle(tr("导出完成"));
    msgBox.setText(tr("已导出 %1 页").arg(tp)); msgBox.setIcon(QMessageBox::Information);
    auto* obtn=msgBox.addButton(tr("打开目录"),QMessageBox::ActionRole); msgBox.addButton(QMessageBox::Ok);
    msgBox.exec();
    if(msgBox.clickedButton()==obtn) QDesktopServices::openUrl(QUrl::fromLocalFile(QDir("outputs").absolutePath()));
}

//=============================================================================
// SVG 导出
//=============================================================================

void MainWindow::onPushButtonExportSvgClicked() {
    if(m_previewWatcher->isRunning()||m_exportWatcher->isRunning()){QMessageBox::warning(this,tr("请稍候"),tr("正在处理中..."));return;}
    QString path=QFileDialog::getSaveFileName(this,tr("导出SVG"),"output.svg",tr("SVG Files (*.svg)"));
    if(path.isEmpty())return;
    TemplateParams p=getParamsFromForm();
    if(!guardRenderBudget(p)) return;
    const QString text=getTextFromTextEdit();   // 先在工作线程外取好，避免跨线程读 UI
    setupProgressDialog(tr("正在生成SVG..."));
    QFuture<bool> future=QtConcurrent::run([p,text,path](){
        HandwriteGenerator g; g.modifyTemplateParams(p);
        return g.exportSvg(text.toStdString(),path.toStdString());
    });
    auto* watcher=new QFutureWatcher<bool>(this);
    connect(watcher,&QFutureWatcher<bool>::finished,this,[this,watcher,path](){
        if(m_progressDialog)m_progressDialog->close();
        if(watcher->result()) QMessageBox::information(this,tr("导出完成"),
            tr("SVG 已保存到:\n%1\n\n多页文档还会生成 %1 同目录下的 -2、-3 … 文件").arg(path));
        else QMessageBox::warning(this,tr("导出失败"),tr("无法生成SVG（可能未选择字体或内存不足）"));
        watcher->deleteLater();
    });
    watcher->setFuture(future);
}

//=============================================================================
// PDF 导出
//=============================================================================

void MainWindow::onPushButtonExportPdfClicked() {
    if(m_previewWatcher->isRunning()||m_exportWatcher->isRunning()){QMessageBox::warning(this,tr("请稍候"),tr("正在处理中..."));return;}
    QString path=QFileDialog::getSaveFileName(this,tr("导出PDF"),"",tr("PDF Files (*.pdf)"));
    if(path.isEmpty())return;
    TemplateParams p=getParamsFromForm();
    if(!guardRenderBudget(p)) return;
    // 文本必须在主线程取好再捕获进工作线程
    // （旧实现把 getTextFromTextEdit() 写在 QtConcurrent::run 的 lambda 里，
    //   等于在工作线程访问 QWidget，是未定义行为）
    const QString text=getTextFromTextEdit();
    setupProgressDialog(tr("正在生成PDF..."));
    QFuture<bool> future=QtConcurrent::run([p,text,path](){
        HandwriteGenerator g; g.modifyTemplateParams(p);
        return g.exportPdf(text.toStdString(),path.toStdString());
    });
    auto* watcher=new QFutureWatcher<bool>(this);
    connect(watcher,&QFutureWatcher<bool>::finished,this,[this,watcher,path](){
        if(m_progressDialog)m_progressDialog->close();
        if(watcher->result()){QMessageBox::information(this,tr("导出完成"),tr("PDF已保存到:\n%1").arg(path));}
        else QMessageBox::warning(this,tr("导出失败"),tr("无法生成PDF"));
        watcher->deleteLater();
    });
    watcher->setFuture(future);
}

//=============================================================================
// 打印
//=============================================================================

void MainWindow::onPushButtonPrintClicked() {
    if(m_previewImages.empty()&&m_previewImagePaths.empty()){QMessageBox::warning(this,tr("无法打印"),tr("请先预览或导出"));return;}
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPageSize(QPageSize::A4));
    QPrintDialog pd(&printer,this); pd.setWindowTitle(tr("打印"));
    if(pd.exec()!=QDialog::Accepted)return;
    QPainter painter;
    if(!painter.begin(&printer)){QMessageBox::warning(this,tr("失败"),tr("无法启动打印"));return;}
    QRectF prc=printer.pageLayout().paintRectPixels(printer.resolution());
    int tp=qMax(static_cast<int>(m_previewImages.size()),static_cast<int>(m_previewImagePaths.size()));
    for(int page=0;page<tp;++page){
        if(page>0)printer.newPage();
        QImage img;
        if(page<static_cast<int>(m_previewImages.size()))img=m_previewImages[page];
        else{auto it=m_previewImagePaths.find(page);if(it!=m_previewImagePaths.end())img.load(QString::fromStdString(it->second));}
        if(img.isNull())continue;
        double s=qMin(prc.width()/img.width(),prc.height()/img.height());
        int sw=static_cast<int>(img.width()*s),sh=static_cast<int>(img.height()*s);
        int sx=static_cast<int>((prc.width()-sw)/2),sy=static_cast<int>((prc.height()-sh)/2);
        painter.drawImage(QRect(sx,sy,sw,sh),img);
    }
    painter.end();
    statusBar()->showMessage(tr("打印已发送"),3000);
}

//=============================================================================
// 背景图片
//=============================================================================

void MainWindow::onPushButtonSelectBgImageClicked() {
    QString path=QFileDialog::getOpenFileName(this,tr("选择背景图片"),"",tr("Images (*.png *.jpg *.jpeg *.bmp *.webp *.tiff *.tif *.gif *.ico *.pbm *.pgm *.ppm *.xbm *.xpm)"));
    if(path.isEmpty())return;
    m_bgImagePath=path;
    m_labelBgImage->setText(QFileInfo(path).fileName());
    onParameterChanged();
}

void MainWindow::onPushButtonClearBgImageClicked() {
    m_bgImagePath.clear();
    m_labelBgImage->setText(tr("未设置"));
    m_bgCalibration = BackgroundCalibration{};
    // 横线导引是绑定在背景图上的（存的是原图坐标），背景没了它也就没有意义
    m_lineGuides = LineGuideSet{};
    onParameterChanged();
}

void MainWindow::onPushButtonCalibrateBgClicked() {
    if (m_bgImagePath.isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("请先选择背景图片"));
        return;
    }
    CalibrationDialog dlg(m_bgImagePath, m_bgCalibration, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_bgCalibration = dlg.getCalibration();
        onParameterChanged();
    }
}

void MainWindow::onPushButtonLineGuideClicked() {
    if (m_bgImagePath.isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("请先选择背景图片"));
        return;
    }
    // 流程上建议先标定锚点：锚点给页面四角几何，横线给每行的弯曲。
    // 但不强制 —— 正拍的照片不需要锚点也能描线。
    if (!m_bgCalibration.isValid()) {
        const auto ret = QMessageBox::question(
            this, tr("尚未标定锚点"),
            tr("还没有用「锚点」标定页面四角。\n\n"
               "锚点决定文字块的边界与整体透视，横线决定每一行走哪条曲线，两者配合效果最好。\n"
               "正对着拍的照片可以跳过，继续吗？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (ret != QMessageBox::Yes) return;
    }

    LineGuideDialog dlg(m_bgImagePath, m_lineGuides, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_lineGuides = dlg.getGuides();
        onParameterChanged();
    }
}

//=============================================================================
// 预设管理
//=============================================================================

QString MainWindow::presetDir() const {
    // 放到用户数据目录：程序若安装在 Program Files 下 applicationDirPath() 不可写，
    // 而且多用户会互相覆盖预设
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) base = QCoreApplication::applicationDirPath();
    QString d = base + "/presets";
    QDir().mkpath(d);
    return d;
}

void MainWindow::refreshPresetList() {
    if (!m_presetList) return;
    m_presetList->clear();
    QDir d(presetDir());
    // 同时列出历史 .toml 预设（旧版本用的扩展名），新建统一用 .conf
    for(const auto& f:d.entryList({"*.conf","*.toml"},QDir::Files|QDir::Readable,QDir::Name))
        m_presetList->addItem(f);
}

void MainWindow::onPushButtonPresetSaveClicked() {
    QString name=QInputDialog::getText(this,tr("保存预设"),tr("预设名称:"));
    if(name.isEmpty())return;
    // 去掉文件名非法字符
    name.replace(QRegularExpression("[/\\\\:*?\"<>|]"), "_");
    QString path=presetDir()+"/"+name+".conf";
    saveConfiguration(path);
    refreshPresetList();
}

void MainWindow::onPushButtonPresetLoadClicked() {
    auto* item=m_presetList->currentItem();
    if(!item){QMessageBox::warning(this,tr("提示"),tr("请选择预设"));return;}
    loadConfiguration(presetDir()+"/"+item->text());
}

void MainWindow::onPushButtonPresetDeleteClicked() {
    auto* item=m_presetList->currentItem();
    if(!item)return;
    if(QMessageBox::question(this,tr("确认"),tr("删除预设 %1?").arg(item->text()))==QMessageBox::Yes){
        QFile::remove(presetDir()+"/"+item->text());
        refreshPresetList();
    }
}

//=============================================================================
// 配置保存/加载
//=============================================================================

void MainWindow::saveConfiguration(const QString& path) {
    Config c;
    c.setWidth(ui->lineEditWidth->text().toInt());
    c.setHeight(ui->lineEditHeight->text().toInt());
    int fi=ui->comboBoxFont->currentIndex();
    if(fi>=0&&fi<static_cast<int>(m_cachedFontPaths.size()))c.setTtfSelector(m_cachedFontPaths[fi]);
    c.setFontSize(ui->lineEditFontSize->text().toInt());
    c.setLineSpacing(ui->lineEditLineSpacing->text().toInt());
    c.setCharDistance(ui->lineEditCharDistance->text().toInt());
    c.setMarginTop(ui->lineEditMarginTop->text().toInt());
    c.setMarginBottom(ui->lineEditMarginBottom->text().toInt());
    c.setMarginLeft(ui->lineEditMarginLeft->text().toInt());
    c.setMarginRight(ui->lineEditMarginRight->text().toInt());
    auto cc=m_tools.getFontColor(ui->comboBoxCharColor->currentText().toStdString());
    c.setCharColor({cc.r,cc.g,cc.b,cc.a});
    auto bc=m_tools.getBackgroundColor(ui->comboBoxBackgroundColor->currentText().toStdString());
    c.setBackgroundColor({bc.r,bc.g,bc.b,bc.a});
    c.setResolution(m_tools.getResolutionRate(ui->comboBoxResolution->currentText().toStdString()));
    c.setLineSpacingSigma(ui->lineEditLineSpacingSigma->text().toDouble());
    c.setFontSizeSigma(ui->lineEditFontSizeSigma->text().toDouble());
    c.setWordSpacingSigma(ui->lineEditWordSpacingSigma->text().toDouble());
    c.setPerturbXSigma(ui->lineEditPerturbXSigma->text().toDouble());
    c.setPerturbYSigma(ui->lineEditPerturbYSigma->text().toDouble());
    c.setPerturbThetaSigma(ui->lineEditPerturbThetaSigma->text().toDouble());
    c.setPaperTexture(m_comboTexture->currentText().toStdString());
    c.setTextureOpacity(m_spinTextureOpacity->value());
    if(!m_bgImagePath.isEmpty())c.setBackgroundImage(m_bgImagePath.toStdString());
    c.setParagraphIndent(m_checkParagraphIndent->isChecked());
    c.setParagraphSpacing(m_spinParagraphSpacing->value());
    c.setInkBleed(m_checkInkBleed->isChecked());
    c.setInkBleedRadius(m_spinInkBleedRadius->value());
    c.setStrikeThroughRate(m_spinStrikeThroughRate->value());
    c.setStrokeWidthSigma(m_spinStrokeWidthSigma->value());
    // ---- 以下参数此前完全没有写入配置，导致保存预设后再加载会丢失 ----
    c.setTextDirection(m_comboTextDirection->currentIndex());
    c.setTextWarp(m_comboTextWarp->currentIndex());
    c.setTextWarpStrength(m_spinTextWarpStrength->value());
    c.setPreserveChinesePunctuation(m_checkPreservePunct->isChecked());
    c.setFontMixList(m_fontMixPaths);
    c.setFontMixRate(m_spinFontMixRate->value());
    c.setSeed(static_cast<unsigned int>(qMax(0, m_lineEditSeed->text().toInt())));
    // 背景图片锚点校准
    c.setBgCalibEnabled(m_bgCalibration.isValid());
    if(m_bgCalibration.isValid()){
        c.setBgCalibRows(m_bgCalibration.rows);
        c.setBgCalibCols(m_bgCalibration.cols);
        std::vector<double> pts;
        pts.reserve(m_bgCalibration.gridPoints.size()*2);
        for(const auto& pt:m_bgCalibration.gridPoints){ pts.push_back(pt.x()); pts.push_back(pt.y()); }
        c.setBgCalibPoints(pts);
    }
    // 背景图横线导引（作业本横线）
    c.setLineGuideEnabled(m_lineGuides.enabled && !m_lineGuides.keyCurves.empty());
    if(m_lineGuides.enabled && !m_lineGuides.keyCurves.empty()){
        c.setLineGuideLineCount(m_lineGuides.lineCount);
        c.setLineGuideInterpolate(m_lineGuides.useInterpolation);
        c.setLineGuideBaselineRatio(m_lineGuides.baselineRatio);
        c.setLineGuideBaselineOffset(m_lineGuides.baselineOffset);
        c.setLineGuideFollowCurve(m_lineGuides.followCurve);
        c.setLineGuideLinesPerRow(m_lineGuides.linesPerRow);
        c.setLineGuideCurves(flattenGuideCurves(m_lineGuides.keyCurves));
    }
    // 字符级覆盖
    {
        std::vector<std::string> list;
        list.reserve(m_charOverrides.size());
        for(const auto& r:m_charOverrides) list.push_back(HandwriteGenerator::serializeCharOverride(r));
        c.setCharOverrides(list);
    }
    if(c.save(path.toStdString()))QMessageBox::information(this,tr("成功"),tr("已保存"));
    else QMessageBox::warning(this,tr("失败"),tr("无法保存"));
    ui->labelCurrentConfig->setText(tr("当前配置文件:\n%1").arg(path));
}

void MainWindow::loadConfiguration(const QString& path) {
    Config c(path.toStdString());
    if(auto v=c.width())ui->lineEditWidth->setText(QString::number(*v));
    if(auto v=c.height())ui->lineEditHeight->setText(QString::number(*v));
    if(auto v=c.ttfSelector()){
        for(int i=0;i<static_cast<int>(m_cachedFontPaths.size());++i)
            if(m_cachedFontPaths[i]==*v){ui->comboBoxFont->setCurrentIndex(i);break;}
    }
    if(auto v=c.fontSize())ui->lineEditFontSize->setText(QString::number(*v));
    if(auto v=c.lineSpacing())ui->lineEditLineSpacing->setText(QString::number(*v));
    if(auto v=c.charDistance())ui->lineEditCharDistance->setText(QString::number(*v));
    if(auto v=c.marginTop())ui->lineEditMarginTop->setText(QString::number(*v));
    if(auto v=c.marginBottom())ui->lineEditMarginBottom->setText(QString::number(*v));
    if(auto v=c.marginLeft())ui->lineEditMarginLeft->setText(QString::number(*v));
    if(auto v=c.marginRight())ui->lineEditMarginRight->setText(QString::number(*v));
    if(auto v=c.charColor()){for(const auto&[n,cl]:m_tools.fontColors())if(cl.r==(*v)[0]&&cl.g==(*v)[1]&&cl.b==(*v)[2]&&cl.a==(*v)[3]){ui->comboBoxCharColor->setCurrentText(QString::fromStdString(n));break;}}
    if(auto v=c.backgroundColor()){for(const auto&[n,cl]:m_tools.backgroundColors())if(cl.r==(*v)[0]&&cl.g==(*v)[1]&&cl.b==(*v)[2]&&cl.a==(*v)[3]){ui->comboBoxBackgroundColor->setCurrentText(QString::fromStdString(n));break;}}
    if(auto v=c.resolution()){for(const auto&[n,r]:m_tools.resolutionRates())if(r==*v){ui->comboBoxResolution->setCurrentText(QString::fromStdString(n));break;}}
    if(auto v=c.lineSpacingSigma())ui->lineEditLineSpacingSigma->setText(QString::number(*v));
    if(auto v=c.fontSizeSigma())ui->lineEditFontSizeSigma->setText(QString::number(*v));
    if(auto v=c.wordSpacingSigma())ui->lineEditWordSpacingSigma->setText(QString::number(*v));
    if(auto v=c.perturbXSigma())ui->lineEditPerturbXSigma->setText(QString::number(*v));
    if(auto v=c.perturbYSigma())ui->lineEditPerturbYSigma->setText(QString::number(*v));
    if(auto v=c.perturbThetaSigma())ui->lineEditPerturbThetaSigma->setText(QString::number(*v));
    if(auto v=c.paperTexture())m_comboTexture->setCurrentText(QString::fromStdString(*v));
    if(auto v=c.textureOpacity())m_spinTextureOpacity->setValue(*v);
    if(auto v=c.backgroundImage()){m_bgImagePath=QString::fromStdString(*v);m_labelBgImage->setText(QFileInfo(m_bgImagePath).fileName());}
    if(auto v=c.paragraphIndent())m_checkParagraphIndent->setChecked(*v);
    if(auto v=c.paragraphSpacing())m_spinParagraphSpacing->setValue(*v);
    if(auto v=c.inkBleed())m_checkInkBleed->setChecked(*v);
    if(auto v=c.inkBleedRadius())m_spinInkBleedRadius->setValue(*v);
    if(auto v=c.strikeThroughRate())m_spinStrikeThroughRate->setValue(*v);
    if(auto v=c.strokeWidthSigma())m_spinStrokeWidthSigma->setValue(*v);
    if(auto v=c.textDirection())m_comboTextDirection->setCurrentIndex(*v);
    if(auto v=c.textWarp())m_comboTextWarp->setCurrentIndex(*v);
    if(auto v=c.textWarpStrength())m_spinTextWarpStrength->setValue(*v);
    if(auto v=c.preserveChinesePunctuation())m_checkPreservePunct->setChecked(*v);
    if(auto v=c.fontMixList()){ m_fontMixPaths=*v; updateFontMixLabel(); }
    if(auto v=c.fontMixRate())m_spinFontMixRate->setValue(*v);
    if(auto v=c.seed())m_lineEditSeed->setText(QString::number(*v));
    // 背景锚点校准
    if(c.bgCalibEnabled().value_or(false)){
        const int rows=c.bgCalibRows().value_or(3), cols=c.bgCalibCols().value_or(3);
        if(auto pts=c.bgCalibPoints()){
            if(static_cast<int>(pts->size())==rows*cols*2){
                BackgroundCalibration cal;
                cal.enabled=true; cal.rows=rows; cal.cols=cols;
                cal.gridPoints.resize(rows*cols);
                for(int i=0;i<rows*cols;++i)
                    cal.gridPoints[i]=QPointF((*pts)[i*2],(*pts)[i*2+1]);
                m_bgCalibration=cal;
            }
        }
    }
    // 背景图横线导引（批次 1 无编辑 UI，只原样保留）
    {
        LineGuideSet g;
        g.enabled=c.lineGuideEnabled().value_or(false);
        if(auto v=c.lineGuideLineCount())g.lineCount=*v;
        if(auto v=c.lineGuideInterpolate())g.useInterpolation=*v;
        if(auto v=c.lineGuideBaselineRatio())g.baselineRatio=*v;
        if(auto v=c.lineGuideBaselineOffset())g.baselineOffset=*v;
        if(auto v=c.lineGuideFollowCurve())g.followCurve=*v;
        if(auto v=c.lineGuideLinesPerRow())g.linesPerRow=*v;
        if(auto v=c.lineGuideCurves())g.keyCurves=parseGuideCurves(*v);
        if(g.keyCurves.empty())g.enabled=false;
        m_lineGuides=g;
    }
    // 字符级覆盖
    if(auto list=c.charOverrides()){
        m_charOverrides.clear();
        for(const auto& s:*list){
            if(auto r=HandwriteGenerator::deserializeCharOverride(s)) m_charOverrides.push_back(*r);
        }
        updateCharOverrideLabel();
    }
    ui->labelCurrentConfig->setText(tr("当前配置文件:\n%1").arg(path));
    onParameterChanged();
}

//=============================================================================
// 按钮槽函数
//=============================================================================

void MainWindow::onPushButtonSaveConfigClicked() {
    QString p=QFileDialog::getSaveFileName(this,tr("保存配置"),"preset.conf",
        tr("配置文件 (*.conf);;旧版配置 (*.toml);;所有文件 (*)"));
    if(!p.isEmpty())saveConfiguration(p);
}
void MainWindow::onPushButtonLoadConfigClicked() {
    QString p=QFileDialog::getOpenFileName(this,tr("加载配置"),"",
        tr("配置文件 (*.conf *.toml);;所有文件 (*)"));
    if(!p.isEmpty())loadConfiguration(p);
}

void MainWindow::onPushButtonCharOverrideClicked() {
    QTextCursor cur=ui->textEditMain->textCursor();
    int sp=cur.selectionStart(),ep=cur.selectionEnd();
    if(sp==ep){QMessageBox::warning(this,tr("提示"),tr("请先选中文本"));return;}
    if(sp>ep)std::swap(sp,ep);
    CharacterOverride ex; bool fe=false;
    for(const auto& r:m_charOverrides){if(r.startIndex==sp&&r.endIndex==ep-1){ex=r.override;fe=true;break;}}
    auto* dlg=new CharacterOverrideDialog(this);dlg->setAttribute(Qt::WA_DeleteOnClose);
    if(fe)dlg->setOverride(ex);
    struct Rng{int s,e;}rng{sp,ep-1};
    connect(dlg,&CharacterOverrideDialog::applied,this,[this,dlg,rng](){
        CharacterOverride ov=dlg->getOverride();
        if(ov.isEmpty())m_charOverrides.erase(std::remove_if(m_charOverrides.begin(),m_charOverrides.end(),[rng](const auto& r){return r.startIndex==rng.s&&r.endIndex==rng.e;}),m_charOverrides.end());
        else{m_charOverrides.erase(std::remove_if(m_charOverrides.begin(),m_charOverrides.end(),[rng](const auto& r){return r.startIndex==rng.s&&r.endIndex==rng.e;}),m_charOverrides.end());
        CharacterOverrideRange nr;nr.startIndex=rng.s;nr.endIndex=rng.e;nr.override=ov;m_charOverrides.push_back(nr);}
        updateCharOverrideLabel();updatePreview();
    });
    dlg->show();
}
void MainWindow::onPushButtonClearOverridesClicked() { m_charOverrides.clear(); updateCharOverrideLabel(); updatePreview(); }
void MainWindow::updateCharOverrideLabel() { ui->labelCharOverride->setText(tr("字符覆盖: %1 处").arg(m_charOverrides.size())); }

//=============================================================================
// 混合字体
//=============================================================================

FontMixDialog::FontMixDialog(const std::vector<std::string>& fontNames,
                             const std::vector<std::string>& fontPaths,
                             const std::vector<std::string>& selected,
                             QWidget* parent)
    : QDialog(parent), m_list(new QListWidget(this)), m_fontPaths(fontPaths) {
    setWindowTitle(tr("选择混合字体"));
    resize(360, 420);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("勾选参与随机混合的字体（正文仍以主字体为主）:"), this));
    layout->addWidget(m_list, 1);
    
    for (size_t i = 0; i < fontNames.size(); ++i) {
        const std::string& path = (i < fontPaths.size()) ? fontPaths[i] : std::string();
        auto* item = new QListWidgetItem(QString::fromStdString(fontNames[i]), m_list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        const bool on = std::find(selected.begin(), selected.end(), path) != selected.end();
        item->setCheckState(on ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, QString::fromStdString(path));
    }
    
    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(btns);
}

std::vector<std::string> FontMixDialog::selectedPaths() const {
    std::vector<std::string> out;
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->checkState() == Qt::Checked) {
            out.push_back(m_list->item(i)->data(Qt::UserRole).toString().toStdString());
        }
    }
    return out;
}

void MainWindow::updateFontMixLabel() {
    if (!m_labelFontMix) return;
    if (m_fontMixPaths.empty()) {
        m_labelFontMix->setText(tr("未启用"));
        return;
    }
    QStringList names;
    for (const auto& p : m_fontMixPaths) {
        names << QFileInfo(QString::fromStdString(p)).baseName();
    }
    m_labelFontMix->setText(tr("%1 个字体").arg(names.size()));
    m_labelFontMix->setToolTip(names.join("、"));
}

void MainWindow::onPushButtonFontMixClicked() {
    FontMixDialog dlg(m_cachedFontNames, m_cachedFontPaths, m_fontMixPaths, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_fontMixPaths = dlg.selectedPaths();
        updateFontMixLabel();
        onParameterChanged();
    }
}

//=============================================================================
// 随机种子
//=============================================================================

void MainWindow::onPushButtonNewSeedClicked() {
    if (!m_lineEditSeed) return;
    m_lineEditSeed->setText(QString::number(makeRandomSeed()));
    onParameterChanged();
}

//=============================================================================
// 分页
//=============================================================================

void MainWindow::onPushButtonFirstPageClicked(){goToPage(0);}
void MainWindow::onPushButtonPrevPageClicked(){if(m_currentPage>0)goToPage(m_currentPage-1);}
void MainWindow::onPushButtonNextPageClicked(){if(m_currentPage<m_totalPages-1)goToPage(m_currentPage+1);}
void MainWindow::onPushButtonLastPageClicked(){if(m_totalPages>0)goToPage(m_totalPages-1);}
void MainWindow::onSpinBoxPageValueChanged(int v){goToPage(v-1);}

void MainWindow::goToPage(int page) {
    if(page<0||page>=m_totalPages||page==m_currentPage)return;
    m_currentPage=page; updatePaginationUI();
    if(m_currentPage>=0&&m_currentPage<static_cast<int>(m_previewImages.size()))showImage(m_previewImages[m_currentPage]);
    else{auto it=m_previewImagePaths.find(m_currentPage);if(it!=m_previewImagePaths.end())showImage(QString::fromStdString(it->second));}
}

void MainWindow::updatePaginationUI() {
    ui->labelPageInfo->setText(tr("%1 / %2").arg(m_currentPage+1).arg(m_totalPages));
    ui->spinBoxPage->blockSignals(true);
    ui->spinBoxPage->setRange(1,qMax(1,m_totalPages));
    ui->spinBoxPage->setValue(m_currentPage+1);
    ui->spinBoxPage->blockSignals(false);
    updatePageButtons();
}

void MainWindow::updatePageButtons() {
    bool hp=m_totalPages>0,hm=m_totalPages>1;
    ui->pushButtonFirstPage->setEnabled(hm&&m_currentPage>0);
    ui->pushButtonPrevPage->setEnabled(hp&&m_currentPage>0);
    ui->pushButtonNextPage->setEnabled(hp&&m_currentPage<m_totalPages-1);
    ui->pushButtonLastPage->setEnabled(hm&&m_currentPage<m_totalPages-1);
    ui->spinBoxPage->setEnabled(hp);
}

//=============================================================================
// 纸张模板
//=============================================================================

void MainWindow::onComboBoxPaperTemplateCurrentIndexChanged(int) {
    PaperSize ps=m_tools.getPaperSizeByDisplayName(ui->comboBoxPaperTemplate->currentText().toStdString());
    ui->lineEditWidth->setText(QString::number(ps.width));
    ui->lineEditHeight->setText(QString::number(ps.height));
    bool ic=(ui->comboBoxPaperTemplate->currentText()=="自定义");
    ui->lineEditWidth->setReadOnly(!ic); ui->lineEditHeight->setReadOnly(!ic);
    onParameterChanged();
}

//=============================================================================
// CalibrationDialog 实现 — 四角锚点 + 精细网格
//=============================================================================
// 全部改用真实布局：旧实现用 setGeometry + setFixedSize 绝对定位按钮，
// 在系统缩放/字体放大时按钮会重叠、标签会跑位。

//=============================================================================
// 图片画布对话框基类
//=============================================================================

ImageCanvasDialog::ImageCanvasDialog(QWidget* parent) : QDialog(parent) {
    setMinimumSize(480, 400);
}

bool ImageCanvasDialog::loadCanvasImage(const QString& imagePath) {
    // 统一加载器（含 WebP 回退）
    m_image = loadImageWithWebpFallback(imagePath.toStdString());
    if (m_image.isNull()) {
        QMessageBox::warning(this, tr("错误"), tr("无法加载背景图片（格式不支持或文件损坏）"));
        return false;
    }
    return true;
}

void ImageCanvasDialog::installCanvas(QVBoxLayout* root, int minHeight) {
    m_canvas = new QWidget(this);
    m_canvas->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_canvas->setMinimumHeight(minHeight);
    // 鼠标事件穿透到对话框，命中测试统一在对话框里做
    m_canvas->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    root->addWidget(m_canvas, 1);
    setMouseTracking(true);
}

void ImageCanvasDialog::relayoutCanvas() {
    if (!m_canvas || m_image.isNull()) return;
    const QRect area = m_canvas->geometry();
    if (area.width() < 2 || area.height() < 2) return;

    const qreal scale = std::min(static_cast<qreal>(area.width())  / m_image.width(),
                                 static_cast<qreal>(area.height()) / m_image.height());
    const int w = std::max(1, static_cast<int>(m_image.width()  * scale));
    const int h = std::max(1, static_cast<int>(m_image.height() * scale));
    m_scaledPixmap = QPixmap::fromImage(m_image.scaled(w, h, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_drawRect = QRect(area.x() + (area.width()  - m_scaledPixmap.width())  / 2,
                       area.y() + (area.height() - m_scaledPixmap.height()) / 2,
                       m_scaledPixmap.width(), m_scaledPixmap.height());
    update();
}

QPointF ImageCanvasDialog::toImageCoords(const QPoint& widgetPos) const {
    if (m_drawRect.width() < 1 || m_drawRect.height() < 1) return QPointF();
    const qreal rx = static_cast<qreal>(m_image.width())  / m_drawRect.width();
    const qreal ry = static_cast<qreal>(m_image.height()) / m_drawRect.height();
    return QPointF((widgetPos.x() - m_drawRect.x()) * rx,
                   (widgetPos.y() - m_drawRect.y()) * ry);
}

QPoint ImageCanvasDialog::toWidgetCoords(const QPointF& imgPos) const {
    if (m_image.width() < 1 || m_image.height() < 1) return QPoint();
    const qreal rx = static_cast<qreal>(m_drawRect.width())  / m_image.width();
    const qreal ry = static_cast<qreal>(m_drawRect.height()) / m_image.height();
    return QPoint(m_drawRect.x() + static_cast<int>(imgPos.x() * rx),
                  m_drawRect.y() + static_cast<int>(imgPos.y() * ry));
}

void ImageCanvasDialog::drawCanvasTip(QPainter& p, const QString& tip) {
    p.setPen(QColor(200, 200, 200));
    p.setFont(QFont(QStringLiteral("Microsoft YaHei"), 9));
    p.drawText(8, m_drawRect.bottom() + 18, tip);
}

void ImageCanvasDialog::resizeEvent(QResizeEvent*) { relayoutCanvas(); }

//=============================================================================
// 背景图片校准对话框（锚点）
//=============================================================================

CalibrationDialog::CalibrationDialog(const QString& imagePath, const BackgroundCalibration& calib, QWidget* parent)
    : ImageCanvasDialog(parent), m_rows(3), m_cols(3) {
    setWindowTitle(tr("网格校准 — 拖拽四角锚点适配纸面"));
    resize(840, 640);

    if (!loadCanvasImage(imagePath)) {
        QMetaObject::invokeMethod(this, [this]() { reject(); }, Qt::QueuedConnection);
        return;
    }
    
    // 恢复已有校准 或 默认四角模式
    if (calib.enabled && calib.isValid()) {
        m_rows = calib.rows;
        m_cols = calib.cols;
        m_points = calib.gridPoints;
        m_cornerMode = false;  // 已有完整网格 → 精细模式
    } else {
        applyMode(true);  // 默认四角模式
    }
    
    // ── 根布局：图片区（自适应） + 按钮栏 ──
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);
    
    installCanvas(root, 240);
    
    auto* btnBar = new QWidget(this);
    auto* bl = new QHBoxLayout(btnBar);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(5);
    
    auto makeBtn = [&](const QString& text, const QString& tip = QString()) {
        auto* b = new QPushButton(text, btnBar);
        b->setMinimumWidth(34);
        if (!tip.isEmpty()) b->setToolTip(tip);
        return b;
    };
    
    // 模式切换按钮（始终可见）
    m_modeBtn = new QPushButton(tr("精细调整 ↗"), btnBar);
    m_modeBtn->setStyleSheet("QPushButton { color: #5af; border: 1px solid #5af;"
                             " border-radius: 4px; padding: 2px 8px; }"
                             "QPushButton:hover { background: rgba(80,170,255,.15); }");
    if (!m_cornerMode) m_modeBtn->setText(tr("四角模式 ↙"));
    bl->addWidget(m_modeBtn);
    bl->addSpacing(10);
    
    m_btnColM = makeBtn("-", tr("减少网格列数"));
    m_lblCol = new QLabel(tr("列:%1").arg(m_cols), btnBar);
    m_btnColP = makeBtn("+", tr("增加网格列数"));
    bl->addWidget(m_btnColM); bl->addWidget(m_lblCol); bl->addWidget(m_btnColP);
    bl->addSpacing(10);
    
    m_btnRowM = makeBtn("-", tr("减少网格行数"));
    m_lblRow = new QLabel(tr("行:%1").arg(m_rows), btnBar);
    m_btnRowP = makeBtn("+", tr("增加网格行数"));
    bl->addWidget(m_btnRowM); bl->addWidget(m_lblRow); bl->addWidget(m_btnRowP);
    bl->addSpacing(10);
    
    m_btnReset = new QPushButton(tr("重置"), btnBar);
    bl->addWidget(m_btnReset);
    
    bl->addStretch();
    
    auto* btnOk = new QPushButton(tr("确定"), btnBar);
    btnOk->setDefault(true);
    auto* btnCancel = new QPushButton(tr("取消"), btnBar);
    bl->addWidget(btnOk);
    bl->addWidget(btnCancel);
    
    root->addWidget(btnBar, 0);
    
    // ── 连接 ──
    connect(m_modeBtn, &QPushButton::clicked, this, [this]() {
        if (m_cornerMode) {
            // 四角 → 精细：内部点由四角双线性插值
            applyMode(false, 3, 3);
            m_modeBtn->setText(tr("四角模式 ↙"));
            m_btnColP->show(); m_btnColM->show(); m_lblCol->show();
            m_btnRowP->show(); m_btnRowM->show(); m_lblRow->show();
            m_btnReset->show();
        } else {
            // 精细 → 四角：保留四角
            applyMode(true);
            m_modeBtn->setText(tr("精细调整 ↗"));
            m_btnColP->hide(); m_btnColM->hide(); m_lblCol->hide();
            m_btnRowP->hide(); m_btnRowM->hide(); m_lblRow->hide();
            m_btnReset->hide();
        }
        m_lblCol->setText(tr("列:%1").arg(m_cols));
        m_lblRow->setText(tr("行:%1").arg(m_rows));
        update();
    });
    
    connect(m_btnColP, &QPushButton::clicked, this, [this]() {
        if (!m_cornerMode && m_cols < 5) { applyMode(false, m_rows, m_cols + 1); m_lblCol->setText(tr("列:%1").arg(m_cols)); update(); }
    });
    connect(m_btnColM, &QPushButton::clicked, this, [this]() {
        if (!m_cornerMode && m_cols > 2) { applyMode(false, m_rows, m_cols - 1); m_lblCol->setText(tr("列:%1").arg(m_cols)); update(); }
    });
    connect(m_btnRowP, &QPushButton::clicked, this, [this]() {
        if (!m_cornerMode && m_rows < 5) { applyMode(false, m_rows + 1, m_cols); m_lblRow->setText(tr("行:%1").arg(m_rows)); update(); }
    });
    connect(m_btnRowM, &QPushButton::clicked, this, [this]() {
        if (!m_cornerMode && m_rows > 2) { applyMode(false, m_rows - 1, m_cols); m_lblRow->setText(tr("行:%1").arg(m_rows)); update(); }
    });
    connect(m_btnReset, &QPushButton::clicked, this, [this]() {
        if (m_cornerMode) { applyMode(true); }
        else { buildUniformGrid(); }
        update();
    });
    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    
    // 初始显示状态
    if (m_cornerMode) {
        m_btnColP->hide(); m_btnColM->hide(); m_lblCol->hide();
        m_btnRowP->hide(); m_btnRowM->hide(); m_lblRow->hide();
        m_btnReset->hide();
    }
    
    setMouseTracking(true);
}

void CalibrationDialog::applyMode(bool cornerMode, int newRows, int newCols) {
    if (cornerMode) {
        // 如果当前是精细模式，从当前网格提取四角
        std::vector<QPointF> corners(4);
        if (!m_cornerMode && !m_points.empty()) {
            corners[0] = m_points[0];                           // TL
            corners[1] = m_points[m_cols - 1];                  // TR
            corners[2] = m_points[(m_rows - 1) * m_cols];       // BL
            corners[3] = m_points[m_rows * m_cols - 1];         // BR
        } else {
            // 默认：图片四角
            corners[0] = QPointF(0, 0);
            corners[1] = QPointF(m_image.width(), 0);
            corners[2] = QPointF(0, m_image.height());
            corners[3] = QPointF(m_image.width(), m_image.height());
        }
        m_rows = 2; m_cols = 2;
        m_cornerMode = true;
        m_points = corners;
    } else {
        const int oldRows = m_rows, oldCols = m_cols;
        m_rows = newRows; m_cols = newCols;
        m_cornerMode = false;
        // 从四角双线性插值出内部点（用旧网格坐标取角，兼容 2×2 和 N×M）
        QPointF tl = m_points[0];
        QPointF tr = m_points[oldCols - 1];
        QPointF bl = m_points[(oldRows - 1) * oldCols];
        QPointF br = m_points[oldRows * oldCols - 1];
        m_points.resize(m_rows * m_cols);
        for (int r = 0; r < m_rows; ++r) {
            qreal v = (m_rows > 1) ? static_cast<qreal>(r) / (m_rows - 1) : 0;
            for (int c = 0; c < m_cols; ++c) {
                qreal u = (m_cols > 1) ? static_cast<qreal>(c) / (m_cols - 1) : 0;
                qreal x = (1 - v) * ((1 - u) * tl.x() + u * tr.x()) + v * ((1 - u) * bl.x() + u * br.x());
                qreal y = (1 - v) * ((1 - u) * tl.y() + u * tr.y()) + v * ((1 - u) * bl.y() + u * br.y());
                m_points[r * m_cols + c] = QPointF(x, y);
            }
        }
    }
}

void CalibrationDialog::buildUniformGrid() {
    m_points.resize(m_rows * m_cols);
    for (int r = 0; r < m_rows; ++r) {
        qreal y = m_image.height() * r / (m_rows - 1.0);
        for (int c = 0; c < m_cols; ++c) {
            qreal x = m_image.width() * c / (m_cols - 1.0);
            m_points[r * m_cols + c] = QPointF(x, y);
        }
    }
}

BackgroundCalibration CalibrationDialog::getCalibration() const {
    BackgroundCalibration c;
    c.enabled = true;
    if (m_cornerMode) {
        // 四角模式：输出 3×3 插值网格
        c.rows = 3; c.cols = 3;
        c.gridPoints.resize(9);
        QPointF tl = m_points[0], tr = m_points[1], bl = m_points[2], br = m_points[3];
        for (int r = 0; r < 3; ++r) {
            qreal v = r / 2.0;
            for (int col = 0; col < 3; ++col) {
                qreal u = col / 2.0;
                qreal x = (1 - v) * ((1 - u) * tl.x() + u * tr.x()) + v * ((1 - u) * bl.x() + u * br.x());
                qreal y = (1 - v) * ((1 - u) * tl.y() + u * tr.y()) + v * ((1 - u) * bl.y() + u * br.y());
                c.gridPoints[r * 3 + col] = QPointF(x, y);
            }
        }
    } else {
        c.rows = m_rows;
        c.cols = m_cols;
        c.gridPoints = m_points;
    }
    return c;
}

void CalibrationDialog::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(40, 40, 40));
    if (m_scaledPixmap.isNull()) return;
    p.drawPixmap(m_drawRect.topLeft(), m_scaledPixmap);
    
    if (m_cornerMode) {
        // ── 四角模式：只画四角大锚点和轮廓线 ──
        QPen outlinePen(QColor(0, 200, 100, 180), 2, Qt::DashLine);
        p.setPen(outlinePen);
        p.drawLine(toWidgetCoords(m_points[0]), toWidgetCoords(m_points[1])); // 上
        p.drawLine(toWidgetCoords(m_points[1]), toWidgetCoords(m_points[3])); // 右
        p.drawLine(toWidgetCoords(m_points[2]), toWidgetCoords(m_points[3])); // 下
        p.drawLine(toWidgetCoords(m_points[0]), toWidgetCoords(m_points[2])); // 左
        
        // 预览内插线（淡色，帮助预览效果）
        QPointF corners[4] = {m_points[0], m_points[1], m_points[2], m_points[3]};
        QPen previewPen(QColor(0, 200, 100, 50), 1, Qt::DotLine);
        p.setPen(previewPen);
        const int previewRows = 3, previewCols = 3;
        auto interp = [&](qreal u, qreal v) -> QPointF {
            return QPointF(
                (1 - v) * ((1 - u) * corners[0].x() + u * corners[1].x()) + v * ((1 - u) * corners[2].x() + u * corners[3].x()),
                (1 - v) * ((1 - u) * corners[0].y() + u * corners[1].y()) + v * ((1 - u) * corners[2].y() + u * corners[3].y()));
        };
        for (int r = 0; r < previewRows; ++r) {
            const qreal v = r / (previewRows - 1.0);
            for (int c = 0; c < previewCols - 1; ++c) {
                p.drawLine(toWidgetCoords(interp(c / (previewCols - 1.0), v)),
                           toWidgetCoords(interp((c + 1) / (previewCols - 1.0), v)));
            }
        }
        for (int r = 0; r < previewRows - 1; ++r) {
            for (int c = 0; c < previewCols; ++c) {
                const qreal u = c / (previewCols - 1.0);
                p.drawLine(toWidgetCoords(interp(u, r / (previewRows - 1.0))),
                           toWidgetCoords(interp(u, (r + 1) / (previewRows - 1.0))));
            }
        }
        
        // 四个大锚点
        for (int i = 0; i < 4; ++i) {
            const QPoint wp = toWidgetCoords(m_points[i]);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 200, 100));
            p.drawEllipse(wp, 7, 7);
            p.setBrush(Qt::white);
            p.drawEllipse(wp, 3, 3);
        }
    } else {
        // ── 精细模式：完整网格 ──
        QPen gridPen(QColor(0, 160, 255, 120), 1);
        p.setPen(gridPen);
        for (int r = 0; r < m_rows; ++r) {
            for (int c = 0; c < m_cols - 1; ++c) {
                p.drawLine(toWidgetCoords(m_points[r * m_cols + c]),
                           toWidgetCoords(m_points[r * m_cols + c + 1]));
            }
        }
        for (int r = 0; r < m_rows - 1; ++r) {
            for (int c = 0; c < m_cols; ++c) {
                p.drawLine(toWidgetCoords(m_points[r * m_cols + c]),
                           toWidgetCoords(m_points[(r + 1) * m_cols + c]));
            }
        }
        
        // 网格点（四角用绿色高亮）
        for (int i = 0; i < m_rows * m_cols; ++i) {
            const int r = i / m_cols, c = i % m_cols;
            const bool isCorner = (r == 0 || r == m_rows - 1) && (c == 0 || c == m_cols - 1);
            const QPoint wp = toWidgetCoords(m_points[i]);
            p.setPen(Qt::NoPen);
            p.setBrush(isCorner ? QColor(0, 200, 100) : QColor(0, 160, 255));
            p.drawEllipse(wp, 5, 5);
            p.setBrush(Qt::white);
            p.drawEllipse(wp, 2, 2);
        }
    }
    
    // 提示（画在图片下方）
    drawCanvasTip(p, m_cornerMode
        ? tr("拖拽绿色四角锚点适配纸面 | 点「精细调整」微调内部")
        : tr("拖拽蓝色锚点微调 | 绿色=四角 | %1×%2 网格").arg(m_rows).arg(m_cols));
}

void CalibrationDialog::mousePressEvent(QMouseEvent* ev) {
    if (ev->button() != Qt::LeftButton) return;
    const QPoint pos = ev->pos();
    if (!m_drawRect.contains(pos)) return;   // 只在图片区域内响应
    
    m_dragIdx = -1;
    const int total = m_rows * m_cols;
    for (int i = 0; i < total; ++i) {
        if (m_cornerMode && i != 0 && i != 1 && i != 2 && i != 3) continue;
        const QPoint wp = toWidgetCoords(m_points[i]);
        if ((pos - wp).manhattanLength() < 14) {
            m_dragIdx = i;
            return;
        }
    }
}

void CalibrationDialog::mouseMoveEvent(QMouseEvent* ev) {
    if (m_dragIdx < 0) return;
    const QPoint pos = ev->pos();
    if (!m_drawRect.contains(pos)) return;
    m_points[m_dragIdx] = toImageCoords(pos);
    update();
}

void CalibrationDialog::mouseReleaseEvent(QMouseEvent*) {
    m_dragIdx = -1;
}

//=============================================================================
// 横线导引对话框（作业本横线）
//=============================================================================
// 设计要点：
//   · 纸张弯曲 → 横线是曲线，只能手绘（鼠标按住拖动采点）
//   · 为降低操作成本，只要求描 2 条关键曲线（首、尾），中间按弧长参数插值
//   · 关键曲线按「垂直位置」排序后插值，所以用户画的先后顺序无关紧要
//   · 采样按像素间距抽稀，松手后做移动平均平滑

namespace {

// 采点抽稀间距（图片坐标下的像素，会按当前缩放换算）
constexpr qreal kSampleMinDist = 3.0;
// 端点命中半径（widget 像素）
constexpr int kHandleHitRadius = 12;
// 放大镜边长与放大倍数
constexpr int kMagnifierSize = 132;
constexpr qreal kMagnifierZoom = 3.0;

QPointF curveCentroidY(const GuideCurve& c) {
    if (c.pts.empty()) return QPointF();
    qreal sum = 0.0;
    for (const QPointF& p : c.pts) sum += p.y();
    return QPointF(0, sum / static_cast<qreal>(c.pts.size()));
}

} // namespace

LineGuideDialog::LineGuideDialog(const QString& imagePath, const LineGuideSet& guides, QWidget* parent)
    : ImageCanvasDialog(parent) {
    setWindowTitle(tr("横线导引 — 沿作业本上的横线拖动描线"));
    resize(940, 760);

    if (!loadCanvasImage(imagePath)) {
        QMetaObject::invokeMethod(this, [this]() { reject(); }, Qt::QueuedConnection);
        return;
    }

    // 恢复已有配置
    m_keyCurves  = guides.keyCurves;
    m_lineCount  = qBound(2, guides.lineCount, 200);
    m_interpolate = guides.useInterpolation;

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    installCanvas(root, 300);

    // ── 操作栏 ──
    auto* bar = new QWidget(this);
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(5);

    auto mkBtn = [&](const QString& text, const QString& tip) {
        auto* b = new QPushButton(text, bar);
        b->setToolTip(tip);
        bl->addWidget(b);
        return b;
    };

    m_btnFirst  = mkBtn(tr("画第 1 条"),   tr("在照片上按住鼠标，沿最上面那条横线拖动"));
    m_btnLast   = mkBtn(tr("画最后 1 条"), tr("在照片上按住鼠标，沿最下面那条横线拖动"));
    m_btnExtra  = mkBtn(tr("+ 关键线"),    tr("再补一条关键曲线（纸张中间鼓/凹时用，会变成分段插值）"));
    bl->addSpacing(8);
    m_btnRedraw = mkBtn(tr("重画选中"),    tr("删除选中的关键曲线并重新描一条"));
    m_btnDelete = mkBtn(tr("删除选中"),    tr("删除选中的关键曲线（Delete 键同效）"));
    m_btnUndo   = mkBtn(tr("撤销"),        tr("回退上一步曲线编辑"));
    m_btnClear  = mkBtn(tr("清空"),        tr("清空所有关键曲线"));

    bl->addStretch();
    m_lblStatus = new QLabel(bar);
    m_lblStatus->setMinimumWidth(260);
    m_lblStatus->setStyleSheet(QStringLiteral("color:#9cf;"));
    bl->addWidget(m_lblStatus);

    root->addWidget(bar, 0);

    // ── 参数栏 ──
    auto* pbar = new QWidget(this);
    auto* pl = new QHBoxLayout(pbar);
    pl->setContentsMargins(0, 0, 0, 0);
    pl->setSpacing(8);

    pl->addWidget(new QLabel(tr("条数:"), pbar));
    m_spinCount = new QSpinBox(pbar);
    m_spinCount->setRange(2, 200);
    m_spinCount->setValue(m_lineCount);
    m_spinCount->setToolTip(tr("整页横线的总条数（含首尾关键曲线）。中间条数由插值生成"));
    pl->addWidget(m_spinCount);

    m_checkInterp = new QCheckBox(tr("关键曲线间自动插值"), pbar);
    m_checkInterp->setChecked(m_interpolate);
    m_checkInterp->setToolTip(tr("勾选：只描 2~3 条，其余自动生成\n不勾：手绘几条就用几条"));
    pl->addWidget(m_checkInterp);

    pl->addSpacing(10);
    pl->addWidget(new QLabel(tr("基线位置:"), pbar));
    m_sliderRatio = new QSlider(Qt::Horizontal, pbar);
    m_sliderRatio->setRange(0, 100);
    m_sliderRatio->setValue(static_cast<int>(guides.baselineRatio * 100));
    m_sliderRatio->setMinimumWidth(140);
    m_sliderRatio->setToolTip(tr("0 = 字贴在两条横线中的上一条\n100 = 贴在下一条\n手写习惯一般在 80 左右"));
    pl->addWidget(m_sliderRatio);
    m_lblRatio = new QLabel(QString::number(guides.baselineRatio, 'f', 2), pbar);
    m_lblRatio->setMinimumWidth(34);
    pl->addWidget(m_lblRatio);

    pl->addWidget(new QLabel(tr("微调:"), pbar));
    m_spinOffset = new QSpinBox(pbar);
    m_spinOffset->setRange(-200, 200);
    m_spinOffset->setValue(guides.baselineOffset);
    m_spinOffset->setSuffix(tr(" px"));
    pl->addWidget(m_spinOffset);

    pl->addSpacing(10);
    m_checkFollow = new QCheckBox(tr("逐字跟随弯曲"), pbar);
    m_checkFollow->setChecked(guides.followCurve);
    m_checkFollow->setToolTip(tr("勾选：行内每个字按曲线定位并跟随切线角旋转\n不勾：整行拉平（弯曲过度导致字形怪异时可关）"));
    pl->addWidget(m_checkFollow);

    pl->addWidget(new QLabel(tr("每行占"), pbar));
    m_spinPerRow = new QSpinBox(pbar);
    m_spinPerRow->setRange(1, 4);
    m_spinPerRow->setValue(qBound(1, guides.linesPerRow, 4));
    m_spinPerRow->setToolTip(tr("一行文字占几条横线的高度。线距小于字号时调大"));
    pl->addWidget(m_spinPerRow);
    pl->addWidget(new QLabel(tr("条线"), pbar));

    pl->addStretch();

    root->addWidget(pbar, 0);

    // ── 提示条 ──
    auto* hint = new QLabel(this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#aaa; background:#2a2a2a; border-radius:4px; padding:4px 8px;"));
    hint->setText(tr("提示：先用「锚点」标定页面四角，再回来画横线。"
                     "纸张弯曲时横线是弯的 —— 按住鼠标沿横线拖过去即可，松手会自动平滑。"
                     "只需画第 1 条和最后 1 条，中间自动插值。"));
    root->addWidget(hint, 0);

    // ── 确定 / 取消 ──
    auto* okBar = new QWidget(this);
    auto* ol = new QHBoxLayout(okBar);
    ol->setContentsMargins(0, 0, 0, 0);
    ol->addStretch();
    auto* btnOk = new QPushButton(tr("确定"), okBar);
    btnOk->setDefault(true);
    auto* btnCancel = new QPushButton(tr("取消"), okBar);
    ol->addWidget(btnOk);
    ol->addWidget(btnCancel);
    root->addWidget(okBar, 0);

    // ── 连接 ──
    connect(m_btnFirst, &QPushButton::clicked, this, [this]() { beginDraw(-1); });
    connect(m_btnLast,  &QPushButton::clicked, this, [this]() { beginDraw(-1); });
    connect(m_btnExtra, &QPushButton::clicked, this, [this]() { beginDraw(-1); });
    connect(m_btnRedraw, &QPushButton::clicked, this, [this]() {
        if (m_selCurve < 0) { m_lblStatus->setText(tr("请先在图中点选一条关键曲线")); return; }
        beginDraw(m_selCurve);
    });
    connect(m_btnDelete, &QPushButton::clicked, this, [this]() {
        if (m_selCurve < 0) { m_lblStatus->setText(tr("请先在图中点选一条关键曲线")); return; }
        pushUndo();
        m_keyCurves.erase(m_keyCurves.begin() + m_selCurve);
        m_selCurve = -1;
        refresh();
    });
    connect(m_btnClear, &QPushButton::clicked, this, [this]() {
        if (m_keyCurves.empty()) return;
        pushUndo();
        m_keyCurves.clear();
        m_selCurve = -1;
        refresh();
    });
    connect(m_btnUndo, &QPushButton::clicked, this, [this]() {
        if (m_undo.empty()) { m_lblStatus->setText(tr("没有可撤销的操作")); return; }
        m_keyCurves = m_undo.back();
        m_undo.pop_back();
        m_selCurve = -1;
        m_drawing = false;
        refresh();
    });

    connect(m_spinCount, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        m_lineCount = v; refresh();
    });
    connect(m_checkInterp, &QCheckBox::toggled, this, [this](bool v) {
        m_interpolate = v; refresh();
    });
    connect(m_sliderRatio, &QSlider::valueChanged, this, [this](int v) {
        m_lblRatio->setText(QString::number(v / 100.0, 'f', 2));
    });
    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);

    refresh();
}

void LineGuideDialog::pushUndo() {
    m_undo.push_back(m_keyCurves);
    // 限制栈深度，避免手绘很多条时内存无谓增长
    if (m_undo.size() > 32) m_undo.erase(m_undo.begin());
}

void LineGuideDialog::sortKeyCurves() {
    // 插值按「上→下」顺序在两两之间进行，所以必须按垂直位置排序，
    // 用户描线的先后顺序不能决定它属于哪一段。
    std::stable_sort(m_keyCurves.begin(), m_keyCurves.end(),
                     [](const GuideCurve& a, const GuideCurve& b) {
                         return curveCentroidY(a).y() < curveCentroidY(b).y();
                     });
}

void LineGuideDialog::refresh() {
    sortKeyCurves();

    LineGuideSet tmp;
    tmp.enabled = true;
    tmp.keyCurves = m_keyCurves;
    tmp.useInterpolation = m_interpolate;
    tmp.lineCount = qMax(2, m_lineCount);
    m_preview = tmp.isValid() ? tmp.build() : std::vector<GuideCurve>();

    updateUI();
    update();
}

void LineGuideDialog::updateUI() {
    const int n = static_cast<int>(m_keyCurves.size());
    m_btnDelete->setEnabled(m_selCurve >= 0);
    m_btnRedraw->setEnabled(m_selCurve >= 0);
    m_btnUndo->setEnabled(!m_undo.empty());
    m_btnClear->setEnabled(n > 0);
    m_spinCount->setEnabled(m_interpolate);

    if (m_drawing) {
        m_lblStatus->setText(tr("正在描线：按住鼠标沿横线拖动，松开结束"));
    } else if (n == 0) {
        m_lblStatus->setText(tr("还没有曲线 — 点「画第 1 条」开始"));
    } else if (m_interpolate && n < 2) {
        m_lblStatus->setText(tr("已画 %1 条（插值需 2 条）— 点「画最后 1 条」").arg(n));
    } else {
        m_lblStatus->setText(tr("关键曲线 %1 条 → 整页 %2 条横线")
                             .arg(n).arg(static_cast<int>(m_preview.size())));
    }
}

void LineGuideDialog::beginDraw(int replaceIndex) {
    m_drawing = true;
    m_replaceIndex = replaceIndex;
    m_drawPts.clear();
    if (replaceIndex < 0) m_selCurve = -1;
    updateUI();
    update();
}

int LineGuideDialog::hitCurve(const QPoint& widgetPos, int* endpoint) const {
    if (endpoint) *endpoint = -1;
    int best = -1;
    int bestDist = kHandleHitRadius + 1;

    // 先判端点（优先级高，方便微调）
    for (size_t i = 0; i < m_keyCurves.size(); ++i) {
        const auto& pts = m_keyCurves[i].pts;
        if (pts.size() < 2) continue;
        const int d0 = (widgetPos - toWidgetCoords(pts.front())).manhattanLength();
        const int d1 = (widgetPos - toWidgetCoords(pts.back())).manhattanLength();
        if (d0 < bestDist) { bestDist = d0; best = static_cast<int>(i); if (endpoint) *endpoint = 0; }
        if (d1 < bestDist) { bestDist = d1; best = static_cast<int>(i); if (endpoint) *endpoint = 1; }
    }
    if (best >= 0) return best;

    // 再判曲线本体（按 x 采样出该位置的 y，比较垂直距离）
    const QPointF ip = toImageCoords(widgetPos);
    int bestCurve = -1;
    qreal bestDy = 1e18;
    for (size_t i = 0; i < m_keyCurves.size(); ++i) {
        const GuideCurve& c = m_keyCurves[i];
        if (!c.usable()) continue;
        const qreal dy = std::abs(c.yAt(ip.x()) - ip.y());
        if (dy < bestDy) { bestDy = dy; bestCurve = static_cast<int>(i); }
    }
    // 换算成 widget 像素再和阈值比较
    const qreal pixPerImgY = (m_image.height() > 0)
        ? static_cast<qreal>(m_drawRect.height()) / m_image.height() : 1.0;
    if (bestCurve >= 0 && bestDy * pixPerImgY <= kHandleHitRadius) return bestCurve;
    return -1;
}

void LineGuideDialog::mousePressEvent(QMouseEvent* ev) {
    if (ev->button() != Qt::LeftButton) return;
    const QPoint pos = ev->pos();
    if (!inCanvas(pos)) return;

    if (m_drawing) {
        m_drawPts.clear();
        m_drawPts.push_back(toImageCoords(pos));
        update();
        return;
    }

    int ep = -1;
    const int hit = hitCurve(pos, &ep);
    m_selCurve = hit;
    if (hit >= 0) {
        pushUndo();
        m_dragSnapshot = m_keyCurves[static_cast<size_t>(hit)].pts;
        m_dragGrab = toImageCoords(pos);
        m_dragEndpoint = ep;
        m_drag = (ep >= 0) ? Drag::Endpoint : Drag::Curve;
    } else {
        m_drag = Drag::None;
    }
    updateUI();
    update();
}

void LineGuideDialog::mouseMoveEvent(QMouseEvent* ev) {
    m_mousePos = ev->pos();
    m_mouseIn = inCanvas(m_mousePos);

    if (m_drawing && (ev->buttons() & Qt::LeftButton) && m_mouseIn) {
        const QPointF ip = toImageCoords(m_mousePos);
        // 抽稀：距上一个采样点太近就不记，避免点过密导致平滑失效
        if (m_drawPts.empty() ||
            QLineF(m_drawPts.back(), ip).length() >= kSampleMinDist) {
            m_drawPts.push_back(ip);
        }
        update();
        return;
    }

    if (m_drag == Drag::None || (!(ev->buttons() & Qt::LeftButton))) {
        if (m_mouseIn) update();   // 放大镜跟随
        return;
    }

    if (m_selCurve < 0) { m_drag = Drag::None; return; }
    auto& pts = m_keyCurves[static_cast<size_t>(m_selCurve)].pts;
    if (pts.size() != m_dragSnapshot.size()) { m_drag = Drag::None; return; }

    const QPointF now = toImageCoords(m_mousePos);
    const QPointF delta = now - m_dragGrab;

    if (m_drag == Drag::Endpoint && m_dragEndpoint >= 0) {
        if (m_dragEndpoint == 0) {
            // 拖左端：只动首点，并把 x 限制在第二点之前
            QPointF p = m_dragSnapshot.front();
            p.rx() = qMin(m_dragSnapshot[1].x() - 1.0, p.x() + delta.x());
            p.ry() += delta.y();
            pts.front() = p;
        } else {
            QPointF p = m_dragSnapshot.back();
            p.rx() = qMax(m_dragSnapshot[m_dragSnapshot.size() - 2].x() + 1.0, p.x() + delta.x());
            p.ry() += delta.y();
            pts.back() = p;
        }
    } else {
        // 整体拖动
        for (size_t i = 0; i < pts.size(); ++i) {
            pts[i] = m_dragSnapshot[i] + delta;
        }
    }
    refresh();
}

void LineGuideDialog::mouseReleaseEvent(QMouseEvent*) {
    if (m_drawing) {
        if (m_drawPts.size() >= 2) {
            GuideCurve c;
            c.pts = m_drawPts;
            c.normalize(2);
            if (c.usable()) {
                pushUndo();
                if (m_replaceIndex >= 0 && m_replaceIndex < static_cast<int>(m_keyCurves.size())) {
                    m_keyCurves[static_cast<size_t>(m_replaceIndex)] = c;
                    m_selCurve = m_replaceIndex;
                } else {
                    m_keyCurves.push_back(c);
                    m_selCurve = static_cast<int>(m_keyCurves.size()) - 1;
                }
            }
        }
        m_drawing = false;
        m_replaceIndex = -1;
        m_drawPts.clear();
        refresh();
        return;
    }

    if (m_drag != Drag::None) {
        m_drag = Drag::None;
        m_dragEndpoint = -1;
        m_dragSnapshot.clear();
        refresh();
    }
}

void LineGuideDialog::keyPressEvent(QKeyEvent* ev) {
    if ((ev->key() == Qt::Key_Delete || ev->key() == Qt::Key_Backspace) && m_selCurve >= 0) {
        pushUndo();
        m_keyCurves.erase(m_keyCurves.begin() + m_selCurve);
        m_selCurve = -1;
        refresh();
        return;
    }
    if (ev->key() == Qt::Key_Escape && m_drawing) {
        m_drawing = false;
        m_drawPts.clear();
        updateUI();
        update();
        return;
    }
    QDialog::keyPressEvent(ev);
}

void LineGuideDialog::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(40, 40, 40));
    if (m_scaledPixmap.isNull()) return;
    p.drawPixmap(m_drawRect.topLeft(), m_scaledPixmap);

    p.setRenderHint(QPainter::Antialiasing);

    auto drawCurve = [&](const GuideCurve& c, const QColor& color, qreal width) {
        if (!c.usable()) return;
        QPolygonF poly;
        poly.reserve(static_cast<int>(c.pts.size()));
        for (const QPointF& pt : c.pts) poly << toWidgetCoords(pt);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(color, width));
        p.drawPolyline(poly);
    };

    // 1) 插值出来的预览曲线（半透明红）
    for (const GuideCurve& c : m_preview) drawCurve(c, QColor(235, 90, 90, 110), 1.0);

    // 2) 用户手绘的关键曲线（绿；选中的亮黄加粗）
    for (size_t i = 0; i < m_keyCurves.size(); ++i) {
        const bool sel = (static_cast<int>(i) == m_selCurve);
        drawCurve(m_keyCurves[i], sel ? QColor(255, 215, 0) : QColor(0, 210, 130), sel ? 2.6 : 1.8);

        // 端点手柄
        const auto& pts = m_keyCurves[i].pts;
        if (pts.size() >= 2) {
            for (int k = 0; k < 2; ++k) {
                const QPoint wp = toWidgetCoords(k == 0 ? pts.front() : pts.back());
                p.setPen(Qt::NoPen);
                p.setBrush(sel ? QColor(255, 215, 0) : QColor(0, 210, 130));
                p.drawEllipse(wp, 5, 5);
                p.setBrush(Qt::white);
                p.drawEllipse(wp, 2, 2);
            }
        }
    }

    // 3) 正在描的线
    if (m_drawing && m_drawPts.size() >= 2) {
        QPolygonF poly;
        for (const QPointF& pt : m_drawPts) poly << toWidgetCoords(pt);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 215, 0), 2.0, Qt::DashLine));
        p.drawPolyline(poly);
    }

    // 4) 描线时的水平参考线，帮助用户画得平
    if (m_drawing && !m_drawPts.empty()) {
        const QPoint wp = toWidgetCoords(m_drawPts.front());
        p.setPen(QPen(QColor(255, 255, 255, 60), 1, Qt::DotLine));
        p.drawLine(m_drawRect.left(), wp.y(), m_drawRect.right(), wp.y());
    }

    // 5) 放大镜：弯曲的线在缩略图上看不真切，画线/拖拽时给个 3× 局部放大
    if (m_mouseIn && (m_drawing || m_drag != Drag::None) &&
        m_image.width() > 0 && m_image.height() > 0) {
        const QPointF ip = toImageCoords(m_mousePos);
        // 图片坐标 -> 缩放位图坐标
        const qreal sx = static_cast<qreal>(m_scaledPixmap.width())  / m_image.width();
        const qreal sy = static_cast<qreal>(m_scaledPixmap.height()) / m_image.height();
        const qreal srcSide = kMagnifierSize / kMagnifierZoom;      // 图片坐标下的边长
        QRect srcRect(static_cast<int>(ip.x() * sx - srcSide * sx / 2.0),
                      static_cast<int>(ip.y() * sy - srcSide * sy / 2.0),
                      qMax(1, static_cast<int>(srcSide * sx)),
                      qMax(1, static_cast<int>(srcSide * sy)));
        srcRect = srcRect.intersected(m_scaledPixmap.rect());

        QRect magRect(m_mousePos.x() + 24, m_mousePos.y() + 24, kMagnifierSize, kMagnifierSize);
        if (magRect.right()  > m_drawRect.right())  magRect.moveLeft(m_mousePos.x() - 24 - kMagnifierSize);
        if (magRect.bottom() > m_drawRect.bottom()) magRect.moveTop(m_mousePos.y() - 24 - kMagnifierSize);

        if (!srcRect.isEmpty()) {
            p.setPen(QPen(QColor(255, 215, 0, 200), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(magRect.adjusted(-1, -1, 1, 1));
            p.drawPixmap(magRect, m_scaledPixmap, srcRect);
        }
    }

    drawCanvasTip(p, m_drawing
        ? tr("按住左键沿横线拖动 · 松开结束 · Esc 取消")
        : tr("拖动绿色关键曲线两端的圆点可微调 · 点曲线本体可整体平移 · Delete 删除选中"));
}

LineGuideSet LineGuideDialog::getGuides() const {
    LineGuideSet g;
    g.enabled = !m_keyCurves.empty();
    g.keyCurves = m_keyCurves;
    g.lineCount = qMax(2, m_lineCount);
    g.useInterpolation = m_interpolate;
    g.baselineRatio = m_sliderRatio ? m_sliderRatio->value() / 100.0 : 0.82;
    g.baselineOffset = m_spinOffset ? m_spinOffset->value() : 0;
    g.followCurve = m_checkFollow ? m_checkFollow->isChecked() : true;
    g.linesPerRow = m_spinPerRow ? m_spinPerRow->value() : 1;
    return g;
}

//=============================================================================
// 缩放
//=============================================================================

void MainWindow::onPushButtonZoomInClicked(){m_zoomFactor=qMin(m_zoomFactor+ZOOM_STEP,ZOOM_MAX);applyZoom();updateZoomDisplay();}
void MainWindow::onPushButtonZoomOutClicked(){m_zoomFactor=qMax(m_zoomFactor-ZOOM_STEP,ZOOM_MIN);applyZoom();updateZoomDisplay();}
void MainWindow::onPushButtonZoomResetClicked(){m_zoomFactor=1.0;applyZoom();updateZoomDisplay();}
void MainWindow::onSliderZoomValueChanged(int v){m_zoomFactor=v/100.0;applyZoom();}
void MainWindow::updateZoomDisplay() {
    ui->labelZoom->setText(QString("%1%").arg(static_cast<int>(m_zoomFactor*100)));
    ui->sliderZoom->blockSignals(true); ui->sliderZoom->setValue(static_cast<int>(m_zoomFactor*100)); ui->sliderZoom->blockSignals(false);
}
void MainWindow::applyZoom(){if(m_pixmapItem){ui->imgPreview->resetTransform();ui->imgPreview->scale(m_zoomFactor,m_zoomFactor);}}

//=============================================================================
// 事件处理
//=============================================================================

void MainWindow::keyPressEvent(QKeyEvent* e) {
    switch(e->key()){
    case Qt::Key_Left:case Qt::Key_PageUp:onPushButtonPrevPageClicked();break;
    case Qt::Key_Right:case Qt::Key_PageDown:onPushButtonNextPageClicked();break;
    case Qt::Key_Home:onPushButtonFirstPageClicked();break;
    case Qt::Key_End:onPushButtonLastPageClicked();break;
    default:QMainWindow::keyPressEvent(e);
    }
}

void MainWindow::closeEvent(QCloseEvent* e) {
    bool hrt=(m_previewWatcher&&m_previewWatcher->isRunning())||(m_exportWatcher&&m_exportWatcher->isRunning());
    if(hrt){if(QMessageBox::question(this,tr("确认关闭"),tr("正在处理中，确定关闭?"))==QMessageBox::No){e->ignore();return;}
    if(m_previewWatcher&&m_previewWatcher->isRunning()){m_previewWatcher->cancel();m_previewWatcher->waitForFinished();}
    if(m_exportWatcher&&m_exportWatcher->isRunning()){m_exportWatcher->cancel();m_exportWatcher->waitForFinished();}
    QCoreApplication::processEvents();}
    if(m_progressDialog){m_progressDialog->close();delete m_progressDialog;m_progressDialog=nullptr;}
    m_previewImages.clear(); m_previewImages.shrink_to_fit(); m_previewImagePaths.clear();
    if(m_scene)m_scene->clear();
    e->accept();
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    if(obj==ui->imgPreview&&event->type()==QEvent::Wheel){
        auto* we=static_cast<QWheelEvent*>(event);
        if(we->modifiers()&Qt::ControlModifier){
            QPoint d=we->angleDelta();
            if(d.y()>0)m_zoomFactor=qMin(m_zoomFactor+ZOOM_STEP,ZOOM_MAX);
            else m_zoomFactor=qMax(m_zoomFactor-ZOOM_STEP,ZOOM_MIN);
            applyZoom();updateZoomDisplay();return true;
        }
    }
    if(obj==ui->textEditMain&&event->type()==QEvent::KeyPress){
        auto* ke=static_cast<QKeyEvent*>(event);
        if((ke->key()==Qt::Key_V&&ke->modifiers()&Qt::ControlModifier)||(ke->key()==Qt::Key_Insert&&ke->modifiers()&Qt::ShiftModifier)){
            auto* cb=QGuiApplication::clipboard();
            if(cb->mimeData()->hasText()){ui->textEditMain->insertPlainText(cb->mimeData()->text());return true;}
        }
    }
    return QMainWindow::eventFilter(obj,event);
}

//=============================================================================
// 拖放背景图片
//=============================================================================

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            QString path = url.toLocalFile();
            QImageReader reader(path);
            if (reader.canRead()) {
                m_bgImagePath = path;
                m_bgCalibration.enabled = false;
                m_labelBgImage->setText(QFileInfo(path).fileName());
                onParameterChanged();
                return;
            }
        }
    }
}

void MainWindow::setupProgressDialog(const QString& title, int maximum) {
    if(m_progressDialog)delete m_progressDialog;
    m_progressDialog=new QProgressDialog(title,tr("取消"),0,maximum,this);
    m_progressDialog->setWindowModality(Qt::WindowModal);
    m_progressDialog->setMinimumDuration(0); m_progressDialog->setValue(0);
    // 保留取消按钮：旧实现用 setCancelButton(nullptr) 把它禁掉了，
    // 高分辨率导出时用户除了强杀进程别无选择
    connect(m_progressDialog,&QProgressDialog::canceled,this,&MainWindow::requestCancel);
    m_progressDialog->show();
}

//=============================================================================
// 菜单操作
//=============================================================================

void MainWindow::onMenuFileNew() {
    ui->textEditMain->clear();
    onParameterChanged();
}

void MainWindow::onMenuFileOpen() {
    QString path = QFileDialog::getOpenFileName(this, tr("打开文本文件"), "",
        tr("文本文件 (*.txt *.md);;所有文件 (*)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        ui->textEditMain->setPlainText(QString::fromUtf8(f.readAll()));
        onParameterChanged();
    }
}

void MainWindow::onMenuFileSave() {
    QString path = QFileDialog::getSaveFileName(this, tr("保存文本"), "text.txt",
        tr("文本文件 (*.txt);;Markdown (*.md);;所有文件 (*)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        f.write(ui->textEditMain->toPlainText().toUtf8());
    }
}

void MainWindow::showAboutDialog() {
    QMessageBox about(this);
    about.setWindowTitle(tr("关于 HandWrite Generator"));
    // 优先用资源图标；资源缺失时退回窗口图标（不再依赖不存在的 qrc 路径）
    QPixmap icon(QStringLiteral(":/resources/app.ico"));
    if (icon.isNull()) icon = windowIcon().pixmap(64, 64);
    if (icon.isNull()) {
        const QPixmap fileIcon(QCoreApplication::applicationDirPath() + "/app.ico");
        if (!fileIcon.isNull()) icon = fileIcon;
    }
    if (!icon.isNull())
        about.setIconPixmap(icon.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    about.setTextFormat(Qt::RichText);
    about.setText(QString(
        "<h3>HandWrite Generator " + QApplication::applicationVersion() + "</h3>"
        "<p>手写作业生成器 — 将电子文本渲染为模拟手写效果</p>"
        "<p>作者: <b>XEKernel</b></p>"
        "<p>代码仓库: <a href='https://github.com/XEKernel/HandWrite-CPP'>github.com/XEKernel/HandWrite-CPP</a></p>"
        "<hr>"
        "<p style='color: gray;'>构建: Qt %1 &middot; GCC %2 &middot; MSYS2 ucrt64</p>"
    ).arg(QT_VERSION_STR).arg(__VERSION__));
    about.setStandardButtons(QMessageBox::Ok);
    about.exec();
}

} // namespace HandWrite
