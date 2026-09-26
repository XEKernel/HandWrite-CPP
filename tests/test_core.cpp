//=============================================================================
// HandWrite 单元测试
//=============================================================================
// 覆盖「纯逻辑」部分：文本工具、Markdown 解析、排版、配置往返、内存预算。
// 不依赖 QtTest（CI 里少装一个包），自己维护一个极小的断言框架。
//
// 运行：ctest（或直接 ./handwrite-tests）。
// 平台插件自动选择：当前目录没有 platforms/（即还没跑 windeployqt）时就用 offscreen，
// 这样无头机器/CI 也能跑；已部署过插件时交给 Qt 默认平台。
// 退出码 0 = 全部通过，非 0 = 失败数量。
//=============================================================================

#include "core.hpp"
#include "config.hpp"

#include <QGuiApplication>
#include <QFont>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstdio>
#include <string>
#include <vector>
#include <sstream>

using namespace HandWrite;

//-----------------------------------------------------------------------------
// 极小断言框架
//-----------------------------------------------------------------------------
static int g_pass = 0;
static int g_fail = 0;
static std::string g_section;

static void section(const std::string& name) {
    g_section = name;
    std::printf("\n== %s\n", name.c_str());
}

template <typename T>
static std::string toStr(const T& v) {
    std::ostringstream o;
    o << v;
    return o.str();
}
static std::string toStr(bool v) { return v ? "true" : "false"; }

static void check(bool ok, const std::string& label, const std::string& detail = "") {
    if (ok) {
        ++g_pass;
        std::printf("  ok   %s\n", label.c_str());
    } else {
        ++g_fail;
        std::printf("  FAIL %s%s\n", label.c_str(),
                    detail.empty() ? "" : ("   -> " + detail).c_str());
    }
}

template <typename A, typename B>
static void checkEq(const A& got, const B& want, const std::string& label) {
    const bool ok = (got == want);
    check(ok, label, ok ? "" : ("got=" + toStr(got) + " want=" + toStr(want)));
}

//=============================================================================
// 1. 中文标点转换
//=============================================================================
static void testConvertChinesePunctuation() {
    section("convertChinesePunctuation");

    checkEq(HandwriteGenerator::convertChinesePunctuation(QStringLiteral("，")).toStdString(),
            std::string(","), "全角逗号 -> 半角");
    checkEq(HandwriteGenerator::convertChinesePunctuation(QStringLiteral("。")).toStdString(),
            std::string("."), "句号 -> 点");
    checkEq(HandwriteGenerator::convertChinesePunctuation(QStringLiteral("“引”")).toStdString(),
            std::string("\"引\""), "弯引号 -> 直引号");
    checkEq(HandwriteGenerator::convertChinesePunctuation(QStringLiteral("《书》")).toStdString(),
            std::string("<书>"), "书名号 -> 尖括号");
    checkEq(HandwriteGenerator::convertChinesePunctuation(QStringLiteral("　")).toStdString(),
            std::string(" "), "全角空格 -> 半角空格");
    checkEq(HandwriteGenerator::convertChinesePunctuation(QStringLiteral("汉字abc")).toStdString(),
            std::string("汉字abc"), "汉字与 ASCII 保持不变");
}

//=============================================================================
// 2. Markdown 解析
//=============================================================================
static void testParseMarkdown() {
    section("parseMarkdown");

    {
        const auto spans = HandwriteGenerator::parseMarkdown(QStringLiteral("普通文本"));
        checkEq(spans.size(), size_t(1), "纯文本 -> 1 个 span");
        if (!spans.empty()) {
            check(spans[0].style == TextStyle::Normal, "纯文本 style = Normal");
            checkEq(spans[0].text.toStdString(), std::string("普通文本"), "纯文本内容");
        }
    }
    {
        // 回归 P1-2：删除线必须闭合，否则后半段会被误加删除线
        const auto spans = HandwriteGenerator::parseMarkdown(QStringLiteral("~~废弃~~保留"));
        checkEq(spans.size(), size_t(2), "删除线闭合 -> 2 个 span");
        if (spans.size() == 2) {
            check(spans[0].style == TextStyle::Strikethrough, "前半段 = Strikethrough");
            checkEq(spans[0].text.toStdString(), std::string("废弃"), "前半段文本");
            check(spans[1].style == TextStyle::Normal, "后半段回到 Normal（旧实现会错误继承删除线）");
            checkEq(spans[1].text.toStdString(), std::string("保留"), "后半段文本");
        }
    }
    {
        const auto spans = HandwriteGenerator::parseMarkdown(QStringLiteral("# 标题\n正文"));
        check(spans.size() >= 2, "标题 + 正文 -> 至少 2 个 span");
        if (!spans.empty()) {
            check(spans[0].style == TextStyle::Heading, "首行 = Heading");
            checkEq(spans[0].fontSizeOverride, 48, "Heading 字号覆盖 = 48");
            // "# 标题" 中内容是 '标'，原文索引 2（'#'=0, ' '=1）
            checkEq(spans[0].origStart, 2, "Heading 的 origStart 指向正文起点");
        }
    }
    {
        const auto spans = HandwriteGenerator::parseMarkdown(QStringLiteral("---\n"));
        bool hasSeparator = false;
        for (const auto& s : spans) if (s.style == TextStyle::Separator) hasSeparator = true;
        check(hasSeparator, "行首 --- -> 分割线 span");
    }
    {
        // 回归：正文中间的 --- 不应被当成分割线
        const auto spans = HandwriteGenerator::parseMarkdown(QStringLiteral("甲---乙"));
        bool hasSeparator = false;
        for (const auto& s : spans) if (s.style == TextStyle::Separator) hasSeparator = true;
        check(!hasSeparator, "行中 --- 不被误判为分割线");
    }
    {
        const auto spans = HandwriteGenerator::parseMarkdown(QStringLiteral("标题AB"));
        if (!spans.empty()) {
            // origStart 用于把「渲染索引」映射回「原文索引」（P1-4 的基础）
            checkEq(spans[0].origStart, 0, "origStart 从 0 开始");
        }
    }
}

