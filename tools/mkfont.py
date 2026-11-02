#!/usr/bin/env python3
"""mkfont.py -- make src/termfont.c: the text starter's 5x10 font, 0x00-0x7F,
with the VT100's line-drawing set (DEC Special Graphics, 0x5F-0x7E) added at
0x80-0x9F so the boxes join up from cell to cell.

    python3 tools/mkfont.py modules/picocalc-text-starter/drivers/font-5x10.c > src/termfont.c

The glyphs are 5 pixels wide (bit 0x10 the left) and 10 high; a line across
is on row 4, a line down is column 2, and both run to the cell's edges.
"""
import re
import sys

src = open(sys.argv[1]).read()
rows = [int(b, 2) for b in re.findall(r"0b([01]{5})", src)]
assert len(rows) >= 128 * 10, len(rows)
base = [rows[i * 10:(i + 1) * 10] for i in range(128)]

H = 4          # the row of a line across
FULL, LEFT, RIGHT, MID = 0b11111, 0b11100, 0b00111, 0b00100


def glyph(rows_):
    g = [0] * 10
    for r, bits in rows_.items():
        g[r] |= bits
    return g


def down(top, bottom):
    return {r: MID for r in range(top, bottom + 1)}


def merge(*parts):
    out = {}
    for p in parts:
        for r, b in p.items():
            out[r] = out.get(r, 0) | b
    return out


def pic(lines, top):
    return {top + i: int(l.replace(".", "0").replace("#", "1"), 2) for i, l in enumerate(lines)}


up_half, down_half, whole = down(0, H), down(H, 9), down(0, 9)
small_box = pic(["###", "###", "###"], 3)
small_box = {r: b << 1 for r, b in small_box.items()}

dec = {
    0x5F: {},
    0x60: pic(["..#..", ".###.", "#####", ".###.", "..#.."], 2),
    0x61: {r: (0b10101 if r % 2 == 0 else 0b01010) for r in range(10)},
    0x62: small_box, 0x63: small_box, 0x64: small_box, 0x65: small_box,
    0x66: pic([".##..", "#..#.", ".##.."], 0),
    0x67: pic(["..#..", "..#..", "#####", "..#..", "..#..", ".....", "#####"], 1),
    0x68: small_box, 0x69: small_box,
    0x6A: merge(up_half, {H: LEFT}),
    0x6B: merge(down_half, {H: LEFT}),
    0x6C: merge(down_half, {H: RIGHT}),
    0x6D: merge(up_half, {H: RIGHT}),
    0x6E: merge(whole, {H: FULL}),
    0x6F: {0: FULL}, 0x70: {2: FULL}, 0x71: {H: FULL}, 0x72: {6: FULL}, 0x73: {8: FULL},
    0x74: merge(whole, {H: RIGHT}),
    0x75: merge(whole, {H: LEFT}),
    0x76: merge(up_half, {H: FULL}),
    0x77: merge(down_half, {H: FULL}),
    0x78: whole,
    0x79: pic(["...#.", "..#..", ".#...", "..#..", "...#.", ".....", ".###."], 1),
    0x7A: pic([".#...", "..#..", "...#.", "..#..", ".#...", ".....", ".###."], 1),
    0x7B: pic(["#####", ".#.#.", ".#.#.", ".#.#.", ".#.#."], 3),
    0x7C: pic(["...#.", "#####", "..#..", "#####", ".#..."], 2),
    0x7D: pic(["..##.", ".#..#", ".#...", "###..", ".#...", ".#...", "#####"], 1),
    0x7E: pic(["..#.."], H),
}

print("/* termfont.c -- made by tools/mkfont.py; don't edit it by hand.")
print(" *")
print(" * Blair Leduc's 5x10 font from picocalc-text-starter (drivers/font-5x10.c,")
print(" * itself after idispatch/raster-fonts' 5x8), 0x00-0x7F, and the VT100's")
print(" * line-drawing set at 0x80-0x9F. */")
print('#include "font.h"')
print()
print("const font_t font_term = {")
print("\t.width = 5,")
print("\t.glyphs = {")
for code in range(128):
    print(f"\t\t/* 0x{code:02X} */ " + ", ".join(f"0x{b:02X}" for b in base[code]) + ",")
for i, code in enumerate(range(0x5F, 0x7F)):
    g = glyph(dec[code])
    print(f"\t\t/* 0x{0x80 + i:02X} (DEC 0x{code:02X}) */ " + ", ".join(f"0x{b:02X}" for b in g) + ",")
print("\t}")
print("};")
