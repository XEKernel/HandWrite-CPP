# HandWrite-CPP 全项目代码审查报告

- 审查日期：2026-09-12
- 审查版本：v2.6.1（`CMakeLists.txt:2`）
- 审查范围：`src/`（10 个文件，约 3,600 行）、`ui/`、`CMakeLists.txt`、`.github/workflows/`
- 审查方式：全量静态阅读 + 交叉引用核实（符号使用点、资源存在性、CI 路径）

> **修复状态（2026-09-12 更新）**：本报告列出的 **P0 4 项、P1 9 项、P2 13 项已全部修复**，
> 并在修复过程中由新加的单元测试额外揪出 2 个此前未发现的问题（见下）。对应版本 **v2.7.0**，
> 详见 [`CHANGELOG.md`](../CHANGELOG.md)。
>
> 修复过程中新发现的、本报告未列出的问题：
> 1. **预设小数被静默截断成整数**（`config.cpp`）—— `std::stoi("0.35")` 只解析前导 `0` 就返回且不抛异常，
>    导致 `font_mix_rate` / `texture_opacity` / 各 `*_sigma` 等**所有小数参数**保存后再加载都变成 `0`。
>    已改为「必须整串被消费才算数字」，并加了回归断言。
> 2. **注释行末的反斜杠吞掉下一行代码**（`config.cpp` 的 `escapeString` 上方注释）——
>    `// 写文件时转义 " 与 \` 的末尾反斜杠让函数定义变成注释，整个 `Config` 类作用域错乱。
>    由 `-Wall`（`-Wcomment`）报出。

---

## 0. 结论摘要

| 指标 | 值 |
|------|-----|
| 整体可维护性 | 良好 —— 分层清晰（`tools` → `config` → `core` → `mainwindow`），命名一致，注释到位 |
| 架构评级 | B+ —— 引擎与 UI 分离到位，但引擎层有若干静态函数重复解码资源的性能陷阱 |
| P0 严重问题 | 4 项（含 1 项数据安全回归、1 项必然 OOM） |
| P1 功能缺陷 | 9 项（含 3 项功能实际失效） |
| P2 整洁问题 | 10 项（含大量死代码 API） |
| 测试覆盖 | 0（无单元测试、无 CI 测试步骤） |

**最需要立刻修的 3 件事**：GUI 导出会清空 `outputs/` 目录全部文件（`mainwindow.cpp:611`）、x32/x64 倍率必然内存耗尽（`core.cpp:931`）、工作线程里访问 UI 控件（`mainwindow.cpp:655`）。

---

## 1. P0 —— 严重问题

### P0-1 数据安全回归：GUI 导出会删除 `outputs/` 下所有文件

**位置**：`src/mainwindow.cpp:609-617`

```cpp
QString od="outputs"; QDir d(od);
if(d.exists()){for(const auto& f:d.entryList(QDir::Files))d.remove(f);}
```

这里遍历并删除了 `outputs/` 下的**每一个文件**，不限扩展名、不限命名。

而 v2.5.1 的变更记录明确写着"输出目录不再递归清空，改为只删除本程序生成的页码图片（如 `0.png`）"——`core.cpp:49-59` 的 `cleanGeneratedPages()` 已经做了安全的数字命名过滤：

```cpp
base.toInt(&ok); if (ok) dir.remove(name);   // 只删纯数字命名的 png
```

**结论：`cleanGeneratedPages()` 已经覆盖了这个需求，`onPushButtonExportClicked` 里的手动清空是多余的，且与安全策略直接冲突。** 如果用户在 `outputs/` 放过自己的 `作业.txt`、`参考图.png`，导出一次就没了。

**修复**：删掉这三行，把清理责任完全交给 `generateImageParallel()` 内部的 `cleanGeneratedPages()`。

---

### P0-2 高倍率必然 OOM（内存耗尽崩溃）

**位置**：`core.cpp:931-932`、`core.cpp:1035-1036`（`generatePreview` / `generateImageParallel`），以及 `mainwindow.cpp:614` 的提示

倍率直接乘在纸张像素尺寸上，且每个中间画布都是 `Format_ARGB32`（4 字节/像素）：

| 倍率 | 画布尺寸 (px) | 单张 QImage | 校准模式需 2 张 | 并行 N 线程峰值 |
|------|--------------|------------|----------------|----------------|
| x4 | 2668 × 3780 | 40 MB | 80 MB | ~0.3 GB |
| x16 | 10672 × 15120 | 645 MB | 1.3 GB | ~5 GB（4 线程） |
| x32 | 21344 × 30240 | 2.6 GB | 5.2 GB | 必然失败 |
| x64 | 42688 × 60480 | **10.3 GB** | **20.6 GB** | 必然失败 |

`x1 ~ x64` 全部列在下拉框里（`tools.cpp:48-56`），界面上只在 `rate >= 16` 时问一句"可能较慢"（`mainwindow.cpp:614`）——实际上 x32/x64 是**内存耗尽**而不是"慢"。此外校准模式下 `renderPageStatic` 会额外开一张 `textOverlay`（`core.cpp:766`），内存翻倍；并行映射时还要再乘线程数。

**修复建议**：

| 方案 | 说明 |
|------|------|
| 上限封顶 | 直接把可选倍率限制到 x8（`tools.cpp:48`），或对 `paperWidth*paperHeight*rate²` 做预算检查（例如单页 > 2 GB 就拒绝并提示） |
| 流式渲染 | 高分页按条带（tile）逐块渲染并直接写入文件，不保留整页 QImage |
| 并行限流 | 用 `QThreadPool::globalInstance()->maxThreadCount()` 与单页内存预算共同决定并发数 |
| 提示修正 | `mainwindow.cpp:614` 的文案改为"内存占用约 N GB，可能失败" |

---

### P0-3 工作线程中访问 UI 控件（未定义行为）

**位置**：`src/mainwindow.cpp:655-658`

```cpp
QFuture<bool> future=QtConcurrent::run([this,p,path](){
    HandwriteGenerator g; g.modifyTemplateParams(p);
    return g.exportPdf(getTextFromTextEdit().toStdString(),path.toStdString());  // ← 工作线程里读 UI
});
```

`getTextFromTextEdit()` 内部是 `ui->textEditMain->toPlainText()`，在 `QtConcurrent` 的工作线程里调用属于跨线程访问 QWidget，是 Qt 明确的未定义行为（可能崩溃，也可能读到撕裂数据）。

**同文件里的其他导出路径写法是正确的**（`onPushButtonExportClicked` 先在主线程取好 `text` 再捕获进 lambda），说明这是漏改。

**修复**：与 `onPushButtonExportClicked` 保持一致，在 lambda 外把 `getTextFromTextEdit()` 的结果取好再捕获。

---

### P0-4 每一页都重新读盘并解码整张背景图

**位置**：`src/core.cpp:776-782`（位于 `renderPageStatic` 内部）

```cpp
if (!data.params.backgroundImagePath.empty()) {
    bgImage = loadImageWithWebpFallback(data.params.backgroundImagePath);   // 每页都执行
```

`renderPageStatic` 是**每页调用一次**的静态函数，还被 `QtConcurrent::mapped` 并行调用。于是：一页 = 一次文件 I/O + 一次完整图片解码（WebP 还要走 libwebp 全量解码）。10 页文档对同一张 4000×3000 的照片解码 10 次；并行时还会多线程同时读同一文件。

**修复**：把解码后的 `QImage` 提到 `PageRenderData` 里（`layoutPages` 时解码一次），或用 `QCache<QString, QImage>` + `QMutex` 做进程内缓存。

---

## 2. P1 —— 功能缺陷

### P1-1 `layoutText` 内存泄漏 + 字体度量串用

**位置**：`src/core.cpp:424-442`、`core.cpp:490`

```cpp
bool hasFontOverride = false;
QFontMetrics* fmPtr = &fm;
for (const auto& span : spans) {
    hasFontOverride = span.fontSizeOverride > 0;      // 每轮被覆盖
    if (hasFontOverride) { ...; fmPtr = new QFontMetrics(overrideFont); }  // 每轮 new
    ...
}
if (hasFontOverride) delete fmPtr;                     // 最多只 delete 一个
```

两个独立缺陷：

1. **泄漏**：`hasFontOverride` 是循环变量，最终值由**最后一个 span** 决定。若最后一个 span 无覆盖 → 一次都不 delete，之前 `new` 的全部泄漏。
2. **度量串用**：普通 span 分支里**没有把 `fmPtr` 复位成 `&fm`**。所以文档里一旦出现过 `# 标题`，后面所有正文的字符宽度都用标题的 48px 度量计算 → 正文行超宽提前换行、排版错乱。

**触发条件**：文档使用 Markdown 标题（`#` / `##`）。

**修复**：改用值语义（`QFontMetrics overrideFm(overrideFont);` + 指针在 span 结束时复位），或把整个循环重构成按 span 计算一行宽度、不跨 span 持有 `new` 对象。

---

### P1-2 Markdown 删除线不闭合，后续文本被误加删除线

**位置**：`src/core.cpp:119-131`

```cpp
if (i + 1 < text.length() && text[i] == '~' && text[i+1] == '~') {
    flush(); currentStyle = TextStyle::Strikethrough; i += 2; continue;      // 分支 A
}
if (currentStyle == TextStyle::Strikethrough &&
    i + 1 < text.length() && text[i] == '~' && text[i+1] == '~') {           // 分支 B
    flush(); currentStyle = TextStyle::Normal; i += 2; continue;
}
```

分支 B 的条件被分支 A 完全包含（都是 `~~`），且分支 A 以 `continue` 结束 → **分支 B 永远不可达**。`currentStyle` 一旦进入 `Strikethrough` 就再也回不到 `Normal`（只有换行会重置）。

**实际表现**：`~~写错了~~ 这段是对的` → `写错了` 和后面的 `这段是对的` **都**被划掉。一行里有两处删除线时同样全部连通。

**修复**：把分支 B 的判断提到分支 A 之前（先判"是否正在删除线内"），或改成状态机式的 `if (inStrike) {关闭} else {开启}`。

---

### P1-3 中文标点被静默替换为 ASCII，且开关在 GUI 中不存在

**位置**：`core.cpp:25-46`（`convertChinesePunctuation`）、`core.cpp:436`（调用点）、`core.hpp:162`（参数定义）

`convertChinesePunctuation()` 把 `。，！？：；（）【】《》` 和全角引号全部替换为 ASCII 等价字符。调用条件是：

```cpp
QString converted = m_params.preserveChinesePunctuation ? span.text : convertChinesePunctuation(span.text);
```

而 `preserveChinesePunctuation` **默认 `false`**，并且在 `getParamsFromForm()`（`mainwindow.cpp:514-558`）、`saveConfiguration()` / `loadConfiguration()`（`mainwindow.cpp:771-844`）里**完全没有出现**——即 GUI 用户永久无法关闭这个替换。

对手写作业这种全中文场景，`。` 被渲染成英文句点、`，` 变成半角逗号、`《》` 变成 `<>`，观感损失明显。

**修复**：在「段落排版」分组加一个复选框「保留中文标点」（默认**开启**更合理），并接入配置读写。

---

### P1-4 字符级覆盖的索引错位

**位置**：`core.cpp:596-599`（索引生成） + `mainwindow.cpp:859-877`（索引来源）

`mainwindow.cpp` 用 `QTextEdit` 的光标选区 `selectionStart/End` 建立 `CharacterOverrideRange`，而 `core.cpp` 的 `globalCharIndex` 是在 `layoutText` **处理之后**重新累加的：

```cpp
for (int i = 0; i < lineText.length(); ++i) lineCharIndices.push_back(globalCharIndex++);
```

但 `layoutText` 会做两件改变字符数的事：

1. 段首缩进插入 **2 个全角空格**（`core.cpp:460`）；
2. Markdown 标记被剥离（`#`、`~~`、`---` 都不进入行文本）。

所以只要文档有段落缩进或 Markdown 标记，**选区索引与渲染索引就对不上**，用户给第 3 段第 5 个字设的"放大/旋转"会作用到别的字上。

**修复**：在 `layoutText` 里保留"渲染索引 → 原文索引"的映射表，`charIndexMap` 存原文索引而不是顺序号。

---

### P1-5 预设保存/加载不完整，多个参数会丢失

**位置**：`mainwindow.cpp:771-807`（存） / `mainwindow.cpp:809-844`（读）

| 参数 | 存 | 读 | 后果 |
|------|:--:|:--:|------|
| `textDirection`（竖排） | ✗ | ✗ | 存了竖排预设，加载回来变横排 |
| `textWarp`（弧形/波浪/圆形） | ✗ | ✗ | 变形设置丢失 |
| `bgCalibration`（四点锚点） | ✗ | ✗ | 重开程序要点一遍四个角 |
| `charOverrides`（字符覆盖） | ✗ | ✗ | 逐字微调全部白做 |
| `fontMixList` | ✗ | ✗ | （本身也是死功能，见 P1-6） |
| `preserveChinesePunctuation` | ✗ | ✗ | 见 P1-3 |
| `strikeThroughRate` / `inkBleed` / `strokeWidthSigma` / 纹理 | ✓ | ✓ | 正常 |

`Config` 类里也没有这些键的访问器（`config.hpp:36-77`）。

**修复**：给 `Config` 补齐 `text_direction`、`text_warp`、`bg_calib_points`（用 double 数组）、`char_overrides`（可用 `range:key=value` 序列化），并在存/读两侧接线。

---

### P1-6 「混合字体」是彻底无法启用的死功能（但 README 把它列为特性）

**位置**：`core.hpp:131`、`core.cpp:316-334`

全仓库搜索 `fontMixList` 的赋值点：**0 处**。它只在 `core.hpp` 声明、在 `core.cpp` 里被读取，从未被填充。对应地 `pickMixedFont()` 里的 `if (fontMixList.empty()) return baseFont;` 永远成立 → 恒定走原字体分支。

而 `README.md:36` 写着"混合字体"，`build.yml:106` 的 Release 说明里也写着 "Font mixing"。

**修复**：要么在 UI 里加一个多选列表（从 `ttf_library` 勾选若干字体 + 一个"混合比例"滑块），要么删掉这条特性宣传。**顺带**：`pickMixedFont` 用的是 `thread_local` 随机源（`core.cpp:320`），与页面 RNG 无关 —— 就算启用，**预览和导出结果也会不一致**（线程数/调用顺序不同）。应改为传入 `PageRenderData::rng`。

---

### P1-7 文字变形是"整行平移"，不是逐字变形

**位置**：`core.cpp:839-859`

```cpp
double phase = (x - centerX) / lineWidth;   // x 此刻恒等于 contentLeft，是常量
warpY -= std::sin(phase * M_PI) * data.scaledHeight * 0.12;
```

`x` 在这一行之前被赋为 `contentLeft`（`core.cpp:834`）且此后不再改变，所以 `phase` 是**每行一个常数**，`sin()` 的结果也是常数 → 所谓"弧形/波浪"只是把**整行**上下平移了一个固定量，行内字符没有任何弯曲。

另外 `warpY` 修改后，页面溢出的判断仍用原始 `y`（`core.cpp:832` 在 warp 之前），变形后可能越出下边距。

**修复**：把变形下推到逐字绘制处（`drawTextWithPerturbationStatic` 内按累计 x 计算偏移），或对整行做 `QPainterPath` 文字路径 + 逐段位移（同时能顺带修好"圆形"模式与预计算行位的冲突）。

---

### P1-8 进度对话框无法取消

**位置**：`mainwindow.cpp:1362-1368`

```cpp
m_progressDialog=new QProgressDialog(title,tr("取消"),0,maximum,this);
m_progressDialog->setCancelButton(nullptr);      // 构造时给了"取消"，随即又禁用
```

同时 `setWindowModality(Qt::WindowModal)`。结果是：一旦点了 x32 导出或大文档渲染，用户**只能等内存耗尽或强杀进程**（`closeEvent` 里虽有确认框，但模态进度框挡住了它）。

**修复**：保留取消按钮，接 `canceled()` 信号 → `m_exportWatcher->cancel()`；`generateImageParallel` 的轮询循环里检查 `future.isCanceled()` 提前退出。

---

### P1-9 预览与导出结果不可复现（没有随机种子）

**位置**：`core.cpp:537-538`（`layoutPages` 内 `std::mt19937 seedRng(rd())`）、`core.cpp:320`（混合字体用 thread_local RNG）

每次预览都重新播种，且预览按 `min(rate, 4)` 渲染、导出按完整 `rate` 渲染（`mainwindow.cpp:490`、`573`），抖动序列不同 → **屏幕上看到的效果和最终导出的图不一致**。用户无法"看着满意了就导出这个效果"。

**修复**：加 `int seed` 参数（0 = 随机），预览与导出共享同一种子；UI 上放一个"随机种子"输入框 + 「骰子」按钮，让用户能复现满意的效果。

---

## 3. P2 —— 整洁性与小问题

| # | 位置 | 问题 |
|---|------|------|
| P2-1 | `mainwindow.cpp:1403` | `QPixmap(":/resources/app.ico")` —— 全仓库**没有任何 `.qrc` 文件**（`CMakeLists.txt:52` 的 `RESOURCE_FILES` 为空），该路径必定落空，「关于」对话框图标是空白的。改用 `applicationDirPath()+"/app.ico"` 或补一个 qrc |
| P2-2 | `core.hpp` / `mainwindow.hpp:164` | 死代码：`findUnsupportedChars`、`generateImage`、`exportSvg`、`setPaperSize`、`setMargins`、`setSpacing`、`setColors`、`setPerturbations`、`setRate` 各自**只有定义处 1 处引用**；`m_exportRate` 只被写、从未被读。共约 130 行可删 |
| P2-3 | `core.cpp:1050-1059` | `while(!future.isFinished()){ msleep(50); for(i..)isResultReadyAt(i) }` —— 忙等待轮询。应改用 `QFutureWatcher` / `future.progressValueChanged` |
| P2-4 | `core.cpp:949-961` vs `1006-1018` | 竖排旋转 + 居中裁切逻辑**完整重复两份**，应提取成 `static void rotateAndCenter(QImage&, int w, int h)` |
| P2-5 | `config.cpp:28-47` | `[1, 2.5]` 这类混合数组：一旦遇到含 `.` 的项就把 `isDouble` 置真，**之前已 push 进 `intArray` 的整数全部丢失**（最终只写 `doubleArray`） |
| P2-6 | `README.md:83` vs `mainwindow.cpp:741` | 预设扩展名不一致：README/CLI 文档写 `.conf`，GUI 的列表过滤和文件对话框用 `*.toml`。且这格式并不是合法 TOML 的常见子集（无节、无转义）。统一成一种并更新文档 |
| P2-7 | `mainwindow.cpp:733-736` | `presetDir()` 用 `applicationDirPath()/presets` —— 若程序装在 `Program Files` 下会写不进（只读），且多用户共用。应移到 `QStandardPaths::AppDataLocation` |
| P2-8 | `mainwindow.cpp:933-1052` | `CalibrationDialog` 全部用 `setGeometry` 绝对定位、无 layout，`setFixedSize` 写死 760×520。高 DPI / 系统字体放大时按钮必然重叠。另外 `m_lblCol->setStyleSheet("color: white;")` 把颜色写死（深色主题没问题，浅色主题会看不见） |
| P2-9 | `core.cpp:1003`、`1046` | `QThreadPool::globalInstance()->setMaxThreadCount(...)` 修改**全局**线程池，会波及其他 `QtConcurrent` 使用者。建议用局部 `QThreadPool` 实例 |
| P2-10 | `core.cpp:917-928` | `loadFont` 里 `addApplicationFont` 失败时静默回退到系统默认字体，用户看到"字体不对"却无从得知。应返回错误并提示（`findUnsupportedCharsStatic` 的思路可复用） |
| P2-11 | `mainwindow.hpp:6` 和 `:21` | `#include <QDialog>` 重复 |
| P2-12 | 全仓库 | 无单元测试、无 `.clang-format`、无 `-Wall -Wextra` 开启（`CMakeLists.txt` 未设 `target_compile_options`）。CI（`build.yml`）只构建不测试 |
| P2-13 | `build.yml:57` | `cp /ucrt64/bin/*.dll .` 把整个 MSYS2 运行时的 DLL 全拷进包，体积臃肿。应只拷 `windeployqt` + `ldd HandWrite.exe` 实际依赖 |

---

## 4. 可添加的功能

按"投入产出比"排序，分四组。**★ = 建议优先做**。

### 4.1 拟真度（产品核心价值，收益最高）

| 功能 | 说明 | 落点 |
|------|------|------|
| ★ 逐字文字变形 | 把 P1-7 修成真正的按字符位置弯曲，并扩展到"每行随机基线倾斜"（模拟手写行不水平） | `drawTextWithPerturbationStatic` |
| ★ 涂改重做 | 现在只有"划线删除"。补：黑色墨块涂抹、矩形涂改框、括号改正符号（⌒）、"写错重写"（整字画圈） | `core.cpp` 笔触段 |
| ★ 笔压模拟 | 按字宽/笔画数调整 `penWidth`，实现起笔顿、收笔提的粗细变化；配合已有的 `strokeWidthSigma` | 同上 |
| ★ 纸张老化 | 叠加折痕、污渍、阴影渐变、扫描噪点、边缘暗角；配合已有背景照片能显著提升"真作业本"观感 | `renderPageStatic` 后处理 |
| 半页/断行真实感 | 每页最后一行随机中断（模拟时间不够/翻页），支持"下一行从半格开始" | `layoutPages` |
| 字距行距渐变 | 行首紧凑、行尾渐松；行内字距随累计位置缓慢漂移（手写越写越挤） | `layoutPages` + 绘制 |
| 中英数字配对 | 中文手写体 + 印刷体数字/字母自动组合（作业本上常见），或让用户为 ASCII 单独指定字体 | 字体选择 |
| 竖排真竖排 | 目前是"整块渲染后旋转 90°"。改成真正的逐列竖排（标点旋转、列序从右向左、标点占位规则） | 需要重写竖排路径 |
| 随机种子复现 | 见 P1-9 | 全链路 |

### 4.2 排版与文本能力

| 功能 | 说明 |
|------|------|
| ★ 中文标点规则补全 | 避头尾（标点不出现在行首）、省略号/破折号占两格、引号成对置位。目前只有 P1-3 的"转 ASCII"和一句"整字换行" |
| ★ 正文/标题/注释多字体 | 用 Markdown span 已有结构（`StyledSpan`）扩展：标题用手写楷书、正文用行书、批注用铅笔体 |
| Markdown 完整化 | 现在支持 `#`/`~~`/`---`。补：`*强调*`（加粗/换圈）、`- 列表`（手写序号）、`> 引用`（缩进）、行内 `代码` |
| 表格与公式 | 手写表格（画线 + 单元格内排字）、简单公式（上下标、分数） |
| 插图/贴纸 | 在指定段落插入图片或手绘图形（贴纸、勾选框、五角星） |
| 页眉页脚 | 姓名/学号/日期/页码，支持"每页自动递增的页码"（作业本刚需） |

### 4.3 交互与工作流

| 功能 | 说明 |
|------|------|
| ★ 参数级撤销/重做 | 现在只有文本框支持 undo/redo。参数改动（拖动滑块、切换模板）也应可撤销 |
| ★ 可取消的长任务 | 见 P1-8，配一个真实的取消按钮 + 进度百分比 |
| ★ 并行对照预览 | 左右分屏对比两组参数（抖动强度 A/B），一键取优 |
| 参数面板折叠 + 搜索 | 设置项已达 30+，分组可折叠（QToolBox/可折叠 QGroupBox），顶部加过滤框 |
| 拖放 txt/md | 现在拖入文件只认图片当背景（`dropEvent`）。应智能识别：图片→背景，文本→内容 |
| 最近文件 + 记忆 | `QSettings` 记住窗口大小、最近目录、上次预设 |
| 多标签/多文档 | 同时编辑多份作业文本，各自独立参数 |
| 深色/浅色主题 | 现在 `CalibrationDialog` 等写死颜色（P2-8）。加主题切换 |
| 批量 GUI | CLI 有 `-b` 批处理，GUI 应支持"导入多个 txt → 批量导出 ZIP" |
| 中英双语 | 现在全部硬编码中文 |

### 4.4 导出与工程

| 功能 | 说明 |
|------|------|
| ★ 导出格式扩展 | jQuery/WebP/TIFF 单页 + 多页 PDF（已有）+ **SVG（多页，现在只导第 1 页，`core.cpp:1121`）+ DOCX + ZIP 打包** |
| ★ 真实进度与预估 | 导出前估算页数、耗时、内存占用（配合 P0-2 的预算检查） |
| CLI 参数补全 | 现在 `-p` 预设只读基础项（`cli.cpp:99-117`），纹理/效果/方向全丢。补 `--texture --ink-bleed --direction --warp --seed --json`，并支持 `--preset-list` 列出内置预设 |
| 内置预设库 | 随包提供若干"语文作业/数学作业/英文抄写"预设 |
| 单测 + 静态检查 | Catch2/GoogleTest 覆盖 `layoutText`、`parseMarkdown`、`convertChinesePunctuation`、`Config` 往返；CI 加 `-Wall -Wextra` 与 `clang-tidy` |
| 崩溃日志 | `qInstallMessageHandler` 落盘日志，便于用户反馈 |

---

## 5. 建议的修复顺序

| 批次 | 内容 | 理由 |
|------|------|------|
| **第 1 批（数据安全 / 稳定性）** | P0-1 清空目录、P0-2 内存封顶、P0-3 跨线程 UI、P0-4 背景图重复解码 | 前两项会造成用户数据损失与崩溃 |
| **第 2 批（功能正确性）** | P1-1 度量泄漏、P1-2 删除线、P1-3 中文标点开关、P1-5 预设完整性、P1-8 可取消 | 都是"用户点得到但结果是错的" |
| **第 3 批（体验与拟真）** | P1-9 随机种子、P1-7 逐字变形、P1-4 覆盖索引、4.1 组功能 | 提升产品核心价值 |
| **第 4 批（整洁）** | P2 全部 + 4.4 组单测/CI | 降低后续维护成本 |
| 版本号 | 第 1、2 批属 BUG 修复 → 2.6.2；第 3 批含新功能 → 2.7.0 | 遵循项目版本规范 |

---

## 附：审查覆盖清单

| 文件 | 行数 | 状态 |
|------|------|------|
| `src/core.hpp` | 291 | 已审 |
| `src/core.cpp` | 1,202 | 已审（含逐行逻辑推演） |
| `src/config.hpp` | 87 | 已审 |
| `src/config.cpp` | 192 | 已审 |
| `src/tools.hpp` | 97 | 已审 |
| `src/tools.cpp` | 264 | 已审 |
| `src/mainwindow.hpp` | 220 | 已审 |
| `src/mainwindow.cpp` | 1,418 | 已审 |
| `src/cli.cpp` | 187 | 已审 |
| `src/main.cpp` | 24 | 已审 |
| `ui/mainwindow.ui` | — | 已审（控件命名） |
| `CMakeLists.txt` | 184 | 已审 |
| `.github/workflows/build.yml` | 120 | 已审 |
| `.github/workflows/pages.yml` | — | 已审 |
