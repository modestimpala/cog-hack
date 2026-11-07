#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Install build/SDL.dll into the game. The original SDL.dll is kept as SDL_orig.dll (+ a one-time
# SDL.dll.pristine). Everything else of ours lives in <game>\hack\.
set -e
G="${COGMIND_DIR:-/mnt/c/Program Files (x86)/Steam/steamapps/common/Cogmind}"
cd "$(dirname "$0")"
[ -f build/SDL.dll ] || { echo "build/SDL.dll missing - run make"; exit 1; }
if /mnt/c/Windows/System32/tasklist.exe /FI "IMAGENAME eq COGMIND.exe" 2>/dev/null | grep -qi cogmind; then
    echo "Cogmind is running - close it first (SDL.dll is locked while the game runs)"; exit 1
fi
[ -f "$G/SDL.dll.pristine" ] || cp "$G/SDL.dll" "$G/SDL.dll.pristine"
cp "$G/SDL.dll.pristine" "$G/SDL_orig.dll"
cp build/SDL.dll "$G/SDL.dll"
mkdir -p "$G/hack/scripts" "$G/hack/plugins"
cp -u scripts/*.as "$G/hack/scripts/"                 # -u: only if the repo copy is newer
for d in plugins/*/; do                               # only the plugins this repo builds
    n=$(basename "$d"); [ -f "build/plugins/$n.dll" ] && cp -u "build/plugins/$n.dll" "$G/hack/plugins/"
done
echo "installed into $G"
echo "  plugins are opt-in: list them in hack\\plugins.txt (created on first launch)"
# files from earlier versions that now live under hack\ - left alone, safe to delete:
for old in "$G/hook.log" "$G/snapshots"; do [ -e "$old" ] && echo "  note: old $(basename "$old") in the game folder is unused now (hack\\ has the new one)"; done
true
