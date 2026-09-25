# HandWrite Generator

手写作业生成器 — 使用 C++/Qt6 编写，通过自然扰动渲染文本以模拟手写效果。

![Platform](https://img.shields.io/badge/platform-Windows-blue)
![Qt](https://img.shields.io/badge/Qt-6.11-green)
![C++](https://img.shields.io/badge/C++-17-blue)

## 特性

### 纸张纹理
支持 6 种纸张模板：横线纸、方格纸、田字格、作文纸、点阵纸、纯色背景，可调透明度。
选择纹理后自动对齐文字到网格线。

### 背景图片 + 透视校准
- **自定义背景** — 支持加载纸的照片作为背景
- **四点锚点校准** — 点击纸的四个角，文字通过透视变换自动映射到歪斜纸张上
- 即使照片中的纸位置不正、角度倾斜，文字也能准确贴合
- **横线导引** — 沿作业本上的印刷横线描线，文字逐行排布在横线内。
  纸张弯曲时横线是弯的，所以是手绘曲线：**按住鼠标沿横线拖过去**，只需描第 1 条和最后 1 条，
  中间按条数自动插值（2 条关键曲线 → 整页 N 条）；文字逐字跟随曲线弯曲与倾斜。
  建议先用「锚点」标定四角再描线 —— 锚点管页面四角几何，横线管每行的弯曲，两者配合最好
  （正拍照片可跳过锚点）

### 笔触效果
- **笔画粗细扰动** — 模拟用力不均
- **墨水洇染** — 笔迹边缘羽化效果
- **涂改模拟** — 随机划线删除

### 字体
- **手写字体库** — 把 `.ttf` 放进 `ttf_library/` 目录，程序启动时自动加载
- **混合字体** — 可指定若干附加字体与出现概率，按字符随机切换，模拟同一页里笔迹不一致的效果
  （GUI「混合字体...」按钮；配合随机种子可复现）

### 排版增强
- 段首缩进两字符
- 段间距独立设置
- 中文标点保留/转 ASCII 开关（默认保留）
- 中文避头尾（禁则）：标点不会孤悬行首
- 竖排、文字变形（弧形/波浪/环形）+ 变形强度
- Markdown 轻标记：`# 标题`、`~~删除线~~`、`---` 分割线

### 复现
- **随机种子** — 设置种子后预览与导出结果逐字节一致；留空则每次随机
  GUI 有「种子」输入框与「随机」按钮，CLI 用 `-s/--seed`

### UI 功能
- 实时自动预览（参数变化 500ms 后刷新）
- **可拖拽分隔条** — 预览区和设置栏宽度自由调节
- PNG / PDF / **SVG（多页）** 导出
- 多预设管理（保存/加载/删除），预设存于 `QStandardPaths::AppDataLocation/presets`
- 字符级别属性覆盖
- 缩放、翻页、打印
- 长任务进度对话框**可取消**

## 构建

### 依赖
- MSYS2 (ucrt64)
- GCC 16+
- CMake 3.16+
- Qt 6.11+

### 安装依赖
```bash
pacman -S mingw-w64-ucrt-x86_64-gcc \
          mingw-w64-ucrt-x86_64-cmake \
          mingw-w64-ucrt-x86_64-ninja \
          mingw-w64-ucrt-x86_64-qt6-base
```

### 编译
```bash
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja
```

### 跑测试
单元测试目标 `handwrite-tests` 默认随主工程一起构建（不在 Windows 时同样可用）：
```bash
cd build
ctest --output-on-failure
# 或直接跑
QT_QPA_PLATFORM=offscreen ./handwrite-tests
```
关闭测试：`cmake .. -DHANDWRITE_BUILD_TESTS=OFF`。
代码格式化：仓库根目录已提供 `.clang-format`（`clang-format -i src/*.cpp src/*.hpp tests/*.cpp`）。
## 使用

### GUI
双击 `HandWrite.exe`，或:
```powershell
cd G:\build\build_gcc
.\HandWrite.exe
```

#### 背景图片校准
1. 点击「选择...」载入纸的照片
2. 点击「锚点」打开校准窗口
3. 依次点击纸的**四个角**：左上 → 右上 → 右下 → 左下
4. 可拖拽红点微调，点击「**确定**」保存
5. 预览中的文字将通过透视变换贴合到纸上

### CLI
```bash
# 基本用法
handwrite-cli -t "文本内容" -o ./output -r 2

# 从文件读取 + 预设配置
handwrite-cli -i input.txt -p presets/语文作业.conf

# 导出 PDF / SVG（SVG 多页为 output.svg、output-2.svg …）
handwrite-cli -t "内容" -f pdf -o ./output
handwrite-cli -t "内容" -f svg -o ./output

# 固定随机种子，结果可复现
handwrite-cli -i essay.txt -o ./out -s 20260912

# 批量处理（每行一个输入文件）
handwrite-cli -b list.txt -o ./out

# 管道输入
echo "文本" | handwrite-cli -o ./out
```

选项:
| 参数 | 说明 |
|------|------|
| `-t, --text` | 直接指定文本 |
| `-i, --input` | 输入文件路径 |
| `-o, --output` | 输出目录 (默认: outputs) |
| `-p, --preset` | 预设配置文件 (.conf) |
| `-r, --rate` | 分辨率倍率 (1/2/4/8/16/32/64) |
| `-f, --format` | 输出格式 (png/pdf/svg) |
| `-s, --seed` | 固定随机种子（同种子结果可复现）；批量模式下按序号递增 |
| `-b, --batch` | 批量处理：每行一个输入文件路径的列表文件 |
| `-h, --help` | 显示帮助 |

> 预设文件是 `key = value` 格式（**.conf**），不是 TOML；旧的 `.toml` 预设仍可读、仍会列出。
> `-p` 会读取预设里的全部字段（纸张纹理、混合字体、排版方向、变形、标点策略、
> 背景锚点校准、字符级覆盖、种子等），与 GUI 保存的内容一致。

## 变更记录

Release 说明由 [`CHANGELOG.md`](CHANGELOG.md) 自动生成：打 tag 推送后，Actions 会截取其中
对应版本的段落作为 GitHub Release 正文。**改代码时请顺手更新该文件。**

### v2.8.0
详见 [`CHANGELOG.md`](CHANGELOG.md) 的 2.8.0 小节；设计文档见
[`docs/PLAN_LINE_GUIDES_2026-09-13.md`](docs/PLAN_LINE_GUIDES_2026-09-13.md)。要点：

- **新增「横线导引」**：沿作业本照片的印刷横线手绘曲线，文字逐行排布在横线内并跟随纸张弯曲。
  只需描 2 条关键曲线 + 填条数，中间按弧长参数插值。导引启用时跳过 `warpMesh`
  （曲线已含透视与弯曲的全部形变）
- 新增 `ImageCanvasDialog` 基类，`CalibrationDialog` 与 `LineGuideDialog` 共用画布与坐标换算
- **修复**：`Config` 的 double 数组若值全是整数会被存成 int 数组，按 double 读回时整组丢失
  （锚点、导引曲线的整数像素坐标会中招）
- **修复**：横线导引与纹理网格吸附同时启用会得到无意义位置；有背景照片时不再画程序纹理

### v2.7.0
详见 [`CHANGELOG.md`](CHANGELOG.md) 的 2.7.0 小节，以及完整静态审查报告
[`docs/CODE_REVIEW_2026-09-12.md`](docs/CODE_REVIEW_2026-09-12.md)。要点：

- **修复**：GUI 导出不再清空 `outputs/`（数据安全）；x32/x64 高倍率不再必然 OOM，
  改为给出可读的内存预算提示；工作线程不再访问 UI；背景图只解码一次并跨页共享
- **修复**：预设里所有小数被 `std::stoi` 静默截断成整数（`0.35` → `0`）；
  预设保存/加载丢失 8 类参数；Markdown 删除线不闭合；字符覆盖索引错位
- **新增**：SVG 多页导出、随机种子复现、混合字体真正可用、中文标点保留开关、
  逐字变形、中文避头尾、长任务可取消、CLI `-f svg` / `-s/--seed`
- **工程**：`handwrite-tests` 单测（88 项断言）+ `ctest`、`.clang-format`、`-Wall -Wextra` 零告警、
  发布包只拷真正依赖的 DLL（30 个，替代原来整包 256MB）

### v2.6.1
- **锚点校准优化**：修复四角↔精细模式切换时角点复位 bug（行列按钮改用插值、角点提取用网格坐标）
- **网页更新**：新增输出效果对比与锚点模式展示图片

### v2.6.0
- **锚点校准重构**：默认进入「四角模式」，只拖 4 个角快速定位，内部网格自动双线性插值；可切换到「精细模式」逐点微调
- **Bug 修复**：修复 warpMesh 在锚点校准下的文字裁切回归 bug
- **Web**：新增产品展示页（`Web/index.html`），版本号从 GitHub API 实时拉取

### v2.5.1
- **数据安全**：输出目录不再递归清空，改为只删除本程序生成的页码图片（如 `0.png`），避免误删用户目录下的其他文件
- **中文标点**：修复 `。！？` 等中文标点无法整字换行的问题
- **字体缓存**：混合字体改为带 `QMutex` 缓存的注册，修复每字符重复注册导致的内存泄漏与多线程竞态
- **竖排导出**：旋转后由左上角裁切改为居中裁切，修复宽大于高时丢内容
- **CLI 批量**：去除行尾 `\r`，坏文件仅跳过而不中断整批
- **性能**：墨水洇染由 O(W·H·r²) 优化为 O(W·H·r)，高倍率不再卡死；网格形变每格只重绘对应子矩形
- **工程**：版本号统一为单一来源（CMake 注入 `HANDWRITE_VERSION`）；CMake 现代度提升（`target_include_directories`、`find_package(WebP)`）；配置文件文档统一为 `.conf`；Circle 形变按行基线偏移避免多行重叠
