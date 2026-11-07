# SPDX-License-Identifier: MPL-2.0
"""Decode/encode Cogmind data/*.bin (gzip'd binary serialization with RNG-driven int obfuscation).

The game's obfuscated-int reader (FUN_00466260) and list loader (FUN_009e2e90) consult a global
MT19937 (object 0xd21dd8) via roll(1..100) <= n. Decoding replays that RNG from its seed.
"""
import gzip, struct

class MT:
    def __init__(self, seed):
        self.mt = [0] * 624; self.mt[0] = seed & 0xffffffff
        for i in range(1, 624):
            self.mt[i] = (0x6c078965 * (self.mt[i-1] ^ (self.mt[i-1] >> 30)) + i) & 0xffffffff
        self.i = 624
    def u32(self):
        if self.i >= 624:
            for k in range(624):
                y = (self.mt[k] & 0x80000000) | (self.mt[(k+1) % 624] & 0x7fffffff)
                self.mt[k] = self.mt[(k+397) % 624] ^ (y >> 1) ^ (0x9908b0df if y & 1 else 0)
            self.i = 0
        y = self.mt[self.i]; self.i += 1
        y ^= y >> 11; y ^= (y << 7) & 0x9d2c5680; y ^= (y << 15) & 0xefc60000; y ^= y >> 18
        return y
    def roll100(self):                    # FUN_00406f00(1.0, 100.0), exact integer form
        return (((2 * self.u32() + 1) * 50) >> 33) + 1
    def flag(self, n):                    # FUN_00406e20(n)
        return self.roll100() <= n

class Reader:
    def __init__(self, data, rng):
        self.d, self.p, self.rng = data, 0, rng
    def i32(self):
        v = struct.unpack_from('<i', self.d, self.p)[0]; self.p += 4; return v
    def u32(self):
        v = struct.unpack_from('<I', self.d, self.p)[0]; self.p += 4; return v
    def u8(self):
        v = self.d[self.p]; self.p += 1; return v
    def str(self):
        n = self.u32()
        if n > 1 << 20: raise ValueError(f'string length {n} at {self.p-4}')
        s = self.d[self.p:self.p+n]; self.p += n; return s.decode('latin1')
    def oint(self):                       # FUN_00466260
        if not self.rng.flag(2): return self.i32()
        n = self.u32()
        if n > 4096: raise ValueError(f'obfuscated int count {n} at {self.p-4}')
        return {'sum_of': [self.i32() for _ in range(n)]}
    def ref(self):                        # u8 present, then i32 index into a def table
        return self.i32() if self.u8() else None
    def read(self, t):
        if t == 'i32': return self.i32()
        if t == 'u8': return self.u8()
        if t == 'str': return self.str()
        if t == 'oint': return self.oint()
        if t == 'ref': return self.ref()
        if t == 'i32x2': return [self.i32(), self.i32()]
        if t == 'rgb': return [self.u8(), self.u8(), self.u8()]
        if t == 'vec_i32': return [self.i32() for _ in range(self.u32())]
        if t == 'vec_str': return [self.str() for _ in range(self.u32())]
        if t == 'vec_ref': return [self.ref() for _ in range(self.u32())]
        if t == 'vec_i32x2': return [[self.i32(), self.i32()] for _ in range(self.u32())]
        if t == 'list_ref_i32': return [[self.ref(), self.i32()] for _ in range(self.u32())]
        if t == 'list_i32x4': return [[self.i32() for _ in range(4)] for _ in range(self.u32())]
        if t == 'list_9eddc0': return [self.elem_9eddc0() for _ in range(self.u32())]
        raise NotImplementedError(t)
    def elem_9eddc0(self):                # FUN_009eddc0: w, h, then w*h raw 20-byte cells (art grid)
        w, h = self.i32(), self.i32()
        n = w * h * 20
        if w < 0 or h < 0 or n > 1 << 24: raise ValueError(f'bad grid {w}x{h} at {self.p-8}')
        cells = self.d[self.p:self.p+n].hex(); self.p += n
        return {'w': w, 'h': h, 'cells_hex': cells}

def load(path):
    return gzip.open(path).read()

class Rolls:
    """Precomputed roll100() sequence; .pos is the draw index. Lets us sync to an unknown RNG position."""
    def __init__(self, seed, n):
        mt = MT(seed); self.r = bytes(mt.roll100() for _ in range(n)); self.pos = 0
    def flag(self, n):
        v = self.r[self.pos]; self.pos += 1; return v <= n
