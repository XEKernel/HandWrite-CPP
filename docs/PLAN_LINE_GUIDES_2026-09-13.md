# 规划：作业本横线导引（Line Guides）

- 状态：**规划中，未开工**
- 提出日期：2026-09-13
- 目标版本：v2.8.0（含新功能，第二位 +1）
- 相关代码：`src/core.{hpp,cpp}`、`src/mainwindow.{hpp,cpp}`、`src/config.{hpp,cpp}`、`src/cli.cpp`

## 修订记录

| 版本 | 日期 | 变更 |
|------|------|------|
| v1 | 2026-09-13 | 初稿。假设横线是直线，方案围绕「逆 warp 映射」设计 |
| **v2** | **2026-09-13** | **按用户反馈重写**：① 纸张弯曲 → 横线是**曲线**，必须手绘；② 流程固定为「**先锚点定透视，再在照片上画横线**」。技术路线从「逆映射回 warp 源平面」改为「**bypass warp，逐字沿曲线直接绘制**」 |

---

## 1. 需求

现在已经能加载自定义背景图并用「锚点」把文字透视贴合到歪斜的纸上。但如果背景图是**作业本/笔记本的照片**，
纸上本身有印刷横线，目前文字只是按 `lineSpacing` 等距排下来，**跟纸上的横线毫无关系** ——
一眼假：字压在横线上、跨线、或者行距和线距对不上。

补充约束（用户明确）：

- **纸张会弯曲，所以照片上的横线是弯的** —— 横线必须是用户手绘的**曲线**，不能假设是直线。
- **操作顺序固定为：先用锚点定出透视 → 再在照片上绘制横线 → 文字按横线排布。**

术语统一：这套机制叫 **Line Guide（横线导引）**，一条导引曲线叫 **guide curve**（虽叫"横线"，实际是曲线）。

---

## 2. 非目标（本次不做）

| 不做 | 原因 |
|------|------|
| 竖排的竖线导引 | 竖排走「整块旋转」路径（`rotateToVertical`），改造面太大，另开一期 |
| 作文格 / 田字格的格子导引 | 需要 2D 网格数据模型，`LineGuideSet` 留了扩展位但本次不实现 |
| 导出图上「画」横线 | 照片里本来就有横线。纯色纸想画线的场景已有 `PaperTexture::HorizontalLine` |
| 自动检测横线 | 见第 8 节：先做纯手绘。检测算法留档在附录 A，需要时再做 |
| 引入 OpenCV / ONNX | 本项目零外部依赖 |

---

## 3. 现状调研

### 3.1 渲染流水线（当前）

```
layoutPages()            core.cpp:586
  ├─ layoutText()        把文本切成行（LineLayout），带 origIndices
  ├─ 背景图解码 + 缩放一次，存进 PageRenderData.backgroundImage
  ├─ 逐行累加 y，写入 lineYPositions
  │    └─ alignToGrid（core.cpp:635）：paperTexture != None 时把 y 吸附到 scaledGrid
  └─ flushPage()

renderPageStatic()       core.cpp:903
  ├─ useCalibration = bgCalibration.isValid()  （core.cpp:911）
  ├─ 校准模式：背景画到 image，文字画到独立的 textOverlay
  ├─ 非校准模式：drawPaperTexture() 画纹理（core.cpp:942）
  ├─ 逐行 drawTextWithPerturbationStatic()
  └─ 校准模式：warpMesh(textOverlay, srcGrid, dstGrid)   （core.cpp:1060）
```

### 3.2 三个坐标空间

| 空间 | 说明 | 谁在用 |
|------|------|--------|
| **A. 背景原图空间** | 照片像素坐标，`BackgroundCalibration::gridPoints` 存在这里（core.hpp:102） | 锚点校准、**guide curve（也存这里）** |
| **B. 渲染画布空间** | `scaledWidth × scaledHeight`，即 `paperWidth*rate × paperHeight*rate` | `lineYPositions`、文字绘制 |
| **C. warp 源平面** | 校准模式下**文字实际被画的平面**：一张 `rows×cols` 的**均匀网格**（core.cpp:1049-1056） | `drawTextWithPerturbationStatic` |

