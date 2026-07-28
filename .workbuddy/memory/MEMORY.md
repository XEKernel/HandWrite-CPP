# HandWrite-CPP 项目记忆

## 版本号规范
- 格式: `主版本.功能添加.小修复`（当前 2.5.1）
- **BUG 修复** → 第三位 +1；**功能添加** → 第二位 +1；第二位满 10 进 1 → 第一位 +1
- **版本已单源化**：只在 `CMakeLists.txt` 的 `project(VERSION x.y.z)` 改一处，通过 `HANDWRITE_VERSION` 宏注入 `main.cpp`/`cli.cpp`；`mainwindow.cpp` 用 `QApplication::applicationVersion()`；`build.yml` 用 `${{ github.ref_name }}`。不再手工同步 6 处。

## 构建（本机环境）
- MSYS2 安装位置: `F:\MSYS2`（用 ucrt64 子环境）
- ⚠️ **源码与构建目录都必须是纯 ASCII 路径**！项目在 `E:\备份\编程\C++\HandWrite-CPP`（含中文）下直接构建会失败：moc 无法在中文路径建子目录、windres 把中文 include 路径当乱码导致 `popen` 失败。
- 可用的工作副本（已建）：源码 `F:/HandWrite-CPP`、构建 `F:/hww_build`
- 安装依赖（pacman，国内镜像已置顶）:
  `mingw-w64-ucrt-x86_64-gcc cmake ninja qt6-base qt6-svg fontconfig libwebp`
- 配置: `cmake -S F:/HandWrite-CPP -B F:/hww_build -G Ninja -DCMAKE_BUILD_TYPE=Release`
- 编译: `cmake --build F:/hww_build --target all -j$(nproc)`（需 `export PATH=/ucrt64/bin:$PATH`）
- 部署: `cd F:/hww_build && windeployqt HandWrite.exe --no-translations --no-system-d3d-compiler` 再 `cp /ucrt64/bin/*.dll .` 与 `ttf_library`，写 `qt.conf`
- 最终可运行包已复制到项目 `E:\备份\编程\C++\HandWrite-CPP\build\`（HandWrite.exe + handwrite-cli.exe + Qt DLL + 字体库）
- CLI 端到端测试: `./handwrite-cli.exe -t "中文。" -o "F:/out" -f png -r 2`（注意用 Windows 风格路径如 `F:/out`，勿用 `/f/out` 否则会被 Windows 解析成 `F:/f/out`）

## 技术栈
- C++17, Qt 6.11, CMake, Ninja, GCC 16
- GitHub Actions: MSYS2 ucrt64 环境自动构建
- Release: 推 tag 触发，zip 包 + winget DLL 自动打包
