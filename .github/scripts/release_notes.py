#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 GitHub Release 说明。

内容按以下顺序拼装：
  1. CHANGELOG.md 中 ``## [<版本号>]`` 到下一个 ``## [`` 之间的段落（人工维护）
  2. 上一个 tag 到本次 tag 之间的提交标题列表（自动提取）
  3. 下载与使用说明

用法：
    python .github/scripts/release_notes.py v2.7.0
    python .github/scripts/release_notes.py v2.7.0 -o release_notes.md
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

MISSING_HINT = (
    "> **CHANGELOG.md 中没有找到 `{ver}` 的条目。**\n"
    "> 发版前请先在 `CHANGELOG.md` 补一个 `## [{ver}] - YYYY-MM-DD` 小节，\n"
    "> 否则用户只能看到下面的提交列表。"
)


def run_git(args):
    """跑一条 git 命令，失败返回 None（不抛异常）。"""
    try:
        out = subprocess.run(
            ["git"] + args,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        if out.returncode != 0:
            return None
        return out.stdout.strip()
    except Exception:
        return None


def extract_changelog_section(path, version):
    """取 CHANGELOG.md 中该版本的段落；找不到返回 None。"""
    if not path.is_file():
        return None

    lines = path.read_text(encoding="utf-8").splitlines()
    head = re.compile(r"^##\s*\[" + re.escape(version) + r"\]")
    start = None
    for i, line in enumerate(lines):
        if head.match(line):
            start = i + 1
            break
    if start is None:
        return None

    buf = []
    for line in lines[start:]:
        if re.match(r"^##\s*\[", line):        # 下一个版本
            break
        if re.match(r"^---\s*$", line):        # 底部链接区
            break
        buf.append(line)

    text = "\n".join(buf).strip()
    return text or None


def collect_commits(tag, prev):
    """返回提交标题列表（不含空行）。"""
    rng = "%s..%s" % (prev, tag) if prev else tag
    out = run_git(["log", "--pretty=format:- %s", rng])
    if out is None:
        return []
    return [ln for ln in out.splitlines() if len(ln.strip()) > 2]


def build(tag, changelog, repo_root):
    version = tag[1:] if tag.startswith("v") else tag

    section = extract_changelog_section(changelog, version)
    if section is None:
        print("::warning::CHANGELOG.md 缺少 %s 的条目，Release 说明已退化为仅提交列表"
              % version)

    prev = run_git(["describe", "--tags", "--abbrev=0", tag + "^"])
    if prev:
        prev = prev.strip() or None
    commits = collect_commits(tag, prev)

    parts = ["## HandWrite %s" % tag, ""]
    parts.append(section if section else MISSING_HINT.format(ver=version))
    parts += ["", "---", ""]

    if prev:
        parts.append("### 本次提交（%s → %s）" % (prev, tag))
    else:
        parts.append("### 本次提交")
    parts.append("")

    if commits:
        parts += commits
    else:
        parts.append("- （无提交记录）")

    parts += [
        "",
        "### 下载与使用",
        "",
        "Windows x64 免安装包：`HandWrite-%s-windows-x64.zip`" % tag,
        "",
        "- 图形界面：解压后运行 `HandWrite.exe`",
        "- 命令行：`handwrite-cli.exe -i 输入.txt -o 输出目录 -r 4 -f png`",
        "- 命令行帮助：`handwrite-cli.exe --help`",
        "- 字体放在解压目录的 `ttf_library` 下，新增字体后重启程序即可在选择列表里看到",
    ]

    return "\n".join(parts) + "\n", prev, len(commits), bool(section)


def main():
    ap = argparse.ArgumentParser(description="生成 GitHub Release 说明")
    ap.add_argument("tag", help="本次发布的 tag，例如 v2.7.0")
    ap.add_argument("-c", "--changelog", default="CHANGELOG.md", help="CHANGELOG 路径")
    ap.add_argument("-o", "--output", default="release_notes.md", help="输出文件")
    args = ap.parse_args()

    repo_root = Path(__file__).resolve().parent.parent.parent
    changelog = Path(args.changelog)
    if not changelog.is_absolute():
        changelog = repo_root / changelog

    body, prev, n_commits, has_section = build(args.tag, changelog, repo_root)

    out = Path(args.output)
    if not out.is_absolute():
        out = Path.cwd() / out
    out.write_text(body, encoding="utf-8")

    print("Release notes written to %s" % out)
    print("Previous tag : %s" % (prev or "(none)"))
    print("Commit lines : %d" % n_commits)
    print("Changelog    : %s" % ("found" if has_section else "MISSING"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