`warpMesh`（core.cpp:1505）做的是 **src(均匀平面 C) → dst(画布 B) 的分片 quadToQuad**，
即「文字先画在 C 上，再整体被掰弯贴到 B 上」。

### 3.3 ⭐ 关键判断：guide 启用时应当 **bypass warpMesh**

v1 的方案是「把 guide 曲线从 B 逆映射回 C，在 C 上按折线排字，warp 后落回照片横线」。
**这个绕圈在几何上是多余的**，理由：

guide 曲线是用户**直接在照片（B 空间）上描出来的**，它已经**完整包含了透视形变 + 纸张弯曲**
（用户描的是照片上肉眼看到的线，不是纸的"真实"几何）。此时：

- 逆映射 B→C 再 warp C→B，理想情况下恒等；实际因 `quadToQuad` 的分片近似、以及 `rows×cols` 网格过粗
  （常见 3×3）无法表达高频弯曲，**反而引入误差**
- 直接在 B 上沿曲线绘制才是精确的：用户画的线在哪里，字就走哪里

**结论：guide 启用时跳过 `warpMesh`，文字直接画在主画布 `image` 上。**

> 前提：guide 曲线在 **B（画布）** 空间绘制。若改在 A（原图）空间绘制，只是多一次线性缩放，结论不变。

### 3.4 那锚点还需要吗？需要 —— 两者分工正交

| 机制 | 负责 | 说明 |
|------|------|------|
| **锚点校准** | 页面几何：文字块**四角在哪、页面多斜** | 决定水平起止 x、整体包围盒、可选的水平透视压缩 |
| **guide curve** | 行级细节：每一行文字**走哪条曲线** | 表达纸张弯曲（锚点网格太粗，表达不了） |

锚点的 `rows×cols` 网格（常见 3×3）只能表达**低频的整体透视**；
纸张弯曲是**高频形变**，靠加密网格去逼近既费操作又失真 —— 这正是必须单独画横线的根本原因。

### 3.5 ⭐ 好消息：绘制循环已经是逐字的

`drawTextWithPerturbationStatic`（core.cpp:755-897）内部**本来就是逐字循环**：

```cpp
for (int i = 0; i < text.length(); ++i) {
    ...
    painter.save();                                  // core.cpp:843
    if (std::abs(perturbTheta) > 0.001) {
        painter.translate(x + perturbX, drawY);      // core.cpp:882
        painter.rotate(perturbTheta * 180.0 / M_PI);
        painter.drawText(0, 0, QString(c));
    } else {
        painter.drawText(x + perturbX, drawY, QString(c));
    }
    painter.restore();                               // core.cpp:890
    x += advance;                                    // core.cpp:894
}
```

所以让文字沿曲线走，改造量很小 —— 只需在循环内按当前 `x` 采样曲线，
把得到的 `y` 偏移与切线角叠加进 `drawY` 和 `rotate`：

```cpp
qreal curveY = 0.0, curveAngle = 0.0;
if (guideActive) curve.sample(x, &curveY, &curveAngle);

const qreal drawY = y + (curveY - curveY0) + perturbY + warpOffset;
const qreal theta = perturbTheta + curveAngle;      // 叠加，不覆盖
```

**这是相比 v1 最大的收益：绕开了逆映射这个最复杂的部分，改造量反而更小。**

### 3.6 顺带发现的现状不一致（开工时一并处理）

`alignToGrid = (m_params.paperTexture != PaperTexture::None)`（core.cpp:635）与是否校准**无关**；
但校准模式下 `drawPaperTexture` 被跳过（core.cpp:941）。

结果：用户同时开了「横线纸纹理」和「背景图 + 锚点」时，
**行仍在按纹理网格吸附，但纹理根本没画，而且文字还要再被 warp 一次** —— 得到的位置无意义。
规划：guide 启用时 `alignToGrid` 强制为 false。

---

## 4. 方案对比

### 4.1 横线几何表示

