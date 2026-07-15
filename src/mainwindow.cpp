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

namespace HandWrite {

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
    
    QDoubleSpinBox *fs; QSpinBox *fs2;
    auto *fsLay = new QHBoxLayout(); m_fontSizeCheck = new QCheckBox(tr("覆盖"), this);
    fs2 = new QSpinBox(this); fs2->setRange(1, 200); fs2->setValue(30); fs2->setEnabled(false);
    fsLay->addWidget(m_fontSizeCheck); fsLay->addWidget(fs2); fsLay->addStretch();
    formLayout->addRow(tr("字体大小:"), fsLay);
    connect(m_fontSizeCheck, &QCheckBox::toggled, fs2, &QSpinBox::setEnabled);
    m_fontSizeSpin = reinterpret_cast<QSpinBox*>(fs2);
    
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
      m_pixmapItem(nullptr), m_previewWatcher(new QFutureWatcher<std::vector<QImage>>(this)),
      m_exportWatcher(new QFutureWatcher<std::map<int,std::string>>(this)), m_progressDialog(nullptr) {
    ui->setupUi(this);
    
    // 菜单栏
    auto* fileMenu = menuBar()->addMenu(tr("文件(&F)"));
    auto* actNew = fileMenu->addAction(tr("新建(&N)"), this, &MainWindow::onMenuFileNew);
    actNew->setShortcut(QKeySequence::New);
    fileMenu->addAction(tr("打开文本(&O)..."), this, &MainWindow::onMenuFileOpen, QKeySequence::Open);
    fileMenu->addAction(tr("保存文本(&S)"), this, &MainWindow::onMenuFileSave, QKeySequence::Save);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("退出(&X)"), this, &MainWindow::close, QKeySequence::Quit);
    
    auto* presetMenu = menuBar()->addMenu(tr("预设(&P)"));
    presetMenu->addAction(tr("保存预设..."), this, &MainWindow::onPushButtonSaveConfigClicked, QKeySequence("Ctrl+Shift+S"));
    presetMenu->addAction(tr("加载预设..."), this, &MainWindow::onPushButtonLoadConfigClicked, QKeySequence("Ctrl+Shift+O"));
    presetMenu->addSeparator();
    presetMenu->addAction(tr("导出 PNG..."), this, &MainWindow::onPushButtonExportClicked, QKeySequence("Ctrl+E"));
    presetMenu->addAction(tr("导出 PDF..."), this, &MainWindow::onPushButtonExportPdfClicked, QKeySequence("Ctrl+P"));
    
    auto* helpMenu = menuBar()->addMenu(tr("帮助(&H)"));
    helpMenu->addAction(tr("关于(&A)"), this, &MainWindow::showAboutDialog);
    
    ui->imgPreview->setScene(m_scene);
    ui->imgPreview->setDragMode(QGraphicsView::ScrollHandDrag);
    ui->imgPreview->setRenderHint(QPainter::Antialiasing);
    ui->imgPreview->setRenderHint(QPainter::SmoothPixmapTransform);
    ui->imgPreview->installEventFilter(this);
    ui->textEditMain->installEventFilter(this);
    
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
    
    connect(m_previewWatcher, &QFutureWatcher<std::vector<QImage>>::finished, this, &MainWindow::onPreviewFinished);
    connect(m_exportWatcher, &QFutureWatcher<std::map<int,std::string>>::finished, this, &MainWindow::onExportFinished);
    connect(m_exportWatcher, &QFutureWatcher<std::map<int,std::string>>::finished, this, &MainWindow::onExportFinished);
    
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
    m_labelBgImage = new QLabel(tr("未设置"), bgGroup);
    m_labelBgImage->setWordWrap(true);
    bgLayout->addWidget(btnSelectBg);
    bgLayout->addWidget(btnClearBg);
    bgLayout->addWidget(btnCalibrateBg);
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
    
    // PDF按钮（添加到导出布局）
    auto *pdfBtn = new QPushButton(tr("导出PDF"), scrollContent);
    connect(pdfBtn, &QPushButton::clicked, this, &MainWindow::onPushButtonExportPdfClicked);
    
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
    infoLayout->insertWidget(insertPos++, presetGroup);
    // PDF按钮插入到导出按钮旁
    for(int i=0;i<infoLayout->count();++i){
        auto* item = infoLayout->itemAt(i);
        if(item && item->layout()){
            auto* hl = qobject_cast<QHBoxLayout*>(item->layout());
            if(hl && hl->objectName()=="exportLayout"){
                hl->addWidget(pdfBtn); break;
            }
        }
    }
    
