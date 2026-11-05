#!/usr/bin/env python3
"""font4.py -- calcterm's 4-pixel-wide font, for 80 columns on the 320-pixel screen.

Each glyph is drawn here, 3 pixels wide with the 4th column left as the gap
between letters, in 10 rows (80x32 on the screen).  A 12-row version (80x26)
is made from the same drawings by adding a row of space on top and repeating
one row of each letter's body.  calcterm uses the 10-row font with plain
on/off pixels (tested on the PicoCalc: the grey made no difference), so
a level of two thirds or more is drawn as on.  The levels are:

    '#' full   '+' two thirds   '-' one third   '.' off

Rows, for the 10-row font: 0 is space above; capitals and digits are rows
1-7; lower case is rows 3-7 with ascenders from row 1; row 7 is the
baseline; descenders are rows 8-9.

    python3 tools/font4.py --preview out      draws sample screens (out-10.png, out-12.png)

This file is calcterm's own (MIT), not taken from any other font.
"""
import sys

H = 10
LEVEL = {'.': 0, '-': 1, '+': 2, '#': 3}
G = {}


def put(ch, top, *rows):
    g = [[0] * 4 for _ in range(H)]
    for i, r in enumerate(rows):
        assert len(r) == 3, (ch, r)
        for x, c in enumerate(r):
            g[top + i][x] = LEVEL[c]
    G[ord(ch) if isinstance(ch, str) else ch] = g


def put4(ch, top, *rows):
    """a letter that needs all 4 columns (no gap after it)"""
    g = [[0] * 4 for _ in range(H)]
    for i, r in enumerate(rows):
        assert len(r) == 4, (ch, r)
        for x, c in enumerate(r):
            g[top + i][x] = LEVEL[c]
    G[ord(ch)] = g


# capitals, rows 1-7
put('A', 1, ".#.", "#.#", "#.#", "###", "#.#", "#.#", "#.#")
put('B', 1, "##.", "#.#", "#.#", "##.", "#.#", "#.#", "##.")
put('C', 1, ".##", "#..", "#..", "#..", "#..", "#..", ".##")
put('D', 1, "##.", "#.#", "#.#", "#.#", "#.#", "#.#", "##.")
put('E', 1, "###", "#..", "#..", "##.", "#..", "#..", "###")
put('F', 1, "###", "#..", "#..", "##.", "#..", "#..", "#..")
put('G', 1, ".##", "#..", "#..", "#.#", "#.#", "#.#", ".##")
put('H', 1, "#.#", "#.#", "#.#", "###", "#.#", "#.#", "#.#")
put('I', 1, "###", ".#.", ".#.", ".#.", ".#.", ".#.", "###")
put('J', 1, "..#", "..#", "..#", "..#", "..#", "#.#", ".#.")
put('K', 1, "#.#", "#.#", "##.", "#..", "##.", "#.#", "#.#")
put('L', 1, "#..", "#..", "#..", "#..", "#..", "#..", "###")
put('M', 1, "#.#", "###", "###", "#.#", "#.#", "#.#", "#.#")
put4('N', 1, "#..#", "##.#", "##.#", "#.##", "#.##", "#..#", "#..#")
put('O', 1, ".#.", "#.#", "#.#", "#.#", "#.#", "#.#", ".#.")
put('P', 1, "##.", "#.#", "#.#", "##.", "#..", "#..", "#..")
put('Q', 1, ".#.", "#.#", "#.#", "#.#", "#.#", "#+#", ".##")
put('R', 1, "##.", "#.#", "#.#", "##.", "#.#", "#.#", "#.#")
put('S', 1, ".##", "#..", "#..", ".#.", "..#", "..#", "##.")
put('T', 1, "###", ".#.", ".#.", ".#.", ".#.", ".#.", ".#.")
put('U', 1, "#.#", "#.#", "#.#", "#.#", "#.#", "#.#", "###")
put('V', 1, "#.#", "#.#", "#.#", "#.#", "#.#", ".#.", ".#.")
put('W', 1, "#.#", "#.#", "#.#", "#.#", "###", "###", "#.#")
put('X', 1, "#.#", "#.#", "+#+", ".#.", "+#+", "#.#", "#.#")
put('Y', 1, "#.#", "#.#", "#.#", ".#.", ".#.", ".#.", ".#.")
put('Z', 1, "###", "..#", "..#", ".#.", "#..", "#..", "###")

