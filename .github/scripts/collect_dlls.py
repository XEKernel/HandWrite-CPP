#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""按 PE 导入表递归收集可执行文件真正依赖的 DLL，拷进发布目录。

替代 `cp /ucrt64/bin/*.dll .` —— 后者会把整个 MSYS2 运行时（约 256 MB）
全拷进发布包，而其中绝大多数 DLL 与主程序毫无关系。

原理：
  1. 用 objdump -p 解析每个 PE 文件的导入表，拿到 "DLL Name: xxx"
  2. 只在给定的搜索目录（默认 /ucrt64/bin）里找这些 DLL。
     系统 DLL（KERNEL32、api-ms-win-crt-* …）根本不在 MSYS2 目录里，自然被跳过
  3. 新拷进来的 DLL 继续递归，直到闭包收敛

任何一步失败都会以非 0 退出，CI 侧据此回退到「整包拷贝」的旧行为，保证能发布。

用法：
    python .github/scripts/collect_dlls.py <发布目录> [搜索目录 ...]
"""

import re
import shutil
import subprocess
import sys
from pathlib import Path

# 少于这个数量说明解析出了问题，宁可让 CI 回退到旧做法
MIN_EXPECTED = 4

DLL_NAME = re.compile(r"^\s*DLL Name:\s*(\S+)", re.IGNORECASE)


def pe_imports(path):
    """返回该 PE 文件导入的 DLL 名列表。"""
    try:
        out = subprocess.run(
            ["objdump", "-p", str(path)],
            capture_output=True, text=True, encoding="utf-8", errors="replace",
        )
    except Exception:
        return []
    if out.returncode != 0:
        return []
    names = []
    for line in out.stdout.splitlines():
        m = DLL_NAME.match(line)
        if m:
            names.append(m.group(1))
    return names


def main():
    if len(sys.argv) < 2:
        print("usage: collect_dlls.py <发布目录> [搜索目录 ...]", file=sys.stderr)
        return 2

    dest = Path(sys.argv[1]).resolve()
    search = [Path(p) for p in sys.argv[2:]] or [Path("/ucrt64/bin")]
    search = [p for p in search if p.is_dir()]
    if not dest.is_dir() or not search:
        print("ERROR: 发布目录或搜索目录不存在", file=sys.stderr)
        return 2

    # 索引搜索目录里的 DLL（小写名 -> 路径）
    index = {}
    for d in search:
        for f in d.glob("*.dll"):
            index.setdefault(f.name.lower(), f)

    # 起点：发布目录里已有的所有 PE 文件（exe + windeployqt 拷来的 dll + 插件）
    pending = [p for p in dest.rglob("*.dll")] + [p for p in dest.glob("*.exe")]
    if not pending:
        print("ERROR: 发布目录里没有可执行文件", file=sys.stderr)
        return 2

    seen = set()
    copied = []
    missing = set()
    guard = 0

    while pending and guard < 5000:
        guard += 1
        f = pending.pop()
        for dll in pe_imports(f):
            key = dll.lower()
            if key in seen:
                continue
            seen.add(key)

            src = index.get(key)
            if src is None:
                # 不在 MSYS2 里 = 系统 DLL 或已随应用分发，无需拷贝
                continue

            dst = dest / dll
            if not dst.exists():
                try:
                    shutil.copy2(src, dst)
                except Exception as e:
                    print("ERROR: 拷贝 %s 失败: %s" % (dll, e), file=sys.stderr)
                    return 2
                copied.append(dll)
            # 新来的 DLL 可能还有自己的依赖，继续递归
            pending.append(dst)

    if len(copied) < MIN_EXPECTED:
        print("ERROR: 只解析出 %d 个 DLL，疑似导入表解析异常，请回退到整包拷贝"
              % len(copied), file=sys.stderr)
        return 2

    print("Copied %d DLLs into %s" % (len(copied), dest))
    for name in sorted(copied):
        print("  + %s" % name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