    refreshPresetList();
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
    for(const auto& n:m_cachedFontNames) ui->comboBoxFont->addItem(QString::fromStdString(n));
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
    m_exportRate=params.rate;
    int previewRate=qMin(params.rate,4);
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
    p.paragraphIndent=m_checkParagraphIndent->isChecked();
    p.paragraphSpacing=m_spinParagraphSpacing->value();
    p.textDirection = (m_comboTextDirection->currentIndex() == 1) ? TextDirection::Vertical : TextDirection::Horizontal;
    p.inkBleed=m_checkInkBleed->isChecked();
    p.inkBleedRadius=m_spinInkBleedRadius->value();
    p.strikeThroughRate=m_spinStrikeThroughRate->value();
    p.strokeWidthSigma=m_spinStrokeWidthSigma->value();
    p.charOverrides=m_charOverrides;
    return p;
}

QString MainWindow::getTextFromTextEdit() { return ui->textEditMain->toPlainText(); }

//=============================================================================
// 预览/导出
//=============================================================================

void MainWindow::updatePreview() { triggerAutoPreview(); }

void MainWindow::onPushButtonPreviewClicked() {
    if(m_previewWatcher->isRunning()||m_exportWatcher->isRunning()){
        QMessageBox::warning(this,tr("请稍候"),tr("正在处理中...")); return;
    }
    TemplateParams p=getParamsFromForm(); QString text=getTextFromTextEdit();
    m_exportRate=p.rate; int pr=qMin(p.rate,4);
    setupProgressDialog(tr("正在生成预览..."));
    m_previewWatcher->setFuture(QtConcurrent::run([this,p,text,pr](){return generatePreviewAsync(p,text,pr);}));
}

std::vector<QImage> MainWindow::generatePreviewAsync(TemplateParams params, QString text, int previewRate) {
    HandwriteGenerator g; params.rate=previewRate; g.modifyTemplateParams(params);
    return g.generatePreviewParallel(text.toStdString());
}

void MainWindow::onPreviewFinished() {
    if(m_progressDialog)m_progressDialog->close();
    if(m_previewWatcher->isCanceled()){ checkPendingPreview(); return; }
    m_previewImages=m_previewWatcher->result();
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
    QString od="outputs"; QDir d(od);
    if(d.exists()){for(const auto& f:d.entryList(QDir::Files))d.remove(f);}else d.mkpath(".");
    TemplateParams p=getParamsFromForm(); QString text=getTextFromTextEdit();
    if(p.rate>=16){if(QMessageBox::question(this,tr("高分辨率"),tr("x%1 可能较慢，继续?").arg(p.rate))!=QMessageBox::Yes)return;}
    setupProgressDialog(tr("正在导出..."));
    m_exportWatcher->setFuture(QtConcurrent::run([this,p,text,od](){return generateExportAsync(p,text,od);}));
}

std::map<int,std::string> MainWindow::generateExportAsync(TemplateParams params, QString text, QString outputDir) {
    HandwriteGenerator g; g.modifyTemplateParams(params);
    return g.generateImageParallel(text.toStdString(),outputDir.toStdString(),0,[this](int cur,int tot){
        QMetaObject::invokeMethod(this,[this,cur,tot](){
            if(m_progressDialog){int tp=tot/2; m_progressDialog->setMaximum(tot); m_progressDialog->setValue(cur);
            QString s=cur<=tp?tr("渲染 %1/%2").arg(cur).arg(tp):tr("保存 %1/%2").arg(cur-tp).arg(tp);
            m_progressDialog->setLabelText(s);}
        },Qt::QueuedConnection);
    });
}

void MainWindow::onExportFinished() {
    if(m_exportWatcher->isCanceled()){if(m_progressDialog)m_progressDialog->close();return;}
    m_previewImagePaths=m_exportWatcher->result();
    int tp=static_cast<int>(m_previewImagePaths.size()),ts=tp*2;
    if(m_progressDialog){m_progressDialog->setMaximum(ts>0?ts:1);m_progressDialog->setValue(ts>0?ts:1);
    m_progressDialog->setLabelText(tr("完成")); QCoreApplication::processEvents(); QThread::msleep(500); m_progressDialog->close();}
    m_totalPages=tp;m_currentPage=0;updatePaginationUI();
    if(!m_previewImagePaths.empty())showImage(QString::fromStdString(m_previewImagePaths[0]));
    QMessageBox msgBox(this); msgBox.setWindowTitle(tr("导出完成"));
    msgBox.setText(tr("已导出 %1 页").arg(tp)); msgBox.setIcon(QMessageBox::Information);
    auto* obtn=msgBox.addButton(tr("打开目录"),QMessageBox::ActionRole); msgBox.addButton(QMessageBox::Ok);
    msgBox.exec();
    if(msgBox.clickedButton()==obtn) QDesktopServices::openUrl(QUrl::fromLocalFile(QDir("outputs").absolutePath()));
}

