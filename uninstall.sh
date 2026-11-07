#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Restore the original SDL.dll. Leaves <game>\hack\ (your scripts, config, logs) in place.
set -e
G="${COGMIND_DIR:-/mnt/c/Program Files (x86)/Steam/steamapps/common/Cogmind}"
[ -f "$G/SDL.dll.pristine" ] || { echo "no SDL.dll.pristine: nothing to restore"; exit 1; }
cp "$G/SDL.dll.pristine" "$G/SDL.dll"
rm -f "$G/SDL_orig.dll"
echo "restored original SDL.dll (hack\\ folder kept; delete it by hand if you want)"