| 方案 | 能否表达弯曲 | 手绘成本 | 说明 |
|------|:---:|:---:|------|
| A. 单 y + 倾角（直线） | ✗ | 低 | v1 方案，已被否定 |
| B. 两点式直线 | ✗ | 低 | 同上 |
| **C. 采样点折线（polyline）** | ✓ | 中 | **推荐**。鼠标拖动直接产生采样点，最贴合手绘 |
| D. 贝塞尔/样条拟合 | ✓ | 中 | 存储紧凑、天然平滑，但**微调不直观**（拖控制点 ≠ 拖线本身） |

**推荐 C（采样折线）** + 绘制前平滑一次，兼顾"能表达弯曲"与"拖拽所见即所得"。

### 4.2 文字如何落位

| 方案 | 说明 | 评价 |
|------|------|------|
| A. 逆 warp 映射回 C 平面 | v1 方案 | 多余且不精确（见 3.3），**放弃** |
| **B. bypass warp，逐字沿曲线** | 3.5 的改造 | **推荐**，改造量小且精确 |
| C. 整行旋转跟随直线 | 忽略行内弯曲 | 弯曲纸上会明显穿帮 |

### 4.3 基线位置与一行占几条线

- **基线**：`baselineRatio`（0 = 贴上线，1 = 贴下线，默认 0.82）+ 未乘 rate 的像素微调 `baselineOffset`。
  手写习惯是字**坐**在下一条线上、略微悬空，硬编码一定不对（不同字体、不同本子差异大）。
- **一行占几条线**：默认 1；字号大于线距时允许 `linesPerRow = 2`（取两条 guide 的中线作基线）。
- **段首缩进**：沿用现有 `paragraphIndent`，guide 模式下仍生效。

---

## 5. 数据模型

### 5.1 新增结构（`src/core.hpp`，放在 `BackgroundCalibration` 之后）

```cpp
//=============================================================================
// 背景图横线导引（让文字排布在作业本横线内；横线可弯，故用采样折线）
//=============================================================================
struct GuideCurve {
    // 采样点，按 x 升序，存于「背景原图」坐标空间
    // （与 BackgroundCalibration::gridPoints 一致，缩放时统一乘 sx/sy）
    std::vector<QPointF> pts;

    bool usable() const { return pts.size() >= 2; }

    // 按 x 采样：线性插值出 y，同时给出该处切线角（弧度）
    // x 超出范围时夹到端点（不做外推，避免边缘乱飞）
    void sample(qreal x, qreal* y, qreal* angle) const;

    qreal yAt(qreal x) const { qreal v = 0, a = 0; sample(x, &v, &a); return v; }

    // 手绘后调用：按 x 排序、去重、移动平均平滑
    void normalize(int smoothPasses = 2);
};

struct LineGuideSet {
    bool enabled = false;

    // 关键曲线：用户手绘的 K 条（K >= 2），中间按弧长参数插值自动补全
    std::vector<GuideCurve> keyCurves;
    int  lineCount = 20;              // 总条数（含首尾关键曲线）
    bool useInterpolation = true;     // false = 只用手绘的那几条

    // --- 文字落位 ---
    double baselineRatio   = 0.82;    // 0=贴上线 1=贴下线
    int    baselineOffset  = 0;       // 未乘 rate 的像素微调
    bool   followCurve     = true;    // 逐字跟随曲线（关闭则退化为直线 + 平均倾角）
    int    linesPerRow     = 1;       // 每行文字占几条横线

    bool isValid() const {
        return enabled && (useInterpolation ? (keyCurves.size() >= 2 && lineCount >= 2)
                                            : !keyCurves.empty());
    }

    // 由关键曲线 + lineCount 生成完整曲线列表（弧长参数插值）
    std::vector<GuideCurve> build() const;
};
```

**插值为何可行**：纸张弯曲是**连续形变**，首尾两条弯曲横线之间的中间线，
用弧长参数化的线性插值就能得到很接近的结果。弯曲特别怪（中间鼓起）时，
用户再手绘 1 条中间关键曲线，即变为分段插值 —— 交互成本从"画 20 条"降到"画 2~3 条"。

`TemplateParams` 增加：

