#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Generate the proxy's .def: hooked exports are ours, everything else forwards to SDL_orig.dll.

usage: gen_sdl_def.py src/sdl_exports.txt build/sdl.def SDL_PollEvent SDL_UpdateRect ...
src/sdl_exports.txt is the real SDL.dll's export names, one per line (from objdump -p).
"""
import sys

exports_path, out_path, *hooked = sys.argv[1:]
names = [l.strip() for l in open(exports_path) if l.strip()]
missing = set(hooked) - set(names)
if missing:
    sys.exit(f'hooked functions not exported by SDL: {sorted(missing)}')
with open(out_path, 'w') as f:
    f.write('LIBRARY SDL.dll\nEXPORTS\n')
    for n in names:
        f.write(f'  {n}\n' if n in hooked else f'  {n} = SDL_orig.{n}\n')
print(f'{out_path}: {len(names)} exports, {len(hooked)} hooked')