# lower case: x-height rows 3-7, ascenders from row 1, descenders to row 9
put('a', 3, ".#.", "..#", ".##", "#.#", ".##")
put('b', 1, "#..", "#..", "##.", "#.#", "#.#", "#.#", "##.")
put('c', 3, ".##", "#..", "#..", "#..", ".##")
put('d', 1, "..#", "..#", ".##", "#.#", "#.#", "#.#", ".##")
put('e', 3, ".#.", "#.#", "###", "#..", ".##")
put('f', 1, "..#", ".#.", "###", ".#.", ".#.", ".#.", ".#.")
put('g', 3, ".##", "#.#", "#.#", "#.#", ".##", "..#", "##.")
put('h', 1, "#..", "#..", "##.", "#.#", "#.#", "#.#", "#.#")
put('i', 1, ".#.", "...", "##.", ".#.", ".#.", ".#.", "###")
put('j', 1, "..#", "...", ".##", "..#", "..#", "..#", "..#", "#.#", ".#.")
put('k', 1, "#..", "#..", "#.#", "#.#", "##.", "#.#", "#.#")
put('l', 1, "##.", ".#.", ".#.", ".#.", ".#.", ".#.", "..#")
put('m', 3, "###", "###", "#.#", "#.#", "#.#")
put('n', 3, "##.", "#.#", "#.#", "#.#", "#.#")
put('o', 3, ".#.", "#.#", "#.#", "#.#", ".#.")
put('p', 3, "##.", "#.#", "#.#", "#.#", "##.", "#..", "#..")
put('q', 3, ".##", "#.#", "#.#", "#.#", ".##", "..#", "..#")
put('r', 3, "#.#", "##.", "#..", "#..", "#..")
put('s', 3, ".##", "#..", ".#.", "..#", "##.")
put('t', 1, ".#.", ".#.", "###", ".#.", ".#.", ".#.", "..#")
put('u', 3, "#.#", "#.#", "#.#", "#.#", ".##")
put('v', 3, "#.#", "#.#", "#.#", "+#+", ".#.")
put('w', 3, "#.#", "#.#", "#.#", "###", "###")
put('x', 3, "#.#", "+#+", ".#.", "+#+", "#.#")
put('y', 3, "#.#", "#.#", "#.#", "#.#", ".##", "..#", "##.")
put('z', 3, "###", "..#", ".#.", "#..", "###")

# digits, rows 1-7 (the zero has a dot, to tell it from O)
put('0', 1, "###", "#.#", "#.#", "#.#", "#.#", "#.#", "###")
put('1', 1, ".#.", "##.", ".#.", ".#.", ".#.", ".#.", "###")
put('2', 1, ".#.", "#.#", "..#", ".#.", "#..", "#..", "###")
put('3', 1, "##.", "..#", "..#", ".#.", "..#", "..#", "##.")
put('4', 1, "#.#", "#.#", "#.#", "###", "..#", "..#", "..#")
put('5', 1, "###", "#..", "##.", "..#", "..#", "..#", "##.")
put('6', 1, ".##", "#..", "#..", "##.", "#.#", "#.#", ".#.")
put('7', 1, "###", "..#", "..#", ".#.", ".#.", ".#.", ".#.")
put('8', 1, ".#.", "#.#", "#.#", ".#.", "#.#", "#.#", ".#.")
put('9', 1, ".#.", "#.#", "#.#", ".##", "..#", "..#", "##.")