```cpp
    BackgroundCalibration bgCalibration;
    LineGuideSet          lineGuides;      // 新增
```

### 5.2 配置持久化（`src/config.{hpp,cpp}`）

沿用 `bg_calib_*` 的命名风格（平铺 double 数组）：

| 键 | 类型 | 说明 |
|----|------|------|
| `line_guide_enabled` | int(0/1) | 开关 |
| `line_guide_line_count` | int | 总条数 |
| `line_guide_interpolate` | int(0/1) | 是否在关键曲线间插值 |
| `line_guide_curves` | double[] | 每条：`n, x0,y0, x1,y1, ...`（原图坐标，n = 该条采样点数） |
| `line_guide_baseline_ratio` | double | |
| `line_guide_baseline_offset` | int | |
| `line_guide_follow_curve` | int(0/1) | |
| `line_guide_lines_per_row` | int | |

`Config` 需新增 getter/setter；**`mainwindow.cpp` 的 `saveConfiguration`/`loadConfiguration`
和 `cli.cpp` 的 `-p` 预设读取都要同步补上**（v2.7.0 刚修完的「预设不完整」坑，别再踩）。

---

## 6. 渲染流水线改动

### 6.1 `renderPageStatic()`（core.cpp:903）—— 拆分"校准"与"warp"

现状 `useCalibration`（core.cpp:911）一个标志同时控制两件事：文字画到 overlay、以及是否 warp。
改为两个独立概念：

```cpp
const bool anchorsValid = data.params.bgCalibration.isValid() && !bgImage.isNull();
const bool guidesValid  = data.lineGuides.isValid();

// 只有「锚点有效 且 没用 guide」才走 warp —— guide 已完整描述形变（见 3.3）
const bool useWarp = anchorsValid && !guidesValid;
```

- `useWarp == true`：维持现状（overlay + warpMesh）
- `guidesValid == true`：文字**直接画在 `image` 上**，跳过 overlay 与 warpMesh
- 锚点仍然生效：用于确定文字块四角（水平起止 x、包围盒）

### 6.2 `layoutPages()`（core.cpp:586）

```
if (params.lineGuides.isValid() && bgUsable) {
    // 1) guide 曲线 A(原图) -> B(画布)：乘 sx/sy
    // 2) 由 keyCurves + lineCount 插值出完整曲线列表
    // 3) 第 i 行文字 -> 第 (i % lineCount) 条曲线
    // 4) 行 y 取该曲线在 contentLeft 处的 y 作基准；行内逐字偏移在绘制时算
    // 5) 曲线用尽 -> flushPage()（每页复用同一组 guide）
    // 6) alignToGrid 强制 false
} else {
    // 现有逻辑不变
}
```

分页语义：`lineCount` 决定**单页容量**。文本行数 > `lineCount` 时翻页，第 2 页复用同一组 guide
（合理 —— 作业本每页排版一样）。

### 6.3 `drawTextWithPerturbationStatic()`（core.cpp:755）—— 核心改造

按 3.5，在已有的逐字循环里加曲线采样（新增 `const GuideCurve* curve` 参数与 rate 缩放，
因为 guide 存原图坐标而绘制在画布空间）：

```cpp
// 循环外
const bool guideActive = (curve != nullptr) && curve->usable();
const qreal curveY0 = guideActive ? curve->yAt(x0) : 0.0;   // 行首基准

// 循环内，替换 core.cpp:856 的 drawY 计算
qreal curveY = 0.0, curveAngle = 0.0;
if (guideActive) curve->sample(x, &curveY, &curveAngle);

const qreal drawY = y + (curveY - curveY0) + perturbY + warpOffset;
const qreal theta = perturbTheta + curveAngle;              // 叠加而非覆盖

// core.cpp:881 的分支条件由 perturbTheta 改为 theta
if (std::abs(theta) > 0.001) {
    painter.translate(x + perturbX, drawY);
    painter.rotate(theta * 180.0 / M_PI);
    painter.drawText(0, 0, QString(c));
} else { ... }
```

注意：删除线标记（core.cpp:867-879）的绘制逻辑同样要跟着 `theta` 走，别漏改。