//=============================================================================
// 3. 字符覆盖序列化往返
//=============================================================================
static void testCharOverrideRoundTrip() {
    section("CharOverride 序列化往返");

    CharacterOverrideRange r;
    r.startIndex = 3;
    r.endIndex = 7;
    r.override.fontSize = 42;
    r.override.perturbX = 1.25;
    r.override.perturbY = -2.5;
    r.override.perturbTheta = 0.125;
    r.override.fillColor = Color(10, 20, 30, 200);

    const std::string encoded = HandwriteGenerator::serializeCharOverride(r);
    const auto decoded = HandwriteGenerator::deserializeCharOverride(encoded);

    check(decoded.has_value(), "能解析回结构");
    if (decoded) {
        checkEq(decoded->startIndex, 3, "startIndex");
        checkEq(decoded->endIndex, 7, "endIndex");
        checkEq(decoded->override.fontSize.value_or(-1), 42, "fontSize");
        checkEq(decoded->override.perturbTheta.value_or(-1.0), 0.125, "perturbTheta");
        check(decoded->override.fillColor.has_value(), "颜色已还原");
        if (decoded->override.fillColor) {
            checkEq(static_cast<int>(decoded->override.fillColor->a), 200, "颜色 alpha");
        }
    }

    // 只设索引、不带任何属性 -> 视为无效
    CharacterOverrideRange bare;
    bare.startIndex = 0;
    bare.endIndex = 1;
    check(!HandwriteGenerator::deserializeCharOverride(
              HandwriteGenerator::serializeCharOverride(bare)).has_value(),
          "无属性的覆盖被丢弃");

    check(!HandwriteGenerator::deserializeCharOverride("").has_value(), "空串 -> nullopt");
    check(!HandwriteGenerator::deserializeCharOverride("a,b").has_value(), "字段不足 -> nullopt");
}

//=============================================================================
// 4. 文本排版
//=============================================================================
static void testLayoutText() {
    section("layoutText");

    HandwriteGenerator gen;
    TemplateParams p;
    p.rate = 1;
    p.paragraphIndent = false;
    p.fontSizeSigma = 0;
    p.perturbXSigma = 0;
    p.wordSpacingSigma = 0;
    gen.modifyTemplateParams(p);

    // 固定字号，避免依赖系统默认字体的度量，测试才可复现
    QFont font;
    font.setPixelSize(16);

    {
        const QString text = QStringLiteral("今天天气很好，我们去公园散步吧。");
        const auto lines = gen.layoutText(text, font, 2000, 70, 0);
        checkEq(lines.size(), size_t(1), "宽幅下单行容纳整句");

        if (!lines.empty()) {
            // 渲染索引 -> 原文索引必须逐字符对齐（P1-4 的核心保证）
            bool aligned = (lines[0].origIndices.size() == static_cast<size_t>(lines[0].text.length()));
            if (aligned) {
                for (int i = 0; i < lines[0].text.length(); ++i) {
                    if (lines[0].origIndices[static_cast<size_t>(i)] != i) { aligned = false; break; }
                }
            }
            check(aligned, "origIndices 与原文逐字对齐");

            bool punctKept = lines[0].text.contains(QChar(0x3002)) &&
                             lines[0].text.contains(QChar(0xFF0C));
            check(punctKept, "preserveChinesePunctuation=true 时保留全角标点");
        }
    }
    {
        // preserveChinesePunctuation=false 时标点应被转成 ASCII
        TemplateParams p2 = p;
        p2.preserveChinesePunctuation = false;
        gen.modifyTemplateParams(p2);
        const auto lines = gen.layoutText(QStringLiteral("你好。"), font, 2000, 70, 0);
        bool ascii = false;
        for (const auto& l : lines) if (l.text.contains(QLatin1Char('.'))) ascii = true;
        check(ascii, "preserveChinesePunctuation=false 时标点转 ASCII");
        gen.modifyTemplateParams(p);
    }
    {
        // 避头尾：任何一行都不应以「行尾禁则」字符开头
        const QString text = QStringLiteral(
            "春眠不觉晓，处处闻啼鸟。夜来风雨声，花落知多少。"
            "锄禾日当午，汗滴禾下土。谁知盘中餐，粒粒皆辛苦。"
            "床前明月光，疑是地上霜。举头望明月，低头思故乡。");
        const auto lines = gen.layoutText(text, font, 200, 70, 0);
        check(lines.size() >= 3, "窄幅下切出多行");

        const QString endChars = QString::fromStdString(p.endChars);
        bool noOrphanPunct = true;
        std::string offender;
        for (const auto& l : lines) {
            if (l.text.isEmpty()) continue;
            if (endChars.contains(l.text.front())) {
                noOrphanPunct = false;
                offender = l.text.left(4).toStdString();
                break;
            }
        }
        check(noOrphanPunct, "没有「行首孤悬标点」（避头尾生效）", offender);
    }
    {
        // 段首缩进：首行前两个字符是程序插入的全角空格，映射为 -1
        TemplateParams p3 = p;
        p3.paragraphIndent = true;
        gen.modifyTemplateParams(p3);
        const auto lines = gen.layoutText(QStringLiteral("第一段内容。\n第二段内容。"), font, 2000, 70, 0);
        check(lines.size() >= 2, "两段 -> 至少两行");
        if (!lines.empty()) {
            check(lines[0].text.startsWith(QString(QChar(0x3000))), "段首插入全角空格缩进");
            check(lines[0].origIndices.size() >= 2 &&
                  lines[0].origIndices[0] == -1 && lines[0].origIndices[1] == -1,
                  "缩进字符的原文索引标记为 -1");
        }
        gen.modifyTemplateParams(p);
    }
    {
        // 标题行应带上字号覆盖
        const auto lines = gen.layoutText(QStringLiteral("# 大标题"), font, 2000, 70, 0);
        bool heading = false;
        for (const auto& l : lines) if (l.isHeading()) heading = true;
        check(heading, "Markdown 标题 -> 行字号覆盖 > 0");

        bool bodyNotHeading = true;
        const auto body = gen.layoutText(QStringLiteral("正文一行"), font, 2000, 70, 0);
        for (const auto& l : body) if (l.isHeading()) bodyNotHeading = false;
        check(bodyNotHeading, "正文行不带字号覆盖");
    }
    {
        // 空文本不应产出任何行
        const auto lines = gen.layoutText(QString(), font, 2000, 70, 0);
        checkEq(lines.size(), size_t(0), "空文本 -> 0 行");
    }
}

