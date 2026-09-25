# HandWrite-CPP 项目记忆

## 版本号规范
- 格式: `主版本.功能添加.小修复`（当前 **2.9.0**）
- **BUG 修复** → 第三位 +1；**功能添加** → 第二位 +1；第二位满 10 进 1 → 第一位 +1
- ⭐ 用户 2026-09-25 补充：**开发过程中每完成一次修改就第三位 +1**
  （横线导引即 2.8.0 → 2.8.1 引擎 → 2.8.2 手绘 UI → 2.9.0 水平透视补偿，功能整体完成时进第二位）
- **版本已单源化**：只在 `CMakeLists.txt` 的 `project(VERSION x.y.z)` 改一处，通过 `HANDWRITE_VERSION` 宏注入 `main.cpp`/`cli.cpp`；`mainwindow.cpp` 用 `QApplication::applicationVersion()`；`build.yml` 用 `${{ github.ref_name }}`。不再手工同步 6 处。
- 发版时要**同步更新 `CHANGELOG.md` 对应版本小节**，CI 用它拼 Release 正文

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

## 规划中（未开工）
- **作业本横线导引 Line Guides**（2026-09-13 提出，目标 v2.8.0）：
  `docs/PLAN_LINE_GUIDES_2026-09-13.md`（v2 版）。让背景作业本照片上的印刷横线可被手绘定义，
  文字按横线排布。流程固定为「先锚点定透视 → 再画横线」。
- ⭐ **路线决策：guide 启用时 bypass `warpMesh`，在画布 B 上逐字沿曲线直接绘制，不做逆映射**。
  理由：guide 曲线是用户在照片（B 空间）上描的，已含透视+弯曲全部形变；
  B→C→B 绕一圈理想恒等、实际因 quadToQuad 分片近似与 3×3 网格过粗而引入误差。
- ⭐ `drawTextWithPerturbationStatic`（core.cpp:755-897）**本来就是逐字循环**
  （每字符 save/translate/rotate/drawText/restore），所以"逐字沿曲线"改造量很小。
- 锚点与 guide 分工正交：锚点管页面四角几何（低频透视），guide 管每行曲线（高频弯曲）。
- 交互关键：**手绘 2 条关键曲线 + 填条数 → 弧长参数插值**出中间线，成本从 20 条降到 2~3 条。
- **批次 1-4 已全部完成（2026-09-25，v2.9.0 未发版）**：引擎 + 手绘 UI + 水平透视补偿 + 自动检测。
  零告警，ctest 170/170，offscreen 截图确认 UI 无回归，端到端（正拍/斜拍/翻页/检测→渲染）全过。
  **待办：打 tag v2.9.0 发版（CHANGELOG 已写好），2026-09-26 进行。**
- ⭐ **检测算法三个坑**（detectHorizontalLines）：① 1px 细线经等权平均 `[1,1,1]/3` 峰值掉到 0.33
  → 用 `[1,2,1]/4`；② 偶数条数时固定 0.5 权重评估中间曲线差半线距 → 按 `midIdx/(n-1)`；
  ③ 首列块全背景时窗口内 ratio 全 0，取窗口边界会让曲线偏移一格线距 → 保持当前 y。
  核心思路是三分类亮度（纸亮/横线中/背景暗）而非"找最暗行"——深色桌面比横线更暗。
- ⭐ **水平透视补偿的坑**：平移与缩放必须**一起做**（`translate(origin - scale*contentLeft)` 再 `scale`）。
  只做 scale、原点固定在 contentLeft，会导致行首仍停在页面标称位置、文字画到纸外面。
- ⭐ **「先赋值再自校验」顺序陷阱**：`cal.enabled = cal.isValid()` 永远得 false，
  因为 `isValid()` 自身要求 `enabled` 为真。CLI 与 GUI 两条路径要对称测试。
- ⭐ **GUI 自动化验证套路**：临时 CMake 工程链接 `mainwindow.cpp`，`QT_QPA_PLATFORM=offscreen`
  + `QTimer::singleShot(400, [&]{ w->grab().save(png); })` 渲染成 PNG 人工检查。
  AUTOUIC 记得把 `ui/mainwindow.ui` 加进 target。
- ⭐ **LineGuideDialog 正确性关键**：关键曲线必须**按垂直位置排序**再插值 ——
  插值在空间相邻的两条之间做，用户描线先后顺序不能决定分段。
- ⭐ **robocopy 陷阱**：Git Bash 里必须 `MSYS_NO_PATHCONV=1 robocopy ...`，
  否则 `/MIR` `/XD` 被 MSYS 路径转换破坏，robocopy 静默失败 → 构建的是旧代码还以为同步成功。
  同步后必须 grep 目标文件确认含新符号。
- ⭐ **Config double 数组陷阱**：值全为整数时被存成 intArray，`getDoubleArray` 会返回空。
  已修复（自动兼容两种存储），新增坐标类字段时记得加「纯整数」回归用例。

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
