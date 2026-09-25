#include "core.hpp"
#include "config.hpp"
#include "tools.hpp"
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <iostream>
#include <fstream>
#include <sstream>

using namespace HandWrite;

static void printUsage() {
    std::cout << R"(HandWrite CLI - 手写作业生成器命令行工具

用法:
  handwrite-cli [选项]

选项:
  -i, --input <文件>       输入文本文件路径（如未指定则从stdin读取）
  -t, --text <文本>        直接指定文本内容
  -o, --output <目录>      输出目录（默认: outputs）
  -p, --preset <预设>      预设配置文件（.conf）
  -r, --rate <倍率>        输出分辨率倍率（1/2/4/8/16/32/64，默认4）
  -f, --format <格式>      输出格式: png / pdf / svg（默认: png）
  -s, --seed <整数>        固定随机种子（同种子保证结果可复现）
  -b, --batch <列表文件>   批量处理: 每行一个输入文件路径
  -h, --help               显示此帮助

示例:
  handwrite-cli -t "今天天气真好" -o ./output -r 2
  handwrite-cli -i essay.txt -p presets/语文作业.conf -f pdf
  handwrite-cli -i essay.txt -f svg -s 20260912 -o ./out
  echo "测试文本" | handwrite-cli -o ./out
)";
}

static std::string readStdin() {
    std::ostringstream oss;
    oss << std::cin.rdbuf();
    return oss.str();
}