//=============================================================================
// 5. 横线导引（Line Guides）几何
//=============================================================================
static void testLineGuides() {
    section("Line Guides 几何");

    const qreal deg45 = std::atan2(1.0, 1.0);

    // --- sample：线性插值 + 端点夹取 + 切线角 ---
    {
        GuideCurve c;
        c.pts = std::vector<QPointF>{QPointF(0, 100), QPointF(100, 200)};
        checkEq(c.yAt(0.0), 100.0, "sample 起点");
        checkEq(c.yAt(100.0), 200.0, "sample 终点");
        checkEq(c.yAt(50.0), 150.0, "sample 中点线性插值");
        checkEq(c.yAt(-100.0), 100.0, "sample 左越界夹到端点（不外推）");
        checkEq(c.yAt(9999.0), 200.0, "sample 右越界夹到端点（不外推）");
        check(std::abs(c.angleAt(50.0) - deg45) < 1e-6, "直线切线角 = 45°");
    }
    // --- 弯曲曲线：中间高、两端低 ---
    {
        GuideCurve c;
        c.pts = std::vector<QPointF>{QPointF(0, 100), QPointF(50, 120), QPointF(100, 100)};
        checkEq(c.yAt(50.0), 120.0, "弯曲曲线顶点");
        checkEq(c.yAt(25.0), 110.0, "弯曲曲线左半插值");
        checkEq(c.yAt(75.0), 110.0, "弯曲曲线右半插值");
        check(c.angleAt(25.0) > 0.0, "左半切线向下（角度为正）");
        check(c.angleAt(75.0) < 0.0, "右半切线向上（角度为负）");
        check(std::abs(c.angleAt(25.0) + c.angleAt(75.0)) < 1e-6, "左右切线角对称");
    }
    // --- 边界情况 ---
    {
        GuideCurve empty;
        check(!empty.usable(), "空曲线不可用");
        checkEq(empty.yAt(10.0), 0.0, "空曲线采样返回 0");
        GuideCurve one;
        one.pts = std::vector<QPointF>{QPointF(5, 5)};
        check(!one.usable(), "单点曲线不可用");
        checkEq(one.yAt(10.0), 5.0, "单点曲线采样返回该点");
    }
    // --- normalize：排序、去重、平滑 ---
    {
        GuideCurve c;
        // 故意乱序 + 重复 x + 一个尖刺
        c.pts = std::vector<QPointF>{QPointF(80, 100), QPointF(0, 100), QPointF(40, 200), QPointF(40, 100),
                 QPointF(20, 100), QPointF(100, 100)};
        c.normalize(2);
        check(c.pts.size() >= 2, "normalize 后仍可用");
        bool sorted = true;
        for (size_t i = 1; i < c.pts.size(); ++i) {
            if (c.pts[i].x() < c.pts[i-1].x()) { sorted = false; break; }
        }
        check(sorted, "normalize 后按 x 升序");
        bool noDup = true;
        for (size_t i = 1; i < c.pts.size(); ++i) {
            if (std::abs(c.pts[i].x() - c.pts[i-1].x()) < 1.0) { noDup = false; break; }
        }
        check(noDup, "normalize 合并了 x 重复的点");
        // 平滑后尖刺应被压低（原尖刺 y=150（200与100合并），平滑后应明显小于它）
        check(c.yAt(40.0) < 145.0, "normalize 平滑压低了尖刺");
        check(std::abs(c.yAt(0.0) - 100.0) < 1e-6, "normalize 不改首端点");
        check(std::abs(c.yAt(100.0) - 100.0) < 1e-6, "normalize 不改尾端点");
    }
    // --- resampleByArcLength ---
    {
        GuideCurve c;
        c.pts = std::vector<QPointF>{QPointF(0, 0), QPointF(50, 0), QPointF(100, 0)};
        const auto rs = c.resampleByArcLength(5);
        checkEq(rs.size(), size_t(5), "重采样点数 = 5");
        if (rs.size() == 5) {
            check(std::abs(rs.front().x() - 0.0) < 1e-6, "重采样首点 = 原首点");
            check(std::abs(rs.back().x() - 100.0) < 1e-6, "重采样尾点 = 原尾点");
            bool monotonic = true;
            for (size_t i = 1; i < rs.size(); ++i) {
                if (rs[i].x() < rs[i-1].x()) { monotonic = false; break; }
            }
            check(monotonic, "重采样点单调递增");
        }
        const auto degenerate = GuideCurve{}.resampleByArcLength(4);
        check(degenerate.empty(), "空曲线重采样返回空");
    }
    // --- 关键曲线插值 build() ---
    {
        LineGuideSet g;
        g.enabled = true;
        g.useInterpolation = true;
        g.lineCount = 5;
        GuideCurve top, bottom;
        top.pts    = {QPointF(0, 100), QPointF(100, 100)};
        bottom.pts = std::vector<QPointF>{QPointF(0, 500), QPointF(100, 500)};
        g.keyCurves = {top, bottom};

        check(g.isValid(), "build 前置：配置有效");
        const auto curves = g.build();
        checkEq(curves.size(), size_t(5), "build 生成 5 条");
        if (curves.size() == 5) {
            check(std::abs(curves[0].yAt(50.0) - 100.0) < 1e-6, "第 1 条 = 首关键曲线");
            check(std::abs(curves[4].yAt(50.0) - 500.0) < 1e-6, "第 5 条 = 尾关键曲线");
            check(std::abs(curves[2].yAt(50.0) - 300.0) < 1e-6, "中间条 = 两端平均");
            check(std::abs(curves[1].yAt(50.0) - 200.0) < 1e-6, "第 2 条按 1/4 插值");
        }
    }
    // --- 关键曲线插值（分段：3 条关键曲线） ---
    {
        LineGuideSet g;
        g.enabled = true;
        g.lineCount = 5;
        GuideCurve a, b, c;
        a.pts = std::vector<QPointF>{QPointF(0, 0),   QPointF(100, 0)};
        b.pts = std::vector<QPointF>{QPointF(0, 100), QPointF(100, 100)};
        c.pts = std::vector<QPointF>{QPointF(0, 300), QPointF(100, 300)};   // 后段间距是前段的 2 倍
        g.keyCurves = {a, b, c};
        const auto curves = g.build();
        checkEq(curves.size(), size_t(5), "分段插值仍生成 5 条");
        if (curves.size() == 5) {
            check(std::abs(curves[2].yAt(50.0) - 100.0) < 1e-6, "中间条落在第 2 条关键曲线上");
            check(std::abs(curves[4].yAt(50.0) - 300.0) < 1e-6, "末条 = 第 3 条关键曲线");
        }
    }
    // --- 不插值模式 ---
    {
        LineGuideSet g;
        g.enabled = true;
        g.useInterpolation = false;
        GuideCurve a, b;
        a.pts = std::vector<QPointF>{QPointF(0, 10), QPointF(100, 10)};
        b.pts = std::vector<QPointF>{QPointF(0, 20), QPointF(100, 20)};
        g.keyCurves = {a, b};
        check(g.isValid(), "不插值模式：只有 1 条也有效");
        checkEq(g.build().size(), size_t(2), "不插值时条数 = 手绘条数");
    }
    // --- isValid ---
    {
        LineGuideSet g;
        check(!g.isValid(), "未启用 = 无效");
        g.enabled = true;
        check(!g.isValid(), "启用但无关键曲线 = 无效");
        GuideCurve a; a.pts = std::vector<QPointF>{QPointF(0,0), QPointF(1,1)};
        g.keyCurves = {a};
        check(!g.isValid(), "插值模式只有 1 条 = 无效");
        g.useInterpolation = false;
        check(g.isValid(), "不插值模式 1 条即可");
    }
    // --- build() 顺序无关：关键曲线顺序被打乱也不应改变结果 ---
    {
        GuideCurve top, bot;
        top.pts = std::vector<QPointF>{QPointF(0, 100), QPointF(100, 100)};
        bot.pts = std::vector<QPointF>{QPointF(0, 500), QPointF(100, 500)};

        LineGuideSet a;
        a.enabled = true; a.lineCount = 5;
        a.keyCurves = {top, bot};

        LineGuideSet b;
        b.enabled = true; b.lineCount = 5;
        b.keyCurves = {bot, top};            // 反序（预设文件里顺序不可控）

        const auto ra = a.build();
        const auto rb = b.build();
        checkEq(ra.size(), rb.size(), "顺序无关：条数一致");
        if (ra.size() >= 2 && ra.size() == rb.size()) {
            check(std::abs(ra.front().yAt(50.0) - rb.front().yAt(50.0)) < 1e-6,
                  "顺序无关：首条一致");
            check(std::abs(ra.back().yAt(50.0) - rb.back().yAt(50.0)) < 1e-6,
                  "顺序无关：末条一致");
            check(ra.front().yAt(50.0) < ra.back().yAt(50.0), "首条在上、末条在下");
        }
    }
    // --- 单条曲线内部点乱序也应被排序（sample 的前置条件是 x 升序） ---
    {
        GuideCurve c;
        c.pts = std::vector<QPointF>{QPointF(100, 200), QPointF(0, 100), QPointF(50, 150)};
        LineGuideSet g;
        g.enabled = true; g.lineCount = 2; g.useInterpolation = false;
        g.keyCurves = {c};
        const auto r = g.build();
        checkEq(r.size(), size_t(1), "单条不插值输出 1 条");
        if (!r.empty()) {
            check(std::abs(r[0].yAt(0.0) - 100.0) < 1e-6, "乱序点排序后：x=0 处正确");
            check(std::abs(r[0].yAt(50.0) - 150.0) < 1e-6, "乱序点排序后：x=50 处正确");
            check(std::abs(r[0].yAt(100.0) - 200.0) < 1e-6, "乱序点排序后：x=100 处正确");
        }
    }
    // --- 曲线编解码往返（配置用） ---
    {
        GuideCurve c1, c2;
        c1.pts = std::vector<QPointF>{QPointF(0, 100), QPointF(50, 120), QPointF(100, 100)};
        c2.pts = std::vector<QPointF>{QPointF(0, 300), QPointF(100, 305)};
        const std::vector<GuideCurve> src = {c1, c2};
        const auto flat = flattenGuideCurves(src);
        // 2 条曲线：(1 + 3*2) + (1 + 2*2) = 7 + 5 = 12
        checkEq(flat.size(), size_t(12), "平铺数组长度 = 12");
        const auto back = parseGuideCurves(flat);
        checkEq(back.size(), size_t(2), "解回 2 条曲线");
        if (back.size() == 2) {
            checkEq(back[0].pts.size(), size_t(3), "第 1 条点数还原");
            checkEq(back[1].pts.size(), size_t(2), "第 2 条点数还原");
            check(std::abs(back[0].yAt(50.0) - 120.0) < 1e-6, "第 1 条曲线形状还原");
            check(std::abs(back[1].yAt(100.0) - 305.0) < 1e-6, "第 2 条曲线形状还原");
        }
        check(parseGuideCurves({}).empty(), "空数组 -> 空");
        check(parseGuideCurves({0, 1, 2}).empty(), "非法点数 -> 停止解析");
        check(parseGuideCurves({3, 0, 0}).empty(), "数据不足 -> 停止解析");
    }
}