### 6.4 可选增强：水平透视缩放（第 3 批）

bypass warp 后，水平方向的透视压缩（斜拍时左窄右宽）不再自动生效。
补偿：由锚点四角双线性插值出每个 x 处的水平缩放 `sx(x)`，逐字 `painter.scale(sx, 1.0)`。

先不做，等第一版出来看视觉效果再决定 —— 多数作业本照片是近似正拍的。

---

## 7. UI 交互

### 7.1 入口

`mainwindow.cpp:261-265` 的背景分组现有「选择...」「锚点」两个按钮，
在「锚点」后加 **「横线...」**，打开 `LineGuideDialog`。

- 背景图为空时置灰 + tooltip「请先选择背景图片」
- 锚点未标定时**允许**打开，但顶部给提示条：「建议先用『锚点』标定页面四角，否则横线定位可能偏差」

### 7.2 `LineGuideDialog`

**复用 `CalibrationDialog`（mainwindow.hpp:95）的画布骨架**：
`m_canvas` + `m_drawRect` + `toImageCoords/toWidgetCoords` + `relayoutCanvas`（v2.7.0 已改成真实布局 + resize 重算）。
建议把这套「图片画布 + 坐标换算」抽成 `ImageCanvasWidget` 基类供两个对话框共用，避免第三次重复。

```
┌────────────────────────────────────────────────────┐
│  [图片画布]                                         │
│   · 半透明红色实线 = 已确认的 guide                 │
│   · 亮黄 = 当前正在绘制/选中的曲线                  │
│   · 端点手柄可拖拽；整条曲线可整体上下拖动           │
│   · 鼠标位置显示坐标 + 放大镜（弯曲处需要）          │
├────────────────────────────────────────────────────┤
│ 操作: [画第1条] [画最后1条] [再加1条关键线]         │
│       [重画选中] [删除选中] [清空] [撤销]            │
│ 条数: [ 20 ▲▼ ]   ☑ 关键曲线间自动插值              │
│ 基线 [=======●==] 0.82    微调 [  0 ] px            │
│ ☑ 逐字跟随曲线弯曲        每行占 [1] 条线            │
├────────────────────────────────────────────────────┤
│ ⓘ 建议：先用「锚点」标定四角，再来画横线            │
│              [ 应用到预览 ]  [ 确定 ]  [ 取消 ]      │
└────────────────────────────────────────────────────┘
```

交互流程（对应"先锚点、再横线"）：

1. 点「画第 1 条」→ 在照片上**沿第一条横线按住拖动**，松开即完成（拖动过程采点）
2. 点「画最后 1 条」→ 同理沿最后一条横线拖动
3. 填「条数」→ 中间曲线自动生成（弧长参数插值），实时在画布上叠显预览
4. 中间明显不对（纸中间鼓起）→「再加 1 条关键线」→ 分段插值
5. 可点选任一曲线拖动微调；`Delete` 删除
6. 「应用到预览」→ 主窗口预览实时刷新（复用现有 500ms 防抖自动预览）

手绘质量保障：

- 拖动采点**按像素间距抽稀**（如 ≥4px 一个点），避免点过密
- 松开后 `normalize()`：按 x 排序 → 去重 → 移动平均平滑 2 轮
- 提供**放大镜**：光标附近 3× 放大，弯曲的线才画得准

### 7.3 参数面板

基线滑块**必须**（理由见 4.3）。「逐字跟随曲线」开关用于在弯曲过度导致字形怪异时降级为直线。

---

## 8. 分批实施计划