# punctuation
put(' ', 0)
put('!', 1, ".#.", ".#.", ".#.", ".#.", ".#.", "...", ".#.")
put('"', 1, "#.#", "#.#")
put('#', 2, "#.#", "###", "#.#", "###", "#.#")
put('$', 1, ".#.", ".##", "#..", ".#.", "..#", "##.", ".#.")
put('%', 1, "#.#", "..#", ".#.", ".#.", ".#.", "#..", "#.#")
put('&', 1, ".#.", "#.#", ".#.", "##.", "#.#", "#.#", ".##")
put("'", 1, ".#.", ".#.")
put('(', 1, "..#", ".#.", ".#.", ".#.", ".#.", ".#.", "..#")
put(')', 1, "#..", ".#.", ".#.", ".#.", ".#.", ".#.", "#..")
put('*', 3, "#.#", ".#.", "#.#")
put('+', 3, ".#.", "###", ".#.")
put(',', 6, ".#.", ".#.", "#..")
put('-', 4, "###")
put('.', 7, ".#.")
put('/', 1, "..#", "..#", ".#.", ".#.", ".#.", "#..", "#..")
put(':', 4, ".#.", "...", "...", ".#.")
put(';', 4, ".#.", "...", "...", ".#.", "#..")
put('<', 2, "..#", ".#.", "#..", ".#.", "..#")
put('=', 3, "###", "...", "###")
put('>', 2, "#..", ".#.", "..#", ".#.", "#..")
put('?', 1, ".#.", "#.#", "..#", ".#.", ".#.", "...", ".#.")
put('@', 1, ".#.", "#.#", "#+#", "#+#", "#++", "#..", ".##")
put('[', 1, "##.", "#..", "#..", "#..", "#..", "#..", "##.")
put('\\', 1, "#..", "#..", ".#.", ".#.", ".#.", "..#", "..#")
put(']', 1, ".##", "..#", "..#", "..#", "..#", "..#", ".##")
put('^', 1, ".#.", "#.#")
put('_', 8, "###")
put('`', 1, "#..", ".#.")
put('{', 1, "..#", ".#.", ".#.", "#..", ".#.", ".#.", "..#")
put('|', 1, ".#.", ".#.", ".#.", ".#.", ".#.", ".#.", ".#.", ".#.")
put('}', 1, "#..", ".#.", ".#.", "..#", ".#.", ".#.", "#..")
put('~', 3, "+#-", "-#+")
put(0x7f, 0)


# the VT100's line-drawing set, DEC 0x5F-0x7E, at 0x80-0x9F: lines run to the
# cell's edges (all 4 columns across, all rows down) so boxes join up
LINES = {}     # the lines' codes and directions, drawn again for each height


def line_glyph(h, up, down, left, right):
    g = [[0] * 4 for _ in range(h)]
    mid, col = h // 2 - 1, 1
    if left:
        for x in range(0, col + 1): g[mid][x] = 3
    if right:
        for x in range(col, 4): g[mid][x] = 3
    if up:
        for y in range(0, mid + 1): g[y][col] = 3
    if down:
        for y in range(mid, h): g[y][col] = 3
    return g


def line(code, up=False, down=False, left=False, right=False):
    LINES[code] = (up, down, left, right)
    G[code] = line_glyph(H, up, down, left, right)


def dec(c):
    return 0x80 + c - 0x5F


line(dec(0x6A), up=True, left=True)
line(dec(0x6B), down=True, left=True)
line(dec(0x6C), down=True, right=True)
line(dec(0x6D), up=True, right=True)
line(dec(0x6E), up=True, down=True, left=True, right=True)
line(dec(0x71), left=True, right=True)
line(dec(0x74), up=True, down=True, right=True)
line(dec(0x75), up=True, down=True, left=True)
line(dec(0x76), up=True, left=True, right=True)
line(dec(0x77), down=True, left=True, right=True)
line(dec(0x78), up=True, down=True)
for c, row in ((0x6F, 0), (0x70, 2), (0x72, 6), (0x73, 8)):   # scan lines
    g = [[0] * 4 for _ in range(H)]
    g[row] = [3, 3, 3, 3]
    G[dec(c)] = g