//=============================================================================
// 6. 横线自动检测
//=============================================================================
namespace {

// 合成一张「作业本照片」：纸面浅色，横线为浅蓝灰
// sag > 0 时横线中间下凹（模拟纸张弯曲）
QImage makeNotebookImage(int w, int h, int lineCount, double sag, bool drawLines = true) {
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(qRgb(250, 250, 246));

    if (!drawLines || lineCount < 2) return img;

    const double top = h * 0.08;
    const double bottom = h * 0.94;
    for (int i = 0; i < lineCount; ++i) {
        const double base = top + (bottom - top) * i / (lineCount - 1.0);
        for (int x = 0; x < w; ++x) {
            const double t = static_cast<double>(x) / w;
            const double y = base + sag * 4.0 * t * (1.0 - t);
            const int yi = static_cast<int>(std::round(y));
            if (yi >= 0 && yi < h) img.setPixel(x, yi, qRgb(150, 165, 195));
        }
    }
    return img;
}

} // namespace

static void testLineDetection() {
    section("横线自动检测");

    // --- 直横线：条数与线距应准确 ---
    {
        const QImage img = makeNotebookImage(200, 400, 20, 0.0);
        const auto res = HandwriteGenerator::detectHorizontalLines(img);
        check(res.ok, "直横线图检测成功", res.message.toStdString());
        if (res.ok) {
            // 20 条线均分，实际检测取首尾峰之间，故为 19
            check(std::abs(res.suggestedCount - 19) <= 1, "条数 ≈ 19",
                  "got=" + std::to_string(res.suggestedCount));
            check(std::abs(res.spacing - (400 * 0.86 / 19.0)) < 2.5, "线距与构造值接近",
                  "got=" + std::to_string(res.spacing));
            check(res.midlineDeviation < 0.18, "直横线弯曲接近线性（偏差小）",
                  "dev=" + std::to_string(res.midlineDeviation));
            checkEq(res.keyCurves.size(), size_t(2), "直横线只需首尾 2 条关键曲线");
        }
    }
    // --- 弯曲横线：检测出的应是曲线（能表达纸张弯曲） ---
    {
        const QImage img = makeNotebookImage(400, 400, 10, 30.0);
        const auto res = HandwriteGenerator::detectHorizontalLines(img);
        check(res.ok, "弯曲横线图检测成功", res.message.toStdString());
        if (res.ok && !res.curves.empty()) {
            const GuideCurve& c = res.curves.front();
            const qreal yLeft  = c.yAt(c.pts.front().x());
            const qreal yMid   = c.yAt(200.0);
            const qreal yRight = c.yAt(c.pts.back().x() - 1.0);
            check(yMid > yLeft + 15.0, "曲线中间下凹（不是直线）—— 左→中落差足够",
                  "left=" + std::to_string(yLeft) + " mid=" + std::to_string(yMid));
            check(yMid > yRight + 15.0, "曲线右端回升",
                  "right=" + std::to_string(yRight) + " mid=" + std::to_string(yMid));
            check(std::abs(yLeft - yRight) < 6.0, "左右两端高度接近");
        }
    }
    // --- 空白图：应当检测失败并给出提示 ---
    {
        const QImage img = makeNotebookImage(200, 400, 0, 0.0, false);
        const auto res = HandwriteGenerator::detectHorizontalLines(img);
        check(!res.ok, "空白图检测失败");
        check(!res.message.isEmpty(), "失败时给出可读提示");
    }
    // --- 空图 ---
    {
        const auto res = HandwriteGenerator::detectHorizontalLines(QImage());
        check(!res.ok, "空图片检测失败");
        check(!res.message.isEmpty(), "空图片有提示");
    }
    // --- 小图 ---
    {
        const auto res = HandwriteGenerator::detectHorizontalLines(QImage(16, 16, QImage::Format_RGB32));
        check(!res.ok, "过小图片检测失败");
    }
    // --- 深色背景（桌面）+ 纸只占中间：不应被背景带偏，也不应产生纸外垃圾段 ---
    {
        QImage img(300, 300, QImage::Format_RGB32);
        img.fill(qRgb(38, 38, 42));                                   // 深灰桌面（比横线更暗）
        for (int y = 20; y < 280; ++y) {
            for (int x = 60; x < 240; ++x) img.setPixel(x, y, qRgb(250, 250, 246));
        }
        for (int i = 0; i < 8; ++i) {
            const int ly = 28 + i * 32;
            for (int x = 60; x < 240; ++x) img.setPixel(x, ly, qRgb(150, 165, 195));
        }
        const auto res = HandwriteGenerator::detectHorizontalLines(img);
        check(res.ok, "深色背景下仍能检测到横线", res.message.toStdString());
        if (res.ok && !res.curves.empty()) {
            const GuideCurve& c = res.curves.front();
            const qreal x0 = c.pts.front().x();
            const qreal x1 = c.pts.back().x();
            // 纸面是 x∈[60,240)；曲线两端不应延伸到纸外（否则会带着等距初值的垃圾段）
            check(x0 >= 55.0, "曲线左端落在纸面内", "x0=" + std::to_string(x0));
            check(x1 <= 245.0, "曲线右端落在纸面内", "x1=" + std::to_string(x1));
            check(x1 - x0 > 120.0, "曲线有效跨度足够宽", "span=" + std::to_string(x1 - x0));
        }
    }
    // --- 检测出的关键曲线应能直接用于插值渲染 ---
    {
        const QImage img = makeNotebookImage(400, 400, 12, 20.0);
        const auto res = HandwriteGenerator::detectHorizontalLines(img);
        if (res.ok) {
            LineGuideSet g;
            g.enabled = true;
            g.keyCurves = res.keyCurves;
            g.lineCount = res.suggestedCount;
            g.useInterpolation = true;
            check(g.isValid(), "检测结果填回 LineGuideSet 后有效");
            const auto built = g.build();
            checkEq(built.size(), static_cast<size_t>(res.suggestedCount),
                    "插值条数 = 检测条数");
            if (!built.empty()) {
                // 首条应与检测到的首条一致
                check(std::abs(built.front().yAt(200.0) - res.curves.front().yAt(200.0)) < 2.0,
                      "插值的首条 = 检测的首条");
                check(std::abs(built.back().yAt(200.0) - res.curves.back().yAt(200.0)) < 2.0,
                      "插值的末条 = 检测的末条");
            }
        }
    }
}