//=============================================================================
// PDF 导出
//=============================================================================

void MainWindow::onPushButtonExportPdfClicked() {
    if(m_previewWatcher->isRunning()||m_exportWatcher->isRunning()){QMessageBox::warning(this,tr("请稍候"),tr("正在处理中..."));return;}
    QString path=QFileDialog::getSaveFileName(this,tr("导出PDF"),"",tr("PDF Files (*.pdf)"));
    if(path.isEmpty())return;
    TemplateParams p=getParamsFromForm();
    setupProgressDialog(tr("正在生成PDF..."));
    QFuture<bool> future=QtConcurrent::run([this,p,path](){
        HandwriteGenerator g; g.modifyTemplateParams(p);
        return g.exportPdf(getTextFromTextEdit().toStdString(),path.toStdString());
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

//=============================================================================
// 预设管理
//=============================================================================

QString MainWindow::presetDir() const {
    QString d=QCoreApplication::applicationDirPath()+"/presets";
    QDir().mkpath(d); return d;
}

void MainWindow::refreshPresetList() {
    m_presetList->clear();
    QDir d(presetDir());
    for(const auto& f:d.entryList({"*.toml"},QDir::Files)) m_presetList->addItem(f);
}

void MainWindow::onPushButtonPresetSaveClicked() {
    QString name=QInputDialog::getText(this,tr("保存预设"),tr("预设名称:"));
    if(name.isEmpty())return;
    QString path=presetDir()+"/"+name+".toml";
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
    ui->labelCurrentConfig->setText(tr("当前配置文件:\n%1").arg(path));
    onParameterChanged();
}

//=============================================================================
// 按钮槽函数
//=============================================================================

void MainWindow::onPushButtonSaveConfigClicked() {
    QString p=QFileDialog::getSaveFileName(this,tr("保存配置"),"",tr("TOML Files (*.toml)"));
    if(!p.isEmpty())saveConfiguration(p);
}
void MainWindow::onPushButtonLoadConfigClicked() {
    QString p=QFileDialog::getOpenFileName(this,tr("加载配置"),"",tr("TOML Files (*.toml)"));
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
// CalibrationDialog 实现 — 网格形变
//=============================================================================

CalibrationDialog::CalibrationDialog(const QString& imagePath, const BackgroundCalibration& calib, QWidget* parent)
    : QDialog(parent), m_rows(3), m_cols(3) {
    setWindowTitle(tr("网格校准 — 拖拽锚点适应纸面弯曲"));
    
    // 使用统一加载器（含 WebP 回退）
    m_image = loadImageWithWebpFallback(imagePath.toStdString());
    if (m_image.isNull()) {
        QMessageBox::warning(this, tr("错误"), tr("无法加载背景图片（格式不支持或文件损坏）"));
        reject();
        return;
    }
    
    m_scaledPixmap = QPixmap::fromImage(m_image.scaled(760, 520, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    int btnBarHeight = 75;
    setFixedSize(m_scaledPixmap.width() + 20, m_scaledPixmap.height() + btnBarHeight);
    
    // 恢复已有校准
    if (calib.enabled && calib.isValid()) {
        m_rows = calib.rows;
        m_cols = calib.cols;
        m_points = calib.gridPoints;
    } else {
        buildUniformGrid();
    }
    
    // 底部按钮栏
    int btnY = m_scaledPixmap.height() + 8;
    int bw = 28, bh = 28, gap = 5;
    
    auto makeBtn = [&](const QString& text, int x) {
        auto* b = new QPushButton(text, this);
        b->setGeometry(x, btnY, bw, bh);
        b->setFont(QFont("", 10));
        return b;
    };
    
    auto* btnColPlus = makeBtn("+", gap);
    auto* btnColMinus = makeBtn("-", gap + bw + gap);
    auto* lblCol = new QLabel(this);
    lblCol->setText(QString("列:%1").arg(m_cols));
    lblCol->setGeometry(gap + (bw+gap)*2, btnY, 50, bh);
    lblCol->setStyleSheet("color: white;");
    
    auto* btnRowPlus = makeBtn("+", gap + (bw+gap)*2 + 55);
    auto* btnRowMinus = makeBtn("-", gap + (bw+gap)*3 + 55);
    auto* lblRow = new QLabel(this);
    lblRow->setText(QString("行:%1").arg(m_rows));
    lblRow->setGeometry(gap + (bw+gap)*4 + 55, btnY, 50, bh);
    lblRow->setStyleSheet("color: white;");
    
    auto* btnReset = new QPushButton(tr("重置"), this);
    btnReset->setGeometry(gap + (bw+gap)*4 + 115, btnY, 50, bh);
    
    auto* btnOk = new QPushButton(tr("确定"), this);
    btnOk->setGeometry(width() - 90, btnY, 70, bh);
    btnOk->setDefault(true);
    
    connect(btnColPlus, &QPushButton::clicked, this, [this, lblCol]() {
        if (m_cols < 5) { m_cols++; buildUniformGrid(); lblCol->setText(QString("列:%1").arg(m_cols)); update(); }
    });
    connect(btnColMinus, &QPushButton::clicked, this, [this, lblCol]() {
        if (m_cols > 2) { m_cols--; buildUniformGrid(); lblCol->setText(QString("列:%1").arg(m_cols)); update(); }
    });
    connect(btnRowPlus, &QPushButton::clicked, this, [this, lblRow]() {
        if (m_rows < 5) { m_rows++; buildUniformGrid(); lblRow->setText(QString("行:%1").arg(m_rows)); update(); }
    });
    connect(btnRowMinus, &QPushButton::clicked, this, [this, lblRow]() {
        if (m_rows > 2) { m_rows--; buildUniformGrid(); lblRow->setText(QString("行:%1").arg(m_rows)); update(); }
    });
    connect(btnReset, &QPushButton::clicked, this, [this]() { buildUniformGrid(); update(); });
    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
    
    setMouseTracking(true);
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
    c.rows = m_rows;
    c.cols = m_cols;
    c.gridPoints = m_points;
    return c;
}

// 坐标转换保持不变
QPointF CalibrationDialog::toImageCoords(const QPoint& widgetPos) const {
    qreal rx = static_cast<qreal>(m_image.width()) / m_scaledPixmap.width();
    qreal ry = static_cast<qreal>(m_image.height()) / m_scaledPixmap.height();
    return QPointF(widgetPos.x() * rx, widgetPos.y() * ry);
}

QPoint CalibrationDialog::toWidgetCoords(const QPointF& imgPos) const {
    qreal rx = static_cast<qreal>(m_scaledPixmap.width()) / m_image.width();
    qreal ry = static_cast<qreal>(m_scaledPixmap.height()) / m_image.height();
    return QPoint(static_cast<int>(imgPos.x() * rx), static_cast<int>(imgPos.y() * ry));
}

void CalibrationDialog::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(40, 40, 40));
    p.drawPixmap(0, 0, m_scaledPixmap);
    
    // 网格线
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
    
    // 网格点
    for (int i = 0; i < m_rows * m_cols; ++i) {
        QPoint wp = toWidgetCoords(m_points[i]);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 160, 255));
        p.drawEllipse(wp, 5, 5);
        p.setBrush(Qt::white);
        p.drawEllipse(wp, 2, 2);
    }
    
    // 提示
    p.setPen(QColor(200, 200, 200));
    p.setFont(QFont("Microsoft YaHei", 9));
    int h = m_scaledPixmap.height();
    p.drawText(5, h + 40, tr("拖拽蓝色锚点适应纸面弯曲 | %1×%2 网格 | 点击确定").arg(m_rows).arg(m_cols));
}

void CalibrationDialog::mousePressEvent(QMouseEvent* ev) {
    if (ev->button() != Qt::LeftButton) return;
    QPoint pos = ev->pos();
    if (pos.y() > m_scaledPixmap.height()) return;  // 忽略按钮区
    
    m_dragIdx = -1;
    for (int i = 0; i < m_rows * m_cols; ++i) {
        QPoint wp = toWidgetCoords(m_points[i]);
        if ((pos - wp).manhattanLength() < 14) {
            m_dragIdx = i;
            return;
        }
    }
}

void CalibrationDialog::mouseMoveEvent(QMouseEvent* ev) {
    if (m_dragIdx >= 0) {
        QPoint pos = ev->pos();
        if (pos.y() > m_scaledPixmap.height()) return;
        m_points[m_dragIdx] = toImageCoords(pos);
        update();
    }
}

void CalibrationDialog::mouseReleaseEvent(QMouseEvent*) {
    m_dragIdx = -1;
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

void MainWindow::setupProgressDialog(const QString& title, int maximum) {
    if(m_progressDialog)delete m_progressDialog;
    m_progressDialog=new QProgressDialog(title,tr("取消"),0,maximum,this);
    m_progressDialog->setWindowModality(Qt::WindowModal);
    m_progressDialog->setCancelButton(nullptr);
    m_progressDialog->setMinimumDuration(0); m_progressDialog->setValue(0); m_progressDialog->show();
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
    about.setIconPixmap(QPixmap(":/resources/app.ico").scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    about.setTextFormat(Qt::RichText);
    about.setText(QString(
        "<h3>HandWrite Generator v2.4.1</h3>"
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
