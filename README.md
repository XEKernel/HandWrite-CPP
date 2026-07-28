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

### 笔触效果
- **笔画粗细扰动** — 模拟用力不均
- **墨水洇染** — 笔迹边缘羽化效果
- **涂改模拟** — 随机划线删除

### 排版增强
- 段首缩进两字符
- 段间距独立设置
- Markdown 轻标记：`# 标题`、`~~删除线~~`、`---` 分割线

### UI 功能
- 实时自动预览（参数变化 500ms 后刷新）
- **可拖拽分隔条** — 预览区和设置栏宽度自由调节
- PDF 导出
- 多预设管理（保存/加载/删除）
- 字符级别属性覆盖
- 缩放、翻页、打印

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

# 导出 PDF
handwrite-cli -t "内容" -f pdf -o ./output

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
| `-f, --format` | 输出格式 (png/pdf) |

## 字体

将 `.ttf` 手写字体文件放入 `ttf_library/` 目录，程序启动时自动加载。

## 变更记录

### v2.5.1
- **数据安全**：输出目录不再递归清空，改为只删除本程序生成的页码图片（如 `0.png`），避免误删用户目录下的其他文件
- **中文标点**：修复 `。！？` 等中文标点无法整字换行的问题
- **字体缓存**：混合字体改为带 `QMutex` 缓存的注册，修复每字符重复注册导致的内存泄漏与多线程竞态
- **竖排导出**：旋转后由左上角裁切改为居中裁切，修复宽大于高时丢内容
- **CLI 批量**：去除行尾 `\r`，坏文件仅跳过而不中断整批
- **性能**：墨水洇染由 O(W·H·r²) 优化为 O(W·H·r)，高倍率不再卡死；网格形变每格只重绘对应子矩形
- **工程**：版本号统一为单一来源（CMake 注入 `HANDWRITE_VERSION`）；CMake 现代度提升（`target_include_directories`、`find_package(WebP)`）；配置文件文档统一为 `.conf`；Circle 形变按行基线偏移避免多行重叠
