# SPDX-License-Identifier: MPL-2.0
"""Print lookup tables taken from the game (not committed: they are the game's own text).

    tables.py rtti      COGMIND.exe        class names from the exe's RTTI (.?AVName@@ / .?AUName@@)
    tables.py strings   COGMIND.exe        printable ASCII strings (min length 4), like `strings`
    tables.py commands  snap_N.dmp         command id -> name (ADDR_COMMAND_NAMES)
    tables.py itemtypes snap_N.dmp         item type names (ADDR_ITEMTYPE_NAMES)

Addresses are read from include/cogmind.h, so they follow the build the header was mapped on.
Command and item type names are std::strings built at startup, so they need a Ctrl+F11 snapshot.
"""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

HEADER = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'include', 'cogmind.h')


def header_value(name):
    src = open(HEADER).read()
    m = re.search(r'X\(%s,\s*(0x[0-9a-fA-F]+)\)' % name, src) or re.search(r'#define %s\s+(\w+)' % name, src)
    if not m:
        sys.exit('%s not in cogmind.h' % name)
    return int(m.group(1), 0)


def rtti(exe):
    names = set(m.group(1).decode() for m in re.finditer(rb'\.\?A[VU](\w+)@@', open(exe, 'rb').read()))
    for n in sorted(names):
        print(n)


def strings(exe):
    for m in re.finditer(rb'[\x20-\x7e\t]{4,}', open(exe, 'rb').read()):
        print(m.group().decode())


def std_strings(dump, base, count, stride=28):
    from minidump import Dump
    d = Dump(dump)
    for i in range(count):
        yield i, base + i * stride, d.msvc_string(base + i * stride)


def commands(dump):
    for i, _, s in std_strings(dump, header_value('ADDR_COMMAND_NAMES'), header_value('COMMAND_COUNT')):
        print('%d\t%s' % (i, s))


def itemtypes(dump):
    for _, va, s in std_strings(dump, header_value('ADDR_ITEMTYPE_NAMES'), 64):   # until the names stop
        if not s:
            break
        print('%#x\t%s' % (va, s))


if __name__ == '__main__':
    cmds = {'rtti': rtti, 'strings': strings, 'commands': commands, 'itemtypes': itemtypes}
    if len(sys.argv) != 3 or sys.argv[1] not in cmds:
        sys.exit(__doc__)
    cmds[sys.argv[1]](sys.argv[2])