//=============================================================================
// 7. 配置读写往返
//=============================================================================
static void testConfigRoundTrip() {
    section("Config 往返");

    QTemporaryDir tmp;
    check(tmp.isValid(), "临时目录可用");
    if (!tmp.isValid()) return;
    const QString path = tmp.filePath(QStringLiteral("preset.conf"));

    {
        Config c;
        c.setWidth(667);
        c.setHeight(945);
        c.setResolution(8);
        c.setFontSize(30);
        c.setPaperTexture("grid");
        c.setTextureOpacity(0.42);
        c.setTtfSelector("ttf_library/example.ttf");
        c.setFontMixList({"a.ttf", "b,c.ttf"});          // 含逗号的项必须能存活
        c.setFontMixRate(0.35);
        c.setParagraphIndent(true);
        c.setParagraphSpacing(12);
        c.setTextDirection(1);
        c.setTextWarp(2);
        c.setTextWarpStrength(1.5);
        c.setPreserveChinesePunctuation(false);
        c.setInkBleed(true);
        c.setInkBleedRadius(2.5);
        c.setStrikeThroughRate(0.1);
        c.setBgCalibEnabled(true);
        c.setBgCalibRows(3);
        c.setBgCalibCols(4);
        c.setBgCalibPoints({1.5, 2.5, 3.5, 4.5});
        c.setCharOverrides({"0,3,42,1.25,,,10,20,30,200"});
        c.setSeed(20260912u);
        c.setCharColor({0, 0, 0, 255});
        // 横线导引（含一条弯曲的关键曲线）
        c.setLineGuideEnabled(true);
        c.setLineGuideLineCount(20);
        c.setLineGuideInterpolate(true);
        c.setLineGuideBaselineRatio(0.75);
        c.setLineGuideBaselineOffset(-2);
        c.setLineGuideFollowCurve(true);
        c.setLineGuideLinesPerRow(1);
        {
            GuideCurve ca, cb;
            ca.pts = std::vector<QPointF>{QPointF(0, 100), QPointF(500, 118), QPointF(1000, 100)};
            cb.pts = std::vector<QPointF>{QPointF(0, 800), QPointF(1000, 812)};
            c.setLineGuideCurves(flattenGuideCurves({ca, cb}));
        }
        check(c.save(path.toStdString()), "保存成功");
    }

    // 回归 P2-5：混合数组 [1, 2.5] 不应丢掉整数项
    {
        QFile f(tmp.filePath(QStringLiteral("mixed.conf")));
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream ts(&f);
            ts << "mixed = [1, 2.5, 3]\n";
            // 回归：std::stoi("0.35") 只读前导 0 就返回，小数被打回整数
            ts << "sig = 0.05\n";
            ts << "opacity = 0.3\n";
            ts << "rate = 0.125\n";
            ts << "big = 3000000000\n";
        }
        Config m(tmp.filePath(QStringLiteral("mixed.conf")).toStdString());
        const auto arr = m.getDoubleArray("mixed");
        check(arr.has_value(), "混合数组能读出 double 数组");
        if (arr) {
            checkEq(arr->size(), size_t(3), "混合数组长度 = 3（旧实现会丢成 1）");
            if (arr->size() == 3) checkEq((*arr)[1], 2.5, "混合数组保留小数项");
        }
        // 回归：纯整数坐标会被 load() 存成 intArray，必须仍能按 double 数组读回
        // （锚点、导引曲线落在整数像素上时就是这种情况，否则整组坐标会丢）
        {
            QFile f2(tmp.filePath(QStringLiteral("ints.conf")));
            if (f2.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream ts(&f2);
                ts << "ints = [10, 20, 30]\n";
            }
            Config mi(tmp.filePath(QStringLiteral("ints.conf")).toStdString());
            const auto ia = mi.getDoubleArray("ints");
            check(ia.has_value(), "纯整数数组能按 double 数组读出");
            if (ia) {
                checkEq(ia->size(), size_t(3), "纯整数数组长度不变");
                if (ia->size() == 3) checkEq((*ia)[2], 30.0, "纯整数数组值还原");
            }
        }

        checkEq(m.getDouble("sig").value_or(-1.0), 0.05, "0.05 不被截断成 0");
        checkEq(m.getDouble("opacity").value_or(-1.0), 0.3, "0.3 不被截断成 0");
        checkEq(m.getDouble("rate").value_or(-1.0), 0.125, "0.125 不被截断成 0");
        check(m.getDouble("big").value_or(0.0) > 2.9e9, "超大整数退化为 double 而非溢出");
    }

    {
        Config c(path.toStdString());
        checkEq(c.width().value_or(-1), 667, "width");
        checkEq(c.height().value_or(-1), 945, "height");
        checkEq(c.resolution().value_or(-1), 8, "resolution");
        checkEq(c.paperTexture().value_or(""), std::string("grid"), "paper_texture");
        checkEq(c.fontMixRate().value_or(-1.0), 0.35, "font_mix_rate");
        checkEq(c.textDirection().value_or(-1), 1, "text_direction");
        checkEq(c.textWarp().value_or(-1), 2, "text_warp");
        checkEq(c.textWarpStrength().value_or(-1.0), 1.5, "text_warp_strength");
        checkEq(c.preserveChinesePunctuation().value_or(true), false, "preserve_chinese_punctuation");
        checkEq(c.strikeThroughRate().value_or(-1.0), 0.1, "strikethrough_rate");
        checkEq(c.seed().value_or(0u), 20260912u, "seed");
        checkEq(c.bgCalibRows().value_or(-1), 3, "bg_calib_rows");
        checkEq(c.bgCalibCols().value_or(-1), 4, "bg_calib_cols");
        checkEq(c.bgCalibPoints().value_or(std::vector<double>{}).size(), size_t(4), "bg_calib_points 长度");
        checkEq(c.paragraphIndent().value_or(false), true, "paragraph_indent");

        const auto mix = c.fontMixList();
        check(mix.has_value() && mix->size() == 2, "font_mix_list 长度 = 2");
        if (mix && mix->size() == 2) {
            checkEq((*mix)[0], std::string("a.ttf"), "font_mix_list[0]");
            checkEq((*mix)[1], std::string("b,c.ttf"), "font_mix_list[1] 含逗号未被切碎");
        }

        const auto ov = c.charOverrides();
        check(ov.has_value() && ov->size() == 1, "char_overrides 长度 = 1");
        if (ov && !ov->empty()) {
            const auto parsed = HandwriteGenerator::deserializeCharOverride((*ov)[0]);
            check(parsed.has_value(), "char_overrides[0] 可解析");
            if (parsed) checkEq(parsed->override.fontSize.value_or(-1), 42, "char_overrides 字号");
        }

        const auto color = c.charColor();
        check(color.has_value() && color->size() == 4, "char_color 长度 = 4");

        // 横线导引
        checkEq(c.lineGuideEnabled().value_or(false), true, "line_guide_enabled");
        checkEq(c.lineGuideLineCount().value_or(-1), 20, "line_guide_line_count");
        checkEq(c.lineGuideInterpolate().value_or(false), true, "line_guide_interpolate");
        checkEq(c.lineGuideBaselineRatio().value_or(-1.0), 0.75, "line_guide_baseline_ratio");
        checkEq(c.lineGuideBaselineOffset().value_or(99), -2, "line_guide_baseline_offset");
        checkEq(c.lineGuideFollowCurve().value_or(false), true, "line_guide_follow_curve");
        {
            const auto flat = c.lineGuideCurves();
            check(flat.has_value() && flat->size() == (1 + 3*2) + (1 + 2*2), "line_guide_curves 长度");
            if (flat) {
                const auto curves = parseGuideCurves(*flat);
                checkEq(curves.size(), size_t(2), "解回 2 条关键曲线");
                if (curves.size() == 2) {
                    check(std::abs(curves[0].yAt(500.0) - 118.0) < 1e-6, "弯曲关键曲线形状还原");
                    check(std::abs(curves[1].yAt(1000.0) - 812.0) < 1e-6, "第 2 条关键曲线还原");
                }
            }
        }

        // 二次保存必须幂等（内容稳定）
        Config again(path.toStdString());
        check(again.save(path.toStdString()), "二次保存成功");
        Config third(path.toStdString());
        checkEq(third.fontMixRate().value_or(-1.0), 0.35, "二次往返后 font_mix_rate 仍正确");
    }
}