static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) { std::cerr << "错误: 无法打开文件 " << path << std::endl; exit(1); }
    std::ostringstream oss; oss << f.rdbuf(); return oss.str();
}

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("HandWrite CLI");
    app.setApplicationVersion(HANDWRITE_VERSION);
    
    QCommandLineParser parser;
    parser.setApplicationDescription("手写作业生成器命令行工具");
    parser.addHelpOption();
    parser.addVersionOption();
    
    QCommandLineOption inputOpt({"i","input"}, "输入文件", "file");
    QCommandLineOption textOpt({"t","text"}, "直接文本", "text");
    QCommandLineOption outputOpt({"o","output"}, "输出目录", "dir", "outputs");
    QCommandLineOption presetOpt({"p","preset"}, "预设配置", "file");
    QCommandLineOption rateOpt({"r","rate"}, "分辨率倍率", "rate", "4");
    QCommandLineOption formatOpt({"f","format"}, "输出格式 (png/pdf/svg)", "fmt", "png");
    QCommandLineOption seedOpt({"s","seed"}, "固定随机种子", "seed");
    QCommandLineOption batchOpt({"b","batch"}, "批量处理: 每行一个文件路径的列表文件", "batchfile");
    
    parser.addOption(inputOpt);
    parser.addOption(textOpt);
    parser.addOption(outputOpt);
    parser.addOption(presetOpt);
    parser.addOption(rateOpt);
    parser.addOption(formatOpt);
    parser.addOption(seedOpt);
    parser.addOption(batchOpt);
    parser.process(app);
    
    // 获取文本
    std::string text;
    if (parser.isSet(textOpt)) {
        text = parser.value(textOpt).toStdString();
    } else if (parser.isSet(inputOpt)) {
        text = readFile(parser.value(inputOpt).toStdString());
    } else {
        text = readStdin();
    }
    
    if (text.empty()) {
        std::cerr << "错误: 未提供文本内容" << std::endl;
        printUsage();
        return 1;
    }
    
    // 构建参数
    HandwriteGenerator generator;
    TemplateParams params = generator.templateParams();
    
    // 加载预设（字段与 GUI 持久化保持一一对应）
    if (parser.isSet(presetOpt)) {
        Config config(parser.value(presetOpt).toStdString());
        if (auto v = config.width()) params.paperWidth = *v;
        if (auto v = config.height()) params.paperHeight = *v;
        if (auto v = config.paperTexture()) {
            static const std::map<std::string, PaperTexture> texMap = {
                {"none", PaperTexture::None}, {"horizontalLine", PaperTexture::HorizontalLine},
                {"grid", PaperTexture::Grid}, {"tianZiGe", PaperTexture::TianZiGe},
                {"composition", PaperTexture::Composition}, {"dotGrid", PaperTexture::DotGrid}
            };
            if (auto it = texMap.find(*v); it != texMap.end()) params.paperTexture = it->second;
        }
        if (auto v = config.textureOpacity()) params.textureOpacity = *v;
        if (auto v = config.backgroundImage()) params.backgroundImagePath = *v;
        if (auto v = config.ttfSelector()) params.fontPath = *v;
        if (auto v = config.fontSize()) params.fontSize = *v;
        if (auto v = config.fontMixList()) params.fontMixList = *v;
        if (auto v = config.fontMixRate()) params.fontMixRate = *v;
        if (auto v = config.lineSpacing()) params.lineSpacing = *v;
        if (auto v = config.charDistance()) params.wordSpacing = *v;
        if (auto v = config.marginTop()) params.topMargin = *v;
        if (auto v = config.marginBottom()) params.bottomMargin = *v;
        if (auto v = config.marginLeft()) params.leftMargin = *v;
        if (auto v = config.marginRight()) params.rightMargin = *v;
        if (auto v = config.paragraphIndent()) params.paragraphIndent = *v;
        if (auto v = config.paragraphSpacing()) params.paragraphSpacing = *v;
        if (auto v = config.textDirection())
            params.textDirection = (*v == 1) ? TextDirection::Vertical : TextDirection::Horizontal;
        if (auto v = config.textWarp()) {
            switch (*v) {
                case 1:  params.textWarp = TextWarp::Arc;    break;
                case 2:  params.textWarp = TextWarp::Wave;   break;
                case 3:  params.textWarp = TextWarp::Circle; break;
                default: params.textWarp = TextWarp::None;   break;
            }
        }
        if (auto v = config.textWarpStrength()) params.textWarpStrength = *v;
        if (auto v = config.lineSpacingSigma()) params.lineSpacingSigma = *v;
        if (auto v = config.fontSizeSigma()) params.fontSizeSigma = *v;
        if (auto v = config.wordSpacingSigma()) params.wordSpacingSigma = *v;
        if (auto v = config.perturbXSigma()) params.perturbXSigma = *v;
        if (auto v = config.perturbYSigma()) params.perturbYSigma = *v;
        if (auto v = config.perturbThetaSigma()) params.perturbThetaSigma = *v;
        if (auto v = config.strokeWidthSigma()) params.strokeWidthSigma = *v;
        if (auto v = config.inkBleed()) params.inkBleed = *v;
        if (auto v = config.inkBleedRadius()) params.inkBleedRadius = *v;
        if (auto v = config.strikeThroughRate()) params.strikeThroughRate = *v;
        if (auto v = config.preserveChinesePunctuation())
            params.preserveChinesePunctuation = *v;
        if (auto v = config.resolution()) params.rate = *v;
        if (auto v = config.charColor()) params.fillColor = Color((*v)[0],(*v)[1],(*v)[2],(*v)[3]);
        if (auto v = config.backgroundColor()) params.backgroundColor = Color((*v)[0],(*v)[1],(*v)[2],(*v)[3]);
        if (auto v = config.seed()) params.seed = *v;
        // 背景图横线导引（作业本横线）
        {
            LineGuideSet& g = params.lineGuides;
            if (auto v = config.lineGuideEnabled()) g.enabled = *v;
            if (auto v = config.lineGuideLineCount()) g.lineCount = *v;
            if (auto v = config.lineGuideInterpolate()) g.useInterpolation = *v;
            if (auto v = config.lineGuideBaselineRatio()) g.baselineRatio = *v;
            if (auto v = config.lineGuideBaselineOffset()) g.baselineOffset = *v;
            if (auto v = config.lineGuideFollowCurve()) g.followCurve = *v;
            if (auto v = config.lineGuideLinesPerRow()) g.linesPerRow = *v;
            if (auto v = config.lineGuideCurves()) {
                g.keyCurves = parseGuideCurves(*v);
                if (g.keyCurves.empty()) g.enabled = false;   // 数据坏了就别启用
            }
        }
        if (auto v = config.charOverrides()) {
            for (const auto& s : *v) {
                if (auto r = HandwriteGenerator::deserializeCharOverride(s))
                    params.charOverrides.push_back(*r);
            }
        }
        // 背景锚点校准
        if (config.bgCalibEnabled().value_or(false)) {
            const int rows = config.bgCalibRows().value_or(3);
            const int cols = config.bgCalibCols().value_or(3);
            if (auto pts = config.bgCalibPoints()) {
                BackgroundCalibration cal;
                cal.rows = rows;
                cal.cols = cols;
                if (static_cast<int>(pts->size()) == rows * cols * 2) {
                    cal.gridPoints.reserve(rows * cols);
                    for (int i = 0; i < rows * cols; ++i)
                        cal.gridPoints.push_back(QPointF((*pts)[2 * i], (*pts)[2 * i + 1]));
                }
                // 注意顺序：isValid() 自身要求 enabled 为真，
                // 所以必须先把 enabled 置真再校验 —— 否则锚点校准在 CLI 里永远不生效
                cal.enabled = true;
                if (!cal.isValid()) cal.enabled = false;
                params.bgCalibration = cal;
            }
        }
    }

    // 命令行倍率覆盖
    if (parser.isSet(rateOpt)) {
        params.rate = parser.value(rateOpt).toInt();
    }

    // 命令行种子覆盖（0 = 每次随机）
    if (parser.isSet(seedOpt)) {
        bool ok = false;
        const uint seedVal = parser.value(seedOpt).toUInt(&ok);
        if (!ok) { std::cerr << "错误: 无效的种子值" << std::endl; return 1; }
        params.seed = seedVal;
    }
    
    // 字体路径
    if (params.fontPath.empty()) {
        BasicTools tools;
        auto ttfs = tools.getTtfFiles();
        if (!ttfs.second.empty()) params.fontPath = ttfs.second[0];
    }
    
    generator.modifyTemplateParams(params);
    
    std::string outputDir = parser.value(outputOpt).toStdString();
    std::string format = parser.value(formatOpt).toStdString();
    
    // 批量模式
    if (parser.isSet(batchOpt)) {
        std::ifstream batchFile(parser.value(batchOpt).toStdString());
        if (!batchFile) { std::cerr << "错误: 无法打开批处理文件" << std::endl; return 1; }
        std::string line;
        int count = 0;
        while (std::getline(batchFile, line)) {
            // 去除 Windows 换行符与首尾空白
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n'
                                     || line.back() == ' ' || line.back() == '\t'))
                line.pop_back();
            while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
                line.erase(line.begin());
            if (line.empty()) continue;
            std::ifstream test(line);
            if (!test) { std::cerr << "警告: 跳过无法打开的文件: " << line << std::endl; continue; }
            std::string itemOutput = outputDir + "/" + std::to_string(count);
            try {
                // 每篇文档用「基础种子 + 序号」，保证同一批次可复现且各篇互不相同
                if (params.seed != 0) {
                    TemplateParams pageParams = params;
                    pageParams.seed = params.seed + static_cast<unsigned int>(count);
                    generator.modifyTemplateParams(pageParams);
                }
                auto result = generator.generateImageParallel(readFile(line), itemOutput);
                std::cout << "[" << count << "] " << line << " -> " << result.size() << " 页" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "[跳过] " << line << ": " << e.what() << std::endl;
            }
            if (params.seed != 0) generator.modifyTemplateParams(params);
            count++;
        }
        std::cout << "批量完成! 共处理 " << count << " 个文件" << std::endl;
        return 0;
    }
    
    // 字体可用性检查：早失败胜过渲染出一片空白
    {
        std::string msg;
        if (!HandwriteGenerator::checkFontAvailable(params.fontPath, &msg)) {
            std::cerr << "错误: " << msg << std::endl;
            return 1;
        }
    }

    // 内存预算检查（x32/x64 这类高倍率会必然 OOM，直接拒绝并给出建议）
    {
        std::string msg;
        if (!HandwriteGenerator::checkRenderBudget(params, 0, 0, true, &msg)) {
            std::cerr << "错误: " << msg << std::endl;
            return 1;
        }
    }

    std::cout << "正在生成手写内容..." << std::endl;
    // 按字符数而非字节数统计，否则中文会虚高约 3 倍
    std::cout << "  文本长度: " << QString::fromUtf8(text.c_str()).length() << " 字符" << std::endl;
    std::cout << "  输出目录: " << outputDir << std::endl;
    std::cout << "  倍率: x" << params.rate << std::endl;
    std::cout << "  字体: " << params.fontPath << std::endl;

    // 进度回调：只在整十百分比处输出，避免刷屏
    auto progress = [](int done, int total) {
        if (total <= 0) return;
        const int pct = static_cast<int>(100LL * done / total);
        static int last = -1;
        if (pct / 10 != last / 10 || done == total) { last = pct; }
    };

    if (format == "pdf") {
        std::string pdfPath = outputDir + "/output.pdf";
        QDir().mkpath(QString::fromStdString(outputDir));
        try {
            if (generator.exportPdf(text, pdfPath)) {
                std::cout << "PDF 已生成: " << pdfPath << std::endl;
            } else {
                std::cerr << "PDF 生成失败" << std::endl;
                return 1;
            }
        } catch (const std::exception& e) {
            std::cerr << "PDF 生成失败: " << e.what() << std::endl;
            return 1;
        }
    } else if (format == "svg") {
        QDir().mkpath(QString::fromStdString(outputDir));
        const std::string svgPath = outputDir + "/output.svg";
        try {
            if (generator.exportSvg(text, svgPath)) {
                std::cout << "SVG 已生成: " << svgPath
                          << "（多页为 output-2.svg、output-3.svg …）" << std::endl;
            } else {
                std::cerr << "SVG 生成失败" << std::endl;
                return 1;
            }
        } catch (const std::exception& e) {
            std::cerr << "SVG 生成失败: " << e.what() << std::endl;
            return 1;
        }
    } else {
        try {
            auto result = generator.generateImageParallel(text, outputDir, 0, progress, nullptr);
            std::cout << "完成! 共生成 " << result.size() << " 页" << std::endl;
            for (const auto& [idx, path] : result) {
                std::cout << "  " << path << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "生成失败: " << e.what() << std::endl;
            return 1;
        }
    }
    
    return 0;
}
