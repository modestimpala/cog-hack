# SPDX-License-Identifier: MPL-2.0
"""ui_catalog.py <snapshots dir>: markdown table of every UI console class seen in Ctrl+Shift+F11 (ui_*.txt)
and Ctrl+F11 (snap_*.txt) captures: parent, common bounds, captures it appears in, sample text."""
import glob, re, collections, sys
files = sorted(glob.glob(sys.argv[1] + "/ui_*.txt") + glob.glob(sys.argv[1] + "/snap_*.txt"))
info = {}  # cls -> dict
pat = re.compile(r'^( *)(\(hidden\) )?(\S+) (-?\d+),(-?\d+) (\d+)x(\d+) font (\d+)  ([0-9a-f]{8})  ?(.*)$')
for f in files:
    stack = []
    seen_here = set()
    for line in open(f, encoding='utf-8', errors='replace').read().split('---- ui tree ----\n', 1)[1].splitlines():
        m = pat.match(line)
        if not m: continue
        ind, hidden, cls, x, y, w, h, font, addr, text = m.groups()
        d = len(ind) // 2
        stack = stack[:d] + [cls]
        par = stack[d - 1] if d else '-'
        e = info.setdefault(cls, {'parents': collections.Counter(), 'bounds': collections.Counter(), 'texts': [], 'files': set(), 'vis': 0, 'hid': 0, 'font': collections.Counter()})
        e['parents'][par] += 1
        e['bounds']['%s,%s %sx%s' % (x, y, w, h)] += 1
        e['font'][font] += 1
        e['files'].add(f)
        if hidden: e['hid'] += 1
        else: e['vis'] += 1
        t = text.strip()
        if t and t not in e['texts'] and len(e['texts']) < 3 and not set(t) <= set('·│─┌┐└┘├┤ ▓'): e['texts'].append(t[:60])
for cls in sorted(info, key=lambda c: (info[c]['parents'].most_common(1)[0][0], c)):
    e = info[cls]
    b = e['bounds'].most_common(2)
    print('| `%s` | %s | %s | %d/%d | %s |' % (cls, e['parents'].most_common(1)[0][0], '; '.join(k for k, _ in b),
          len(e['files']), len(files), ' · '.join(x.replace('|', '/') for x in e['texts'])))
