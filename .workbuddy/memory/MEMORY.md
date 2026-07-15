# HandWrite-CPP 项目记忆

## 版本号规范
- 格式: `主版本.功能添加.小修复` (SemVer)
- 例如 v2.2.0 = 主版本2 + 2个功能添加 + 0个小修复
- **每次代码修改都要递增版本号**，涉及文件:
  - `CMakeLists.txt` (VERSION)
  - `src/main.cpp` (setApplicationVersion)
  - `src/cli.cpp` (setApplicationVersion)
  - `src/mainwindow.cpp` (About 对话框)
  - `.github/workflows/build.yml` (zip 包名、Release 名、tag)

## 构建
- 源文件: `G:\备份\编程\C++\HandWrite-CPP\src\`
- 构建目录: `/g/build/build_gcc`
- 构建命令: `cd /g/build/build_gcc && PATH="/f/MSYS2/ucrt64/bin:$PATH" ninja`
- 部署到项目: `cp /g/build/build_gcc/HandWrite.exe "G:/备份/编程/C++/HandWrite-CPP/build/"`

## 技术栈
- C++17, Qt 6.11, CMake, Ninja, GCC 16
- GitHub Actions: MSYS2 ucrt64 环境自动构建
- Release: 推 tag 触发，zip 包 + winget DLL 自动打包
