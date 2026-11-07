#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""The title art (art/coghack.txt) in its three forms.

    title_art.py art/coghack.txt build/title_art.inc   C include for src/overlay/title.c (the build runs this)
    title_art.py art/coghack.txt coghack.xp            REXPaint image: layer 1 circuits, layer 2 letters
    title_art.py coghack.xp art/coghack.txt            back from REXPaint (an image with one or two layers)
    title_art.py art/coghack.txt coghack.png snap_N.dmp [font]   render it with the game's art font

The text file has three sections of rows (docs/art.md): [letters], [circuits] and [bright], the
circuits' green level per cell as a hex digit (blank = 4). Rows are the full 69x28 title canvas. In the .xp, a circuit cell's colour is 0,level*16,0 and a letter is 0,217,0 on
0,64,0, so REXPaint shows it as the game does."""
import gzip
import os
import struct
import sys
import zlib

X0, W, H = 0, 69, 28
LETTER_FG, LETTER_BG = (0, 217, 0), (0, 64, 0)
TRANSPARENT = (255, 0, 255)


def parse(path):
    sections, cur = {}, None
    for line in open(path, encoding='utf-8'):
        line = line.rstrip('\n')
        if line.startswith('#'):
            continue
        if line.startswith('[') and line.endswith(']'):
            cur = line[1:-1]
            sections[cur] = []
        elif cur is not None:
            sections[cur].append(line)
    return sections


def grid(rows):
    return [(rows[y] if y < len(rows) else '').ljust(W)[:W] for y in range(H)]


def load(path):
    s = parse(path)
    return grid(s.get('letters', [])), grid(s.get('circuits', [])), grid(s.get('bright', []))


def write_text(path, letters, circ, bright):
    def rows(g):
        return [r.rstrip() for r in g]
    out = ["# COG-HACK title art: our own logo, drawn over the whole of the game's title logo. Rows are the",
           "# 69x28 art cells of the title canvas. Edit with a monospace font, or as .xp through",
           "# tools/title_art.py (docs/art.md).",
           "# letters: drawn in the title's letter colours (lit green on dark green)",
           "# circuits: the decoration, in dim green; bright: its green level / 16 per cell (blank = 4)",
           '[letters]'] + rows(letters) + ['[circuits]'] + rows(circ) + ['[bright]'] + rows(bright)
    open(path, 'w', encoding='utf-8').write('\n'.join(out) + '\n')


def build(src, dst):
    letters, circ, bright = load(src)
    cells = []
    for y in range(H):
        for x in range(W):
            if circ[y][x] != ' ':
                b = bright[y][x]
                cells.append((X0 + x, y, circ[y][x].encode('cp437')[0], 0, int(b, 16) if b != ' ' else 4))
            if letters[y][x] != ' ':
                cells.append((X0 + x, y, letters[y][x].encode('cp437')[0], 1, 0))
    with open(dst, 'w') as f:
        f.write('// generated from %s by tools/title_art.py: our COG-HACK logo, drawn over the title logo\n' % src)
        f.write('#define TITLE_ART_X0 %d\n#define TITLE_ART_W %d\n#define TITLE_ART_H %d\n' % (X0, W, H))
        f.write('static const struct { uint8_t x, y, glyph, kind, bright; } TITLE_ART[] = {\n')
        for i, c in enumerate(cells):
            f.write('    {%d,%d,%d,%d,%d},%s' % (c + ('\n' if i % 8 == 7 else ' ',)))
        f.write('\n};\n#define TITLE_ART_N %d\n' % len(cells))


def export_xp(src, dst):
    """REXPaint .xp: int32 version (-1), int32 layers, per layer int32 w, int32 h, then w*h cells
    column-major: int32 CP437 code, fg r g b, bg r g b; bg 255,0,255 = not painted."""
    letters, circ, bright = load(src)
    out = struct.pack('<ii', -1, 2)
    for layer in (0, 1):
        out += struct.pack('<ii', W, H)
        for x in range(W):
            for y in range(H):
                ch = (circ if layer == 0 else letters)[y][x]
                if ch == ' ':
                    out += struct.pack('<iBBBBBB', 32, 0, 0, 0, *TRANSPARENT)
                elif layer == 0:
                    b = bright[y][x]
                    level = int(b, 16) if b != ' ' else 4
                    out += struct.pack('<iBBBBBB', ch.encode('cp437')[0], 0, level * 16, 0, 0, 0, 0)
                else:
                    out += struct.pack('<iBBBBBB', ch.encode('cp437')[0], *LETTER_FG, *LETTER_BG)
    with gzip.open(dst, 'wb') as f:
        f.write(out)


def import_xp(src, dst):
    d = gzip.open(src).read()
    ver, nl = struct.unpack_from('<ii', d, 0)
    off = 8 if ver < 0 else 4
    if ver >= 0:
        nl = ver
    layers = []
    for _ in range(nl):
        w, h = struct.unpack_from('<ii', d, off)
        off += 8
        cells = {}
        for x in range(w):
            for y in range(h):
                code, fr, fg, fb, br, bg, bb = struct.unpack_from('<iBBBBBB', d, off)
                off += 10
                if (br, bg, bb) != TRANSPARENT and code not in (0, 32):
                    cells[(x, y)] = (code, (fr, fg, fb), (br, bg, bb))
        layers.append(cells)
    letters = [[' '] * W for _ in range(H)]
    circ = [[' '] * W for _ in range(H)]
    bright = [[' '] * W for _ in range(H)]
    for i, cells in enumerate(layers):
        for (x, y), (code, fg, bg) in cells.items():
            if x >= W or y >= H:
                continue
            ch = bytes([code & 0xff]).decode('cp437')
            if (len(layers) > 1 and i == 1) or (len(layers) == 1 and bg == LETTER_BG):
                letters[y][x] = ch
            else:
                circ[y][x] = ch
                level = max(1, min(15, round(fg[1] / 16)))
                bright[y][x] = '%x' % level if level != 4 else ' '
    write_text(dst, [''.join(r) for r in letters], [''.join(r) for r in circ], [''.join(r) for r in bright])


def font_from_snapshot(dump_path, index):
    """The game's font `index` as (glyph w, glyph h, columns, alpha(x, y)) from a Ctrl+F11 snapshot:
    font manager (ADDR_FONT_MGR) +0x1c vector<XFontData*>, XFontData +0x28 SDL_Surface*, +0x2c/+0x30 glyph size."""
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 're'))
    from minidump import Dump
    d = Dump(dump_path)
    fonts = d.u32(d.u32(0xd235a0) + 0x1c)
    font = d.u32(fonts + 4 * index)
    surf, gw, gh = struct.unpack('<III', d.read(font + 0x28, 12))
    fmt, w, h, pitch, pixels = struct.unpack_from('<IiiH2xI', d.read(surf + 4, 20))
    ashift = d.read(fmt, 14)[13]
    sheet = d.read(pixels, pitch * h)
    if not sheet or len(sheet) < pitch * h:
        sys.exit('font %d is not in this snapshot' % index)
    def alpha(x, y):
        return sheet[y * pitch + x * 4 + ashift // 8]
    return gw, gh, w // gw, alpha


def render_png(src, dst, dump_path, index=5):
    """Draw the art as the title shows it once lit: letters 0,217,0 on 0,64,0, circuits 0,level*16,0
    on transparent. Cropped to the drawing plus a one-cell margin."""
    letters, circ, bright = load(src)
    gw, gh, cols, alpha = font_from_snapshot(dump_path, index)
    used = [(x, y) for y in range(H) for x in range(W) if letters[y][x] != ' ' or circ[y][x] != ' ']
    x0, x1 = max(0, min(x for x, _ in used) - 1), min(W, max(x for x, _ in used) + 2)
    y0, y1 = max(0, min(y for _, y in used) - 1), min(H, max(y for _, y in used) + 2)
    pw, ph = (x1 - x0) * gw, (y1 - y0) * gh
    img = bytearray(pw * ph * 4)                         # RGBA, transparent

    def cell(cx, cy, ch, fg, bg):
        code = ch.encode('cp437')[0]
        sx, sy = code % cols * gw, code // cols * gh
        for yy in range(gh):
            row = ((cy - y0) * gh + yy) * pw
            for xx in range(gw):
                o = (row + (cx - x0) * gw + xx) * 4
                a = alpha(sx + xx, sy + yy)
                if bg:                                   # opaque cell: glyph blended over its background
                    img[o:o + 4] = bytes([bg[i] + (fg[i] - bg[i]) * a // 255 for i in range(3)] + [255])
                elif a:
                    img[o:o + 4] = bytes(list(fg) + [a])
    for y in range(y0, y1):
        for x in range(x0, x1):
            if letters[y][x] != ' ':
                cell(x, y, letters[y][x], LETTER_FG, LETTER_BG)
            elif circ[y][x] != ' ':
                b = bright[y][x]
                cell(x, y, circ[y][x], (0, (int(b, 16) if b != ' ' else 4) * 16, 0), None)

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    raw = b''.join(b'\0' + bytes(img[y * pw * 4:(y + 1) * pw * 4]) for y in range(ph))
    with open(dst, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', pw, ph, 8, 6, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))
    print('%s: %dx%d (font %d, %dx%d cells)' % (dst, pw, ph, index, gw, gh))


def main():
    src, dst = sys.argv[1], sys.argv[2]
    if dst.endswith('.png'):
        if len(sys.argv) < 4:
            sys.exit('render needs a snapshot: title_art.py art/coghack.txt out.png snap_N.dmp [font]')
        render_png(src, dst, sys.argv[3], int(sys.argv[4]) if len(sys.argv) > 4 else 5)
    elif src.endswith('.xp'):
        import_xp(src, dst)
    elif dst.endswith('.xp'):
        export_xp(src, dst)
    else:
        build(src, dst)


main()