//=============================================================================
// 8. 内存预算（P0-2 回归）
//=============================================================================
static void testRenderBudget() {
    section("渲染内存预算");

    TemplateParams p;
    p.paperWidth = 667;
    p.paperHeight = 945;

    p.rate = 4;
    const long long per4 = HandwriteGenerator::estimateSinglePageBytes(p);
    check(per4 > 0, "x4 单页估算 > 0");
    check(HandwriteGenerator::checkRenderBudget(p, 0, 1, true, nullptr) == true,
          "x4 单页通过预算检查");

    p.rate = 16;
    check(HandwriteGenerator::checkRenderBudget(p, 0, 1, true, nullptr) == true,
          "x16 单页通过预算检查");

    p.rate = 32;
    std::string msg;
    const bool ok32 = HandwriteGenerator::checkRenderBudget(p, 0, 1, true, &msg);
    check(!ok32, "x32 单页被预算拦下（旧实现必然 OOM）");
    check(!msg.empty(), "x32 给出可读的失败原因");
    check(msg.find("倍率") != std::string::npos, "失败原因提示降低倍率");

    p.rate = 64;
    check(!HandwriteGenerator::checkRenderBudget(p, 0, 1, true, nullptr),
          "x64 单页被预算拦下");

    // 多页 × 需同时持有 -> 峰值叠加，线程数应被压到 1
    p.rate = 8;
    checkEq(HandwriteGenerator::clampThreadsForBudget(p, 16, 4, true), 1,
            "holdAllPages=true 时线程数压到 1");

    p.rate = 1;
    const int t = HandwriteGenerator::clampThreadsForBudget(p, 8, 100, false);
    check(t >= 1 && t <= 8, "低倍率时线程数落在 [1, 请求值]");
}