| 批次 | 内容 | 产出 | 版本 | 状态 |
|------|------|------|------|------|
| **1. 骨架 + 曲线跟随** | `GuideCurve`/`LineGuideSet`、Config 持久化、`drawTextWithPerturbationStatic` 逐字曲线采样、`useWarp` 拆分、**先不做 UI**（用预设/配置文件喂数据验证） | 能渲染出沿弯曲横线的文字 | 2.8.0 | ✅ **已完成（2026-09-25）** |
| **2. UI 与手绘** | 抽取 `ImageCanvasWidget`、`LineGuideDialog`、拖动采点 + 平滑 + 关键曲线插值、基线滑块 | 用户可自助标定整页 | 2.8.0 / 2.8.1 | 待开工 |
| **3. 水平透视补偿** | 锚点四角插值出 `sx(x)`，逐字 `painter.scale` | 斜拍照片也自然 | 2.9.0 | 待开工 |
| **4. 自动检测（可选）** | 附录 A 的算法 + 候选线确认 | 「自动检测」按钮 | 2.9.0 | 待开工 |
| **5. 扩展** | 作文格/田字格 2D 导引、竖排竖线导引 | — | 之后 | 待开工 |

### 批次 1 实施记录（2026-09-25）

- 零告警；`ctest` **152/152 通过**（本批新增 64 项断言）
- 端到端验证：程序生成一张 900×1200 的"作业本照片"（横线中间下凹 26px 模拟纸张弯曲），
  预设只给**首尾 2 条关键曲线** + `lineCount=22`，中间 20 条由弧长插值生成 ——
  渲染结果每一行都坐在横线上且**跟随弯曲**；把 `lineCount` 改成 7 后正确翻页（3 页），第二页从第一条横线重排
- 实施中新发现并修复一个**既有 bug**：`Config` 的 double 数组若值恰好全是整数会被存成
  int 数组，按 double 读回时返回空 → 锚点、导引曲线这类整数像素坐标会整组丢。
  已在 `getDoubleArray` 统一兼容两种存储，并加回归用例
- 工程踩坑（已记入项目记忆）：**Git Bash 里跑 `robocopy` 必须 `MSYS_NO_PATHCONV=1`**，
  否则 `/MIR`、`/XD` 会被 MSYS 当成路径转换成 `F:/Git/MIR` 之类，robocopy 报错但被重定向吞掉，
  造成"同步成功、构建零告警"的假象 —— 实际构建的一直是旧代码。

批次 1 刻意不做 UI：先用配置文件喂一组 guide 坐标 + 单测验证渲染正确，
**避免在 UI 里调试几何问题**（画布坐标、rate 缩放、原图/画布换算这类 bug 在 UI 里极难定位）。

---

## 9. 风险与对策

| 风险 | 影响 | 对策 |
|------|------|------|
| 手绘曲线抖动 → 文字上下跳 | 明显不自然 | 采点抽稀 + 2 轮移动平均；必要时提供「平滑强度」 |
| 曲线采样在字符间不连续（x 跳变） | 字与字之间出现台阶 | `sample()` 用线性插值而非最近点；按字符中心 x 采样 |
| 弯曲过强时字符旋转过度 | 字变形怪异 | `followCurve` 可关；或对 `curveAngle` 限幅（如 ±8°） |
| 字号 > 线距 | 字压线、出格 | `linesPerRow = 2`；UI 按 `fontSize` 与线距给黄色提示 |
| 关键曲线插值在非线性弯曲下偏差 | 中间几行穿帮 | 支持「再加 1 条关键线」分段插值 |
| bypass warp 后水平透视丢失 | 斜拍时右侧字偏大 | 第 3 批的 `sx(x)` 补偿 |
| guide 与 `paperTexture` 网格吸附冲突 | 位置无意义 | guide 启用时强制 `alignToGrid=false`（见 3.6） |
| 20 条曲线的配置数据量大 | 预设文件变大 | 抽稀后每条约 20~40 点，20 条 ≈ 1600 个 double，可接受 |

---

## 10. 验收标准

1. 弯曲的作业本照片 + 手绘 2 条关键曲线 + 20 条 → 每行文字沿曲线走，不压线、不跨线
2. 单条曲线大幅弯曲时，行内首尾字符跟随切线角旋转，肉眼无台阶
3. 同一文本同种子，预览 / PNG / PDF / SVG 一致（v2.7.0 已保证的机制不被破坏）
4. 预设保存 → 加载往返，曲线点数与坐标不丢
5. `ctest` 新增用例：`sample()` 插值与边界夹取、`normalize()` 排序去重、关键曲线插值、A→B 坐标换算
6. 三个目标仍**零 warning**

