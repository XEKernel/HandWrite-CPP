# HandWrite-CPP 项目记忆

## 版本号规范
- 格式: `主版本.功能添加.小修复`
- **BUG 修复** → 第三位 +1（如 2.4.2 → 2.4.3）
- **功能添加** → 第二位 +1（如 2.4.0 → 2.5.0）
- **第二位满 10 进 1** → 第一位 +1，第二位归 0（如 2.9.x → 3.0.x）
- **每次代码修改都要递增版本号**，以下文件必须同步更新:
  - `CMakeLists.txt` (project VERSION)
  - `src/main.cpp` (setApplicationVersion)
  - `src/cli.cpp` (setApplicationVersion)
  - `src/mainwindow.cpp` (About 对话框 vX.Y.Z)
  - `.github/workflows/build.yml` (zip 包名、Release 名、tag，共 5 处)

## 构建
- 源文件: `G:\备份\编程\C++\HandWrite-CPP\src\`
- 构建目录: `/g/build/build_gcc`
- 构建命令: `cd /g/build/build_gcc && PATH="/f/MSYS2/ucrt64/bin:$PATH" ninja`
- 部署到项目: `cp /g/build/build_gcc/HandWrite.exe "G:/备份/编程/C++/HandWrite-CPP/build/"`

## 技术栈
- C++17, Qt 6.11, CMake, Ninja, GCC 16
- GitHub Actions: MSYS2 ucrt64 环境自动构建
- Release: 推 tag 触发，zip 包 + winget DLL 自动打包
