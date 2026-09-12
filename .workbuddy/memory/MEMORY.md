# HandWrite-CPP 项目记忆

## 版本号规范
- 格式: `主版本.功能添加.小修复`（当前 **2.7.0**）
- **BUG 修复** → 第三位 +1；**功能添加** → 第二位 +1；第二位满 10 进 1 → 第一位 +1
- **版本已单源化**：只在 `CMakeLists.txt` 的 `project(VERSION x.y.z)` 改一处，通过 `HANDWRITE_VERSION` 宏注入 `main.cpp`/`cli.cpp`；`mainwindow.cpp` 用 `QApplication::applicationVersion()`；`build.yml` 用 `${{ github.ref_name }}`。不再手工同步 6 处。

## 构建（本机环境）
- MSYS2 安装位置: `F:\MSYS2`（用 ucrt64 子环境）
- ⚠️ **源码与构建目录都必须是纯 ASCII 路径**！项目在 `E:\备份\编程\C++\HandWrite-CPP`（含中文）下直接构建会失败：moc 无法在中文路径建子目录、windres 把中文 include 路径当乱码导致 `popen` 失败。
- 可用的工作副本（已建）：源码 `F:/HandWrite-CPP`、构建 `F:/hww_build`（另有 `F:/hww_ci` 用于模拟 CI 的新目录）
- ⚠️ **robocopy 会保留 mtime**，同步后 Ninja 可能不重编 → 对被改的源文件 `touch` 一下
- 安装依赖（pacman，国内镜像已置顶）:
  `mingw-w64-ucrt-x86_64-gcc cmake ninja qt6-base qt6-svg fontconfig libwebp`
- 配置: `cmake -S F:/HandWrite-CPP -B F:/hww_build -G Ninja -DCMAKE_BUILD_TYPE=Release`
- 编译: `cmake --build F:/hww_build -j$(nproc)`（需 `export PATH=/f/MSYS2/ucrt64/bin:$PATH`）
- **跑测试**: `cd F:/hww_build && ctest --output-on-failure`（或直接 `./handwrite-tests`）
- 部署: `cd F:/hww_build && windeployqt HandWrite.exe --no-translations --no-system-d3d-compiler`
  再 `python F:/HandWrite-CPP/.github/scripts/collect_dlls.py . "F:/MSYS2/ucrt64/bin"`（**只拷真正依赖的 30 个 DLL**，
  取代旧的 `cp /ucrt64/bin/*.dll .` 整包 256MB），最后写 `qt.conf`
- 最终可运行包已复制到项目 `E:\备份\编程\C++\HandWrite-CPP\build\`（HandWrite.exe + handwrite-cli.exe + Qt DLL + 字体库）
- CLI 端到端测试: `./handwrite-cli.exe -t "中文。" -o "F:/out" -f png -r 2`
- ⚠️ 用 `/f/xxx` 会被 Windows 版 Python 解析成 `F:\f\xxx`；脚本路径一律写 `F:/xxx`；
  MSYS2 路径传给 Windows Python 前要 `cygpath -w`（如 `cygpath -w /ucrt64/bin`）

## 发布流程（2026-09-12 起）
1. 改代码时**同步更新 `CHANGELOG.md`** 的 `## [x.y.z] - YYYY-MM-DD` 小节（新增/修复/变更）
2. `CMakeLists.txt` 的 `project(VERSION x.y.z)` 改一位（唯一版本源）
3. commit + push，然后 `git tag vX.Y.Z && git push origin vX.Y.Z`
4. Actions 用 `.github/scripts/release_notes.py` 把 CHANGELOG 该版本段落 + 提交列表 + 下载说明
   拼成 `release_notes.md`，作为 Release 正文（`body_path`）
5. CHANGELOG 缺条目时脚本会打 `::warning::` 并退化成仅提交列表 —— 不会静默发空 Release

## 单元测试
- 目标 `handwrite-tests`（`tests/test_core.cpp`，88 项断言），默认随主工程构建；关掉用 `-DHANDWRITE_BUILD_TESTS=OFF`
- 覆盖：中文标点转换、Markdown 解析、字符覆盖序列化、文本排版/避头尾、Config 往返、内存预算、字体检查
- 测试二进制自动选平台插件：当前目录无 `platforms/`（未跑 windeployqt）才用 `offscreen`

## 产品网页
- 托管于 **XEKernel.github.io** → `projects/handwrite/`
- 源码修改 → push HandWrite-CPP `Web/**` → GitHub Actions 自动同步到 XEKernel.github.io
- Deploy key: SSH ed25519 (`SITE_DEPLOY_KEY` secret)，只对 XEKernel.github.io 有写权限
- 网页版本号从 GitHub API 实时拉取最新 Release tag，无须手动维护

## 技术栈
- C++17, Qt 6.11, CMake, Ninja, GCC 16
- GitHub Actions: MSYS2 ucrt64 环境自动构建
- Release: 推 tag 触发，zip 包 + winget DLL 自动打包

## 审查问题已全部修复（2026-09-12，v2.7.0）
- 完整报告: `docs/CODE_REVIEW_2026-09-12.md`（文首有修复状态）；当日详情见 `2026-09-12.md`
- P0(4) / P1(9) / P2(13) 全部修复；另由单测与 `-Wall` 新揪出 2 个原报告未列的 BUG
  （`std::stoi("0.35")` 静默截断预设小数；注释行末反斜杠吞掉 `escapeString` 定义）
- 新增功能：SVG 多页导出、随机种子复现、混合字体真正可用、中文标点保留开关、
  逐字变形、避头尾、长任务可取消、CLI `-f svg`/`-s/--seed`、单测 + `.clang-format` + `-Wall -Wextra`
- 三个目标在 GCC 16 / Qt 6.11 下**零 warning**；`ctest` 88/88 通过；发布包 DLL 由 256MB 降到 30 个

## C++ 陷阱（本项目踩过，务必记住）
- `std::stoi("0.35")` 返回 0 且**不抛异常** → 判断类型必须检查 `pos == str.size()`
- 注释行末不要出现反斜杠（`// ... \`）→ 会把下一行代码吞进注释，只有 `-Wcomment` 能报出来
- 静态成员函数里不能碰 `m_xxx` 成员变量（本次 `renderAndSaveParallel` 踩过）
- Qt 6.11：`QMenu::addAction(text, obj, slot, shortcut)` 已废弃，应写 `addAction(text, shortcut, obj, slot)`