---

## 11. 已决策 / 待决策

### 已决策（2026-09-13）

| # | 决策 |
|---|------|
| 1 | 横线是**曲线**，必须手绘（纸张会弯曲） |
| 2 | 流程固定为：**先锚点定透视 → 再画横线 → 文字按横线排布** |
| 3 | **不做**逆 warp 映射；guide 启用时 bypass `warpMesh`，直接在画布上逐字沿曲线绘制 |
| 4 | 自动检测**暂不做**（先做纯手绘） |
| 5 | 不引入外部依赖 |

### 待你确认

1. **一行占几条线**：作业本常见是「一行字占一条线」，但线距很密的本子（如 7mm）用 30px 字会顶线。
   `linesPerRow = 2` 第 1 批就做，还是等遇到再说？
2. **弯曲限幅**：`followCurve` 打开时是否对切线角限幅（如 ±8°）？
   不限幅最真实，但用户画线手抖会造成怪异扭曲。
3. **guide 与锚点的强制关系**：是否要求「必须先标定锚点才能画横线」？
   我倾向**不强制**（只提示）—— 正拍照片不需要锚点也能画横线。
4. 锚点网格密度提示：bypass warp 后网格密度对文字已无影响（只影响四角定位），
   所以 v1 里设想的「建议 4×4」提示**不需要了**，确认一下这个判断。

---

## 附 A：自动检测算法（第 4 批，暂不做，留档）

零外部依赖，纯手写：

1. **ROI**：用户在画布上框一个矩形（默认整图），排除页眉、装订孔、阴影边缘
2. **灰度化**：`lum = 0.299R + 0.587G + 0.114B`
3. **逐行统计**：ROI 内每行 y，统计"暗于该行中位数 T 以上"的像素占比 `r(y)`
   - 浅蓝/灰色横线在灰度上比纸暗
   - **红色横线灰度差很小 → 检测必然失败**，此时明确提示改手动，不硬猜
4. **平滑**：窗口 3 移动平均
5. **峰值检测**：`r(y)` 局部极大值且 > 全局均值 × 1.5
6. **等距规则化（鲁棒性的主要来源）**：
   - 取相邻峰间距中位数 `d`
   - 以首个峰为起点按 `d` 生成 `floor(ROI高 / d)` 条等距线
   - 这一步能补上被字迹/污渍遮挡而漏检的线
7. 输出候选 → 画布叠加显示，用户可拖拽/增删/改条数；**检测完不直接使用**

注：检测出的是**直线**（逐行统计没有 x 分辨率）。要得到**弯曲**的曲线，
需再对每条候选线做「逐列局部最优 y」跟踪（在首峰 y ± d/3 范围内找每列最暗的 y）——
这也是它放到第 4 批的原因之一。

---

## 附 B：涉及文件清单（开工时对照）

| 文件 | 改动 |
|------|------|
| `src/core.hpp` | 新增 `GuideCurve` / `LineGuideSet`；`TemplateParams::lineGuides`；`drawTextWithPerturbationStatic` 加 `curve` 参数 |
| `src/core.cpp` | `drawTextWithPerturbationStatic` 逐字曲线采样（core.cpp:856/881）；`renderPageStatic` 的 `useWarp` 拆分（core.cpp:911）；`layoutPages` 排 Y 分支（core.cpp:586）；`alignToGrid` 互斥（core.cpp:635） |
| `src/config.{hpp,cpp}` | 8 个新键的 getter/setter |
| `src/mainwindow.hpp` | `LineGuideDialog`；抽取 `ImageCanvasWidget`；`m_lineGuides` 成员 |
| `src/mainwindow.cpp` | 「横线...」按钮 + 对话框；拖动采点/平滑/插值；预设存取补新键 |
| `src/cli.cpp` | `-p` 预设补 `line_guide_*` |
| `tests/test_core.cpp` | `sample()` / `normalize()` / 关键曲线插值 / A→B 换算 |
| `CHANGELOG.md` | 2.8.0 小节（**v2.7.0 起的约定：发版必须写**） |
