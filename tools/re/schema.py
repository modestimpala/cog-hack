# SPDX-License-Identifier: MPL-2.0
"""Extract a deserializer's field schema from objdump output: ordered (reader, offset) calls."""
import re

READERS = {
    0x9dca80: 'i32', 0x9d10c0: 'u8', 0x4098d0: 'str', 0x466260: 'oint', 0x9d31d0: 'vec_i32',
    0x4099c0: 'vec_str', 0x45f370: 'i32x2', 0x4376f0: 'list_9eddc0', 0x40a500: 'i32x2',
    0x9d7ed0: 'ref', 0x9d9de0: 'ref', 0x9da060: 'ref', 0x9d7460: 'ref', 0x9d8620: 'ref', 0x9d8010: 'ref',
    0x9d8100: 'list_ref_i32', 0x9d9ed0: 'vec_ref', 0x9d9f20: 'list_i32x4', 0x411ff0: 'rgb',
    0x9d2480: 'vec_i32x2', 0x9d4320: 'vec_i32', 0x9e1810: 'str',
}

def extract(asm_path, start, end):
    """Walk [start,end): each reader call; the field offset is the last `add reg,imm` / `lea reg,[reg+imm]`."""
    fields, last_off = [], 0
    for line in open(asm_path):
        m = re.match(r'\s+([0-9a-f]+):\s+(.*)', line)
        if not m: continue
        a = int(m.group(1), 16)
        if a < start: continue
        if a >= end: break
        ins = m.group(2)
        mo = re.search(r'add\s+e[a-d]x,0x([0-9a-f]+)$', ins) or re.search(r'lea\s+e[a-d]x,\[e[a-d]x\+0x([0-9a-f]+)\]', ins)
        if mo: last_off = int(mo.group(1), 16)
        mc = re.match(r'call\s+0x([0-9a-f]+)', ins)
        if mc:
            t = int(mc.group(1), 16)
            if t in READERS:
                fields.append((READERS[t], last_off, a))
            last_off = 0
    return fields
