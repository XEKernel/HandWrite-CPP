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
  -f, --format <格式>      输出格式: png 或 pdf（默认: png）
  -h, --help               显示此帮助

示例:
  handwrite-cli -t "今天天气真好" -o ./output -r 2
  handwrite-cli -i essay.txt -p presets/语文作业.conf -f pdf
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
    QCommandLineOption formatOpt({"f","format"}, "输出格式 (png/pdf)", "fmt", "png");
    QCommandLineOption batchOpt({"b","batch"}, "批量处理: 每行一个文件路径的列表文件", "batchfile");
    
    parser.addOption(inputOpt);
    parser.addOption(textOpt);
    parser.addOption(outputOpt);
    parser.addOption(presetOpt);
    parser.addOption(rateOpt);
    parser.addOption(formatOpt);
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
    
    // 加载预设
    if (parser.isSet(presetOpt)) {
        Config config(parser.value(presetOpt).toStdString());
        if (auto v = config.width()) params.paperWidth = *v;
        if (auto v = config.height()) params.paperHeight = *v;
        if (auto v = config.ttfSelector()) params.fontPath = *v;
        if (auto v = config.fontSize()) params.fontSize = *v;
        if (auto v = config.lineSpacing()) params.lineSpacing = *v;
        if (auto v = config.charDistance()) params.wordSpacing = *v;
        if (auto v = config.marginTop()) params.topMargin = *v;
        if (auto v = config.marginBottom()) params.bottomMargin = *v;
        if (auto v = config.marginLeft()) params.leftMargin = *v;
        if (auto v = config.marginRight()) params.rightMargin = *v;
        if (auto v = config.lineSpacingSigma()) params.lineSpacingSigma = *v;
        if (auto v = config.fontSizeSigma()) params.fontSizeSigma = *v;
        if (auto v = config.wordSpacingSigma()) params.wordSpacingSigma = *v;
        if (auto v = config.perturbXSigma()) params.perturbXSigma = *v;
        if (auto v = config.perturbYSigma()) params.perturbYSigma = *v;
        if (auto v = config.perturbThetaSigma()) params.perturbThetaSigma = *v;
        if (auto v = config.resolution()) params.rate = *v;
        if (auto v = config.charColor()) params.fillColor = Color((*v)[0],(*v)[1],(*v)[2],(*v)[3]);
        if (auto v = config.backgroundColor()) params.backgroundColor = Color((*v)[0],(*v)[1],(*v)[2],(*v)[3]);
    }
    
    // 命令行倍率覆盖
    if (parser.isSet(rateOpt)) {
        params.rate = parser.value(rateOpt).toInt();
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
            auto result = generator.generateImageParallel(readFile(line), itemOutput);
            std::cout << "[" << count << "] " << line << " -> " << result.size() << " 页" << std::endl;
            count++;
        }
        std::cout << "批量完成! 共处理 " << count << " 个文件" << std::endl;
        return 0;
    }
    
    std::cout << "正在生成手写内容..." << std::endl;
    std::cout << "  文本长度: " << text.length() << " 字符" << std::endl;
    std::cout << "  输出目录: " << outputDir << std::endl;
    std::cout << "  倍率: x" << params.rate << std::endl;
    std::cout << "  字体: " << params.fontPath << std::endl;
    
    if (format == "pdf") {
        std::string pdfPath = outputDir + "/output.pdf";
        QDir().mkpath(QString::fromStdString(outputDir));
        if (generator.exportPdf(text, pdfPath)) {
            std::cout << "PDF 已生成: " << pdfPath << std::endl;
        } else {
            std::cerr << "PDF 生成失败" << std::endl;
            return 1;
        }
    } else {
        auto result = generator.generateImageParallel(text, outputDir);
        std::cout << "完成! 共生成 " << result.size() << " 页" << std::endl;
        for (const auto& [idx, path] : result) {
            std::cout << "  " << path << std::endl;
        }
    }
    
    return 0;
}
