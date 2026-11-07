# SPDX-License-Identifier: MPL-2.0
"""Reader for UI recordings (hack\\snapshots\\rec_N.bin, see src/debug/record.c).

    from uirec import frames
    for tick, consoles in frames('rec_1.bin'):        # consoles: list of Console
        for c in consoles: print(tick, c.cls, c.w, c.h, c.cell(3, 4))

Run as a script for a summary: frame times and how many cells changed per console.
"""
import struct
import sys

CELL = 20


class Console:
    __slots__ = ('addr', 'cls', 'w', 'h', 'ax', 'ay', 'cells')

    def __init__(self, addr, cls, w, h, ax, ay, cells):
        self.addr, self.cls, self.w, self.h, self.ax, self.ay, self.cells = addr, cls, w, h, ax, ay, cells

    def cell(self, x, y):
        """(font, char, glyph, fg, bg) of cell x, y (column-major storage)."""
        o = (x * self.h + y) * CELL
        font, ch, glyph = struct.unpack_from('<III', self.cells, o)
        return font, ch, glyph, tuple(self.cells[o + 12:o + 15]), tuple(self.cells[o + 15:o + 18])


def frames(path):
    data = open(path, 'rb').read()
    assert data[:6] == b'CMREC1', 'not a UI recording'
    off = 8
    while off + 12 <= len(data):
        tag, tick, ncons = struct.unpack_from('<4sII', data, off)
        assert tag == b'FRAM', f'bad frame at {off}'
        off += 12
        consoles = []
        for _ in range(ncons):
            addr, cls, w, h, ax, ay = struct.unpack_from('<I32siiii', data, off)
            off += 52
            n = w * h * CELL
            consoles.append(Console(addr, cls.split(b'\0')[0].decode('latin1'), w, h, ax, ay, data[off:off + n]))
            off += n
        yield tick, consoles


if __name__ == '__main__':
    prev = {}
    t0 = None
    for tick, consoles in frames(sys.argv[1]):
        if t0 is None:
            t0 = tick
        parts = []
        for c in consoles:
            old = prev.get(c.addr)
            changed = sum(1 for i in range(0, len(c.cells), CELL) if not old or c.cells[i:i + CELL] != old[i:i + CELL])
            parts.append(f'{c.cls}@{c.addr:08x} {changed}')
            prev[c.addr] = c.cells
        print(f'{tick - t0:6d} ms  ' + '  '.join(parts))
