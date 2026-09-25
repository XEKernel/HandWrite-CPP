#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成「作业本照片」样例图，用于验证横线导引（Line Guides）。

两种形态：
    skew —— 纸被斜拍成梯形（上窄下宽），横线随之变短；用于验证水平透视补偿
    flat —— 正拍，纸为整幅矩形；对照组，水平缩放应恒为 1.0

用法：
    python tools/make_sample_notebook.py out.png [skew|flat]

它会顺便打印该图对应的 3×3 锚点（bg_calib_points）与首尾两条 guide 关键曲线
（line_guide_curves），可直接粘进预设文件：

    background_image = "out.png"
    bg_calib_enabled = 1
    bg_calib_rows = 3
    bg_calib_cols = 3
    bg_calib_points = [...]
    line_guide_enabled = 1
    line_guide_line_count = 22
    line_guide_curves = [...]

纯标准库（zlib + struct），不依赖 Pillow。
"""

import struct
import sys
import zlib

W, H = 900, 1200
LINE_COUNT = 22

# 纸四角（skew=梯形俯拍 / flat=正拍矩形）
CORNERS = {
    "skew": {"TL": (250.0, 100.0), "TR": (650.0, 100.0),
             "BL": (80.0, 1140.0),  "BR": (820.0, 1140.0)},
    "flat": {"TL": (20.0, 60.0),   "TR": (880.0, 60.0),
             "BL": (20.0, 1160.0), "BR": (880.0, 1160.0)},
}


def lerp(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)


def build(mode):
    tl = CORNERS[mode]["TL"]
    tr = CORNERS[mode]["TR"]
    bl = CORNERS[mode]["BL"]
    br = CORNERS[mode]["BR"]

    rows = [bytearray(bytes((38, 38, 42, 255)) * W) for _ in range(H)]   # 桌面背景

    # 纸面：逐行按左右斜边裁剪填充
    for y in range(H):
        t = y / (H - 1.0)
        left = lerp(tl, bl, t)
        right = lerp(tr, br, t)
        for x in range(max(0, int(left[0])), min(W - 1, int(right[0])) + 1):
            rows[y][x * 4:x * 4 + 4] = bytes((250, 250, 246, 255))

    # 横线：第 i 条的两端点在左右斜边上按 t 插值
    for i in range(LINE_COUNT):
        t = i / (LINE_COUNT - 1.0)
        p0 = lerp(tl, bl, t)
        p1 = lerp(tr, br, t)
        steps = int(abs(p1[0] - p0[0])) + 1
        for s in range(steps):
            u = s / (steps - 1.0)
            x = p0[0] + (p1[0] - p0[0]) * u
            yc = p0[1] + (p1[1] - p0[1]) * u
            for dy in (-1, 0, 1):
                y = int(round(yc)) + dy
                if not (0 <= y < H):
                    continue
                xi = int(round(x))
                if not (0 <= xi < W):
                    continue
                o = xi * 4
                a = 1.0 if dy == 0 else 0.45
                rows[y][o]     = int(rows[y][o]     * (1 - a) + 150 * a)
                rows[y][o + 1] = int(rows[y][o + 1] * (1 - a) + 165 * a)
                rows[y][o + 2] = int(rows[y][o + 2] * (1 - a) + 195 * a)

    raw = bytearray()
    for r in rows:
        raw.append(0)
        raw += r
    return bytes(raw)


def write_png(path, raw):
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    out = b"\x89PNG\r\n\x1a\n"
    out += chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0))
    out += chunk(b"IDAT", zlib.compress(raw, 9))
    out += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(out)


def print_preset_hints(mode):
    tl = CORNERS[mode]["TL"]
    tr = CORNERS[mode]["TR"]
    bl = CORNERS[mode]["BL"]
    br = CORNERS[mode]["BR"]

    # 3×3 锚点：双线性插值
    pts = []
    for v in (0.0, 0.5, 1.0):
        for u in (0.0, 0.5, 1.0):
            x = (1 - v) * ((1 - u) * tl[0] + u * tr[0]) + v * ((1 - u) * bl[0] + u * br[0])
            y = (1 - v) * ((1 - u) * tl[1] + u * tr[1]) + v * ((1 - u) * bl[1] + u * br[1])
            pts.append("%g,%g" % (round(x, 1), round(y, 1)))

    # 首尾两条关键曲线（各 3 点）
    g0 = (tl[0], tl[1], (tl[0] + tr[0]) / 2, tl[1], tr[0], tr[1])
    g1 = (bl[0], bl[1], (bl[0] + br[0]) / 2, bl[1], br[0], br[1])

    print("\n预设片段（配合该图使用）：")
    print("  bg_calib_points    = [%s]" % ",".join(pts))
    print("  line_guide_curves  = [3,%g,%g,%g,%g,%g,%g, 3,%g,%g,%g,%g,%g,%g]" % (g0 + g1))
    print("  line_guide_line_count = %d" % LINE_COUNT)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    path = sys.argv[1]
    mode = sys.argv[2] if len(sys.argv) > 2 else "skew"
    if mode not in CORNERS:
        print("未知形态：%s（可选 skew / flat）" % mode)
        return 2

    write_png(path, build(mode))
    print("wrote %s (%dx%d, %s)" % (path, W, H, mode))
    print_preset_hints(mode)
    return 0


if __name__ == "__main__":
    sys.exit(main())