//=============================================================================
// 9. 字体可用性检查（P2-10）
//=============================================================================
static void testFontCheck() {
    section("字体可用性检查");

    std::string msg;
    check(!HandwriteGenerator::checkFontAvailable("", &msg), "空路径 -> 不可用");
    check(!msg.empty(), "空路径给出提示");

    check(!HandwriteGenerator::checkFontAvailable("Z:/definitely/not/here.ttf", &msg),
          "不存在的路径 -> 不可用");
}

//=============================================================================
int main(int argc, char** argv) {
    // 无头环境下优先用 offscreen 平台插件。
    // 但如果当前目录已经部署过平台插件（跑过 windeployqt，里面通常只有 qwindows），
    // 就不强制 offscreen —— Qt 会在该目录里找不到 offscreen 插件并直接退出。
    if (!QDir(QStringLiteral("platforms")).exists()) {
        qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    }
    QGuiApplication app(argc, argv);

    std::printf("HandWrite unit tests\n");

    testConvertChinesePunctuation();
    testParseMarkdown();
    testCharOverrideRoundTrip();
    testLayoutText();
    testLineGuides();
    testLineDetection();
    testConfigRoundTrip();
    testRenderBudget();
    testFontCheck();

    std::printf("\n----------------------------------------\n");
    std::printf("passed: %d   failed: %d\n", g_pass, g_fail);
    if (g_fail == 0) std::printf("ALL TESTS PASSED\n");
    return g_fail == 0 ? 0 : 1;
}
