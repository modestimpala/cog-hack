# SPDX-License-Identifier: MPL-2.0
"""Minimal reader for full-memory minidumps written by the hook (Ctrl+F11).

    from minidump import Dump
    d = Dump('snap_123.dmp')
    d.read(0xd046b8, 64)          # bytes at a virtual address
    d.u32(addr); d.find(b'Ion Engine')  # -> list of VAs
"""
import bisect
import mmap
import re
import struct
import sys


class Dump:
    def __init__(self, path):
        self.f = open(path, 'rb')
        self.m = mmap.mmap(self.f.fileno(), 0, access=mmap.ACCESS_READ)
        sig, ver, nstreams, dir_rva = struct.unpack_from('<4sIII', self.m, 0)
        assert sig == b'MDMP', 'not a minidump'
        self.regions = []  # (va, size, file_offset)
        for i in range(nstreams):
            stype, size, rva = struct.unpack_from('<III', self.m, dir_rva + i * 12)
            if stype == 9:  # Memory64ListStream
                n, base_rva = struct.unpack_from('<QQ', self.m, rva)
                off = base_rva
                for j in range(n):
                    va, sz = struct.unpack_from('<QQ', self.m, rva + 16 + j * 16)
                    self.regions.append((va, sz, off))
                    off += sz
            elif stype == 5:  # MemoryListStream (heap+data snapshots, the default since snapshot_full=0)
                n = struct.unpack_from('<I', self.m, rva)[0]
                for j in range(n):
                    va, sz, data_rva = struct.unpack_from('<QII', self.m, rva + 4 + j * 16)
                    self.regions.append((va, sz, data_rva))
        self.regions.sort()
        self._starts = [r[0] for r in self.regions]

    def _loc(self, va):
        i = bisect.bisect_right(self._starts, va) - 1
        if i >= 0:
            rva, sz, off = self.regions[i]
            if va < rva + sz:
                return off + (va - rva), rva + sz - va
        return None, 0

    def read(self, va, n):
        """Read up to n bytes, continuing across adjacent regions; stops at a gap."""
        out = b''
        while n > 0:
            off, avail = self._loc(va)
            if off is None:
                break
            k = min(n, avail)
            out += self.m[off:off + k]
            va += k; n -= k
        return out or None

    def u32(self, va):
        b = self.read(va, 4)
        return struct.unpack('<I', b)[0] if b and len(b) == 4 else None

    def i32(self, va):
        b = self.read(va, 4)
        return struct.unpack('<i', b)[0] if b and len(b) == 4 else None

    def find(self, needle, limit=100000):
        """All VAs where `needle` (bytes or compiled regex) occurs."""
        pat = needle if hasattr(needle, 'finditer') else re.compile(re.escape(needle))
        out = []
        for va, sz, off in self.regions:
            for mt in pat.finditer(self.m, off, off + sz):
                out.append(va + mt.start() - off)
                if len(out) >= limit:
                    return out
        return out

    def msvc_string(self, va):
        """Decode an MSVC10 std::string at va (16-byte SSO buf, size @+16, cap @+20)."""
        b = self.read(va, 28)
        if not b or len(b) < 28:
            return None
        size, cap = struct.unpack_from('<II', b, 16)
        if size > cap or cap > 1 << 20:
            return None
        if cap < 16:
            return b[:size].decode('latin1')
        p = struct.unpack_from('<I', b, 0)[0]
        s = self.read(p, size)
        return s.decode('latin1') if s else None


if __name__ == '__main__':
    d = Dump(sys.argv[1])
    total = sum(sz for _, sz, _ in d.regions)
    print(f'{len(d.regions)} regions, {total / 1e6:.1f} MB')