G[dec(0x5F)] = [[0] * 4 for _ in range(H)]
G[dec(0x61)] = [[(1 if (x + y) % 2 else 0) * 3 for x in range(4)] for y in range(H)]   # checkerboard
FILL = (dec(0x61),)        # fills the whole cell at any height
put(dec(0x60), 2, ".#.", "###", "###", ".#.")          # diamond
put(dec(0x62), 2, "###", "###", "###")                 # (HT, FF, CR, LF: little blocks)
for c in (0x63, 0x64, 0x65, 0x68, 0x69):
    G[dec(c)] = G[dec(0x62)]
put(dec(0x66), 1, ".#.", "#.#", ".#.")                 # degree
put(dec(0x67), 2, ".#.", "###", ".#.", "...", "###")   # plus/minus
put(dec(0x79), 2, "..#", ".#.", "#..", ".#.", "..#", "...", "###")   # <=
put(dec(0x7A), 2, "#..", ".#.", "..#", ".#.", "#..", "...", "###")   # >=
put(dec(0x7B), 3, "###", "#.#", "#.#", "#.#")          # pi
put(dec(0x7C), 2, "..#", "###", ".#.", "###", "#..")   # not equal
put(dec(0x7D), 1, ".##", ".#.", ".#.", "###", ".#.", ".#.", "###")   # pound
put(dec(0x7E), 4, ".#.")                               # centred dot


def tall(g):
    """the 12-row version: a row of space on top, and one row of the body repeated"""
    rows = [r[:] for r in g]
    body = [y for y in range(H) if any(rows[y])]
    dup = None
    if body:
        lo, hi = min(body), max(body)
        # a row that's the same as the one above or below it (part of an
        # upright stroke), as near the middle as can be
        cands = [y for y in range(max(lo, 1), min(hi, 7) + 1)
                 if (y > lo and rows[y] == rows[y - 1]) or (y < hi and rows[y] == rows[y + 1])]
        if cands:
            dup = min(cands, key=lambda y: abs(y - 4.5))
        elif hi - lo >= 2:
            dup = (lo + hi) // 2
    if dup is None:
        # one row or two (a dash, a comma): don't thicken it, add space
        dup = next((y for y in range(4, H) if not any(rows[y])), 0)
    out = [[0] * 4] + rows[:dup + 1] + [rows[dup][:]] + rows[dup + 1:]
    return out


def glyph(code, h):
    if h != H and code in LINES:
        return line_glyph(h, *LINES[code])
    if h != H and code in FILL:
        return [[(1 if (x + y) % 2 else 0) * 3 for x in range(4)] for y in range(h)]
    g = G.get(code) or G[ord('?')]
    return g if h == H else tall(g)


def render(lines, h, path, scale, fg=(208, 208, 208), bg=(0, 0, 0)):
    from PIL import Image
    W = 320
    rows = 320 // h
    img = Image.new('RGB', (W, rows * h), bg)
    px = img.load()
    for r, text in enumerate(lines[:rows]):
        for c, ch in enumerate(text[:80]):
            g = glyph(ch if isinstance(ch, int) else ord(ch), h)
            for y in range(h):
                for x in range(4):
                    a = g[y][x] / 3
                    px[c * 4 + x, r * h + y] = tuple(int(bg[i] * (1 - a) + fg[i] * a) for i in range(3))
    if scale > 1:
        img = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
    img.save(path)


