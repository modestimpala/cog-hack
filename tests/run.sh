#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Builds a throwaway game-like directory, runs build/test_script.exe in it, checks expected output.
set -e
cd "$(dirname "$0")/.."
T=build/testenv
rm -rf "$T" && cp -r tests/fixtures "$T" && mkdir -p "$T/hack/plugins"
cp scripts/autoexec.as "$T/hack/scripts/"
cp build/plugins/example.dll "$T/hack/plugins/example.dll"
cp build/plugins/example.dll "$T/hack/plugins/unlisted.dll"
cp build/plugins/example.dll "$T/hack/plugins/pinned_bad.dll"
cp build/plugins/ui_inspect.dll "$T/hack/plugins/"
printf 'example.dll\npinned_bad.dll 0000000000000000000000000000000000000000000000000000000000000000\nui_inspect.dll\n' > "$T/hack/plugins.txt"
cp build/test_script.exe "$T/"

# (no pipe into tr: sh has no pipefail, the exit code must be the test's)
U="$PWD/build/unit.out"
unit() { r=0; "$@" > "$U" 2>&1 || r=$?; tr -d '\r' < "$U"; [ $r = 0 ] || { echo "$1 FAILED"; exit 1; }; }
unit build/test_ui.exe
G="${COGMIND_DIR:-/mnt/c/Program Files (x86)/Steam/steamapps/common/Cogmind}"
EXE="$(wslpath -w "$G/COGMIND.exe" 2>/dev/null || echo none)"
cd build
unit ./test_sigs.exe "$EXE"                          # built-in signatures
unit ./test_sigs.exe "$EXE" ../sigs/cogmind.sigs     # the same file loaded as a sidecar
cd ..

cd "$T"

timeout 120 ./test_script.exe \
  'print("hello " + (6*7))' \
  'equip(4)' \
  'print("add=" + add(40, 2) + " " + shout("plugin"))' \
  'safe_div(1, 0)' \
  'print("peek=" + peek(0x400000))' \
  'print("set=" + set_core(5) + " " + set_item_hp(0, 1) + " " + bot_set_core(0, 1))' \
  'press("shift+a")' --synth \
  'int[] z; z[3] = 1' \
  'this is not valid' \
  'while (true) {}' \
  '.load ../../evil.as' \
  '.load t_escape.as' \
  '.load t_include.as' \
  '.load t_runaway.as' --frame --frame \
  'print("frames=" + frames)' \
  'print("in_game=" + in_game() + " core=" + core())' \
  --ticks 300 --panels \
  'print("log=[" + log(5) + "] combat=[" + combat_log() + "]")' \
  'print("ui=[" + ui_hovered() + "|" + ui_at(3, 4) + "|" + ui_text("CParts") + "|" + screen_text() + "|" + ui_tree() + "] m=" + mouse_x() + "," + mouse_y())' \
  'ui_label("t", 1, 2, "hi"); ui_label("t", 1, 2, ""); ui_inspect(false); print("labels ok")' \
  'print("addr=" + formatUInt(game_addr("ADDR_WORLD_PTR"), "H") + " bogus=" + game_addr("NOPE") + " ver=" + game_version())' \
  'print("set=" + setting("script_timeout_ms") + "|" + setting("overlay_pos") + "|" + setting("nope") + "|")' \
  '.load t_ui.as' --click 4 4 --click 20 9 --panels \
  --hotkey ctrl+shift+f11 --hotkey pause --hotkey "pause, ctrl+f12" --hotkey "alt+x" --hotkey "ctrl+bogus" \
  > out.txt 2>&1 || true
# crash report: a real fault in the test process (it dies; the report and dump must be there)
timeout 60 ./test_script.exe --crash > crash.txt 2>&1 || true
tr -d '\r' < out.txt > out.log
tr -d '\r' < crash.txt >> out.log
fail=0
expect() { if grep -qF -- "$1" out.log; then echo "  ok   $1"; else echo "  FAIL $1"; fail=1; fi; }
reject() { if grep -qF -- "$1" out.log; then echo "  FAIL (unexpected) $1"; fail=1; else echo "  ok   no '$1'"; fi; }
expect "AngelScript 2.38.0 ready"
expect "loaded autoexec.as"
expect "hello 42"
expect "plugin example.dll -> 0 (unpinned)"
expect "add=42 PLUGIN!"
expect "plugin unlisted.dll not enabled"
expect "plugin pinned_bad.dll REFUSED"
expect "exception: safe_div: division by zero"
expect "peek needs dev_mode=1"
expect "set_core needs dev_mode=1"
expect "set_item_hp needs dev_mode=1"
expect "bot_set_core needs dev_mode=1"
expect "set=false false false"
expect "synth type=2 sym=97 mod=1001"
expect "synth type=3 sym=97 mod=1001"
expect "exception: Index out of bounds"
expect "No matching symbol 'this'"
expect "aborted: ran longer than 300 ms"
expect "load refused (outside hack\\scripts): ../../evil.as"
expect "#include refused (outside hack\\scripts)"
reject "SHOULD NOT RUN"
expect "include ok: twice(21)=42"
expect "aborted: ran longer than 100 ms"
expect "(onFrame disabled until the next .load)"
expect "frames=1"
expect "in_game=false core=0"
expect "cmd: unknown command 'CMD_INVENTORY_EQUIP4'"
# UI (no game in the test process: the reader must fail soft)
expect "plugin ui_inspect.dll -> 0"
expect "ui=[||||] m=0,0"
expect "labels ok"
# address table: plugins get the host's resolved value (the test host moved ADDR_WORLD_PTR)
expect "addr=CF0D2C bogus=0 ver=not checked"
# config.txt: the fixture's value wins, missing keys get defaults and are appended to the file
expect "set=100|1||"
expect "hotkey 'ctrl+shift+f11' -> ok sym=292 mods=3"
expect "hotkey 'pause' -> ok sym=19 mods=0"
expect "hotkey 'pause, ctrl+f12' -> ok sym=19 mods=0"
expect "hotkey 'alt+x' -> ok sym=120 mods=4"
expect "hotkey 'ctrl+bogus' -> BAD"
grep -q "^console_at_start=0" hack/config.txt && grep -q "^script_timeout_ms=100 " hack/config.txt && grep -q "^// Hotkeys" hack/config.txt \
  && echo "  ok   config.txt rewritten in full, fixture values kept" || { echo "  FAIL config.txt not updated"; fail=1; }
expect "loaded t_ui.as"
expect "cell=false 0 0"
expect "click 4,4 button 1 -> OK"
expect "swallowed=1"
expect "click 20,9 button 1 -> passed on"
expect "swallowed=0"
expect "panel line"
reject "No matching signature"
expect "crash reports on"
expect "CRASH: access violation (write to 00000002) at"
expect "running: test (cog-hack code was on the stack)"
expect "test_script.exe+"
expect "last commands: 18"
ls hack/snapshots/crash_*.dmp >/dev/null 2>&1 && echo "  ok   crash dump written" || { echo "  FAIL no crash dump"; fail=1; }
[ $fail = 0 ] && echo "all tests passed" || { echo "--- output ---"; cat out.log; exit 1; }
