# SPDX-License-Identifier: MPL-2.0
"""Reverse pointer search: who points (with offset <= WIN) at the target, up to N levels, until a static .data address."""
import sys, array, bisect
import os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from minidump import Dump
DATA = (0xca9000, 0xd60000); WIN = 0x800
D = Dump(sys.argv[1]); target = int(sys.argv[2], 16); levels = int(sys.argv[3])
# load every dword once
chunks = []
for va, sz, off in D.regions:
    a = array.array('I'); b = D.m[off:off + sz - sz % 4]; a.frombytes(b); chunks.append((va, a))
frontier = {target: [hex(target)]}   # address -> description chain
for lvl in range(levels):
    lo_sorted = sorted(frontier)
    lo, hi = lo_sorted[0] - WIN, lo_sorted[-1]
    nxt = {}
    for va, a in chunks:
        for i, v in enumerate(a):
            if lo <= v <= hi:
                j = bisect.bisect_left(lo_sorted, v)
                if j < len(lo_sorted) and lo_sorted[j] - v <= WIN:
                    t = lo_sorted[j]; src = va + i * 4
                    if src not in nxt and src not in frontier:
                        nxt[src] = [hex(src) + f' (+{t - v:#x})'] + frontier[t]
    statics = {s: c for s, c in nxt.items() if DATA[0] <= s < DATA[1]}
    print(f'level {lvl+1}: {len(nxt)} pointer sources, {len(statics)} static', flush=True)
    for s, c in list(statics.items())[:30]:
        print('   STATIC', ' -> '.join(c), flush=True)
    if statics or len(nxt) > 20000: break
    frontier = nxt