SAMPLE = [
    "calcterm's own 4-pixel font: 80 columns on the PicoCalc's 320x320 screen.",
    "",
    "-rw-r--r--  1 ctorkildson ctorkildson   192512 Nov  3 10:45 calcterm.uf2",
    "-rw-r--r--  1 ctorkildson ctorkildson     1903 Nov  2 12:30 calcterm.ti",
    "drwxr-xr-x  5 ctorkildson ctorkildson     4096 Nov  2 13:37 modules",
    "",
    "The quick brown fox jumps over the lazy dog.  0123456789  ()[]{}<>!?@#$%^&*",
    "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG.  il1I| O0o  rn m  cl d  ,.;:'\"`~",
    "",
    "ctorkildson@lubuntu:~$ sudo systemctl restart serial-getty@ttyUSB0.service",
    "ctorkildson@lubuntu:~$ ls /usr/share/terminfo/c | head -3",
    "",
    "Four score and seven years ago our fathers brought forth on this continent, a",
    "new nation, conceived in Liberty, and dedicated to the proposition that all",
    "men are created equal. Now we are engaged in a great civil war, testing whether",
    "that nation, or any nation so conceived and so dedicated, can long endure.",
    "",
]
box_top = [dec(0x6C)] + [dec(0x71)] * 30 + [dec(0x77)] + [dec(0x71)] * 30 + [dec(0x6B)]
box_mid = [dec(0x78)] + [ord(c) for c in " Name          Size  Modified "] + [dec(0x78)] + [ord(c) for c in " /snap           60  Nov  3   "] + [dec(0x78)]
box_bot = [dec(0x6D)] + [dec(0x71)] * 30 + [dec(0x76)] + [dec(0x71)] * 30 + [dec(0x6A)]
SAMPLE += [box_top, box_mid, box_bot, "",
           "         1         2         3         4         5         6         7         8",
           "12345678901234567890123456789012345678901234567890123456789012345678901234567890"]

def c_source():
    """font4.c: both heights, one byte per row (2 bits a pixel, the left pixel
    in the top bits), codes 0x00-0x9F"""
    out = ["/* font4.c -- made by tools/font4.py; edit that, not this. */",
           "#include <stdint.h>", ""]
    for h in (10, 12):
        out.append(f"const uint8_t font4x{h}[160][{h}] = {{")
        for code in range(160):
            g = glyph(code, h) if (code in G or code in LINES or code in FILL) else [[0] * 4 for _ in range(h)]
            rows = [(r[0] << 6) | (r[1] << 4) | (r[2] << 2) | r[3] for r in g]
            out.append("\t{" + ",".join(f"0x{b:02X}" for b in rows) + "},")
        out.append("};")
        out.append("")
    return "\n".join(out)


def sample_c():
    """the sample screen, as C strings (line-drawing codes as \\x escapes)"""
    out = ["static const char *const sample[] = {"]
    for line_ in SAMPLE:
        codes = [ord(c) for c in line_] if isinstance(line_, str) else line_
        s = "".join(chr(c) if 32 <= c < 127 and chr(c) not in '\\"' else f'\\x{c:02X}""' for c in codes)
        out.append(f'\t"{s}",')
    out.append("};")
    return "\n".join(out)


def c_source_1bit():
    """src/font4.c for calcterm: the 10-row font, plain on/off pixels (a
    pixel at two thirds or more is on), the left pixel in bit 3"""
    out = ["/* font4.c -- calcterm's 4x10 font for 80 columns.",
           " * Made by tools/font4.py --calcterm; edit that, not this. */",
           "#include <stdint.h>", "",
           "const uint8_t font4[160][10] = {"]
    for code in range(160):
        g = glyph(code, H) if (code in G or code in LINES or code in FILL) else [[0] * 4 for _ in range(H)]
        rows = [sum(8 >> x for x in range(4) if r[x] >= 2) for r in g]
        out.append(f"\t/* 0x{code:02X} */ {{" + ",".join(f"0x{b:X}" for b in rows) + "},")
    out.append("};")
    return "\n".join(out)


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--calcterm':
        open(sys.argv[2], "w").write(c_source_1bit() + "\n")
    elif len(sys.argv) == 3 and sys.argv[1] == '--c':
        open(sys.argv[2] + "/font4.c", "w").write(c_source() + "\n")
        open(sys.argv[2] + "/sample.h", "w").write(sample_c() + "\n")
    elif len(sys.argv) == 3 and sys.argv[1] == '--preview':
        out = sys.argv[2]
        for h in (10, 12):
            render(SAMPLE, h, f"{out}-{h}.png", 1)
            render(SAMPLE, h, f"{out}-{h}-x3.png", 3)
        print("drew", len([c for c in G if 32 <= c < 127]), "printable glyphs")
