# SPDX-License-Identifier: MPL-2.0
"""Find static pointer paths  [.data global] -> +o1 -> +o2 ... -> target  in a minidump."""
import sys, struct
import os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from minidump import Dump

DATA = (0xca9000, 0xcebe00)
WIN = 0x400

def paths(D, target, depth=3):
    # level sets: addresses whose *value* equals the goal at each level
    found = []
    statics = range(DATA[0], DATA[1], 4)
    raw = D.read(DATA[0], DATA[1] - DATA[0])
    svals = struct.unpack(f'<{len(raw)//4}I', raw[:len(raw)//4*4])
    def follow(base, chain, lvl):
        blk = D.read(base, WIN)
        if not blk: return
        vals = struct.unpack(f'<{len(blk)//4}I', blk[:len(blk)//4*4])
        for i, v in enumerate(vals):
            if v == target:
                found.append(chain + [i * 4])
            elif lvl < depth and 0x10000 <= v < 0x7fff0000 and v != base:
                if lvl + 1 == depth:
                    # last hop: cheap check only
                    b2 = D.read(v, WIN)
                    if b2 and struct.pack('<I', target) in b2:
                        k = b2.find(struct.pack('<I', target))
                        while k != -1:
                            if k % 4 == 0: found.append(chain + [i * 4, k])
                            k = b2.find(struct.pack('<I', target), k + 1)
                else:
                    follow(v, chain + [i * 4], lvl + 1)
    for s, v in zip(statics, svals):
        if v == target: found.append([s, 'direct'])
        elif 0x10000 <= v < 0x7fff0000 and not (DATA[0] <= v < DATA[1]):
            follow(v, [s], 1)
    return found

if __name__ == '__main__':
    D = Dump(sys.argv[1]); t = int(sys.argv[2], 16); dep = int(sys.argv[3]) if len(sys.argv) > 3 else 3
    for p in paths(D, t, dep):
        print(hex(p[0]), ' -> '.join(f'+{o:#x}' if isinstance(o, int) else o for o in p[1:]))
