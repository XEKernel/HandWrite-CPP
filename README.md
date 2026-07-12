# HandWrite Generator

手写作业生成器 — 使用 C++/Qt6 编写，通过自然扰动渲染文本以模拟手写效果。

![Platform](https://img.shields.io/badge/platform-Windows-blue)
![Qt](https://img.shields.io/badge/Qt-6.11-green)
![C++](https://img.shields.io/badge/C++-17-blue)

## 特性

### 纸张纹理
支持 6 种纸张模板：横线纸、方格纸、田字格、作文纸、点阵纸、纯色背景，可调透明度。

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
- PDF 导出
- 多预设管理（保存/加载/删除）
- 字符级别属性覆盖
- 背景图片叠加
- 缩放、翻页、打印

## 构建

### 依赖
- MSYS2 (ucrt64)
- GCC 16+
- CMake 4+
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

### CLI
```bash
# 基本用法
handwrite-cli -t "文本内容" -o ./output -r 2

# 从文件读取 + 预设配置
handwrite-cli -i input.txt -p presets/语文作业.toml

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
| `-p, --preset` | 预设配置文件 (.toml) |
| `-r, --rate` | 分辨率倍率 (1/2/4/8/16/32/64) |
| `-f, --format` | 输出格式 (png/pdf) |

## 字体

将 `.ttf` 手写字体文件放入 `ttf_library/` 目录，程序启动时自动加载。
