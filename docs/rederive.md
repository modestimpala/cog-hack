# Rederiving the game data

The repo does not ship anything taken from the game except what the build needs: the address
table in `include/cogmind.h` and the byte signatures in `sigs/cogmind.sigs`. Everything else can be
regenerated from your own copy of the game. This page covers how.

You need Python 3, a Ctrl+F11 snapshot (`<game>\hack\snapshots\snap_N.dmp`) for anything that
lives in memory, and a disassembler (Ghidra works well) for following code.

## Lookup tables

`tools/re/tables.py` prints each one to stdout. Keep the output wherever you like (`notes/` is
ignored by git).

| table | command |
|---|---|
| RTTI class names | `tables.py rtti COGMIND.exe` |
| printable strings | `tables.py strings COGMIND.exe` |
| command id to name | `tables.py commands snap_N.dmp` |
| item type names | `tables.py itemtypes snap_N.dmp` |
| UI class catalog | `ui_catalog.py <game>/hack/snapshots` (reads the `ui_*.txt` / `snap_*.txt` captures) |

## After a game update

1. Install and start the game. `hack\hook.log` reports the build check:
   - `known` or `compatible`: nothing moved. You are done.
   - `ADAPTED`: some addresses moved, but every one was found again. The log lists each one as
     `MOVED -> <new address>`. Reads, scripts and plugins work. Writes and game calls stay off until
     the struct layouts are checked (steps 3 and 4).
   - `UNSUPPORTED`: an address could not be found (`MISSING`) or its code sites disagree
     (`CONFLICT`). Re-find it by hand (step 2).
2. Re-find any lost address. The table below says where each one came from. In short: find the
   code that uses it (a string xref, a known function) and read the operand.
3. Put the new values in the `CM_ADDRESSES` list in `include/cogmind.h`.
4. Check the struct offsets. Take a snapshot in a run and run `build/replay.exe snap_N.dmp --ui`.
   Its numbers (core, energy, parts, location, UI tree) must match what the game shows. If a field
   is off, find it again (see "Methods").
5. Regenerate the signatures and test:

   ```sh
   python3 tools/re/gen_sigs.py "<game>/COGMIND.exe" include/cogmind.h > sigs/cogmind.sigs
   make test
   ```

To try new signatures without rebuilding, copy the `.sigs` file to `<game>\hack\sigs.txt`. It
replaces the built-in set at startup (hook.log says which set was used). Delete it to go back.

## Where each address came from

Function names are Ghidra's (`FUN_<address>`) in Beta 17.1 build 260906a.

| address | how to find it |
|---|---|
| `ADDR_WORLD_PTR` | HUD draw function (references the string `"% exposed)"`) does `mov ecx,[World]` then calls getPlayer (`[this+0x66c]`) |
| `ADDR_ENT_REGISTRY` | getPlayer's handle lookup returns it; handles are `idx = h & 0xffff`, `gen = h >> 16` |
| `ADDR_ITEM_REGISTRY` | the static next to the entity registry where all of the player's item handles resolve |
| `ADDR_PROP_REGISTRY`, `ADDR_LOC_REGISTRY` | entity registry +4 / +0x14 |
| `ADDR_ITEM_DEFS`, `ADDR_BOT_DEFS` | the static `std::vector<Def*>` in .data that holds a known def pointer |
| `ADDR_PLAYER_MAX_CORE` | the HUD draw function reads it for the core maximum |
| `FN_ENT_MAX_ENERGY`, `FN_ENT_MAX_MATTER` | called by the HUD draw function for the energy / matter maxima |
| `ADDR_COMMAND_NAMES` | the static `std::string[]` the key binding code indexes by command id |
| `FN_COMMAND_CTOR`, `FN_COMMAND_CTOR_POS`, `FN_XROOT_HANDLE` | the key binding code: builds a Command and calls the root console's handler |
| `ADDR_CMD_OBSERVER` | the null function pointer the root handler checks before every command |
| `ADDR_MSG_LOG`, `ADDR_COMBAT_LOG` | search a snapshot for the last log line on screen, then `revpath.py` back to a static |
| `ADDR_SCREEN_SURFACE` | the surface the present function passes to `SDL_Flip` |
| `ADDR_FONT_MGR` | the renderer indexes its font vector (`+0x1c`) with each cell's font id |
| `ADDR_DISPLAY`, `ADDR_UI_ROOT` | the root console (class `XRoot`) is display +0x6c; display holds the pixel origin |
| `ADDR_MOUSE` | the object the SDL mouse motion handler updates (pixels, cell, hovered console) |
| `ADDR_CUR_LOCATION`, `ADDR_MAP_NAMES` | the HUD's `"Loc: -"` code: a location handle, then its map type indexes the name table |
| `ADDR_MAP_W` (+ `_H`, `_CELLS`) | the static map grid `{w, h, Cell**}` that owns the cells holding floor item handles |
| `ADDR_ITEM_IDENTIFIED` | the item display name function checks it before printing "Prototype ..." |
| `ADDR_ITEMTYPE_NAMES`, `ADDR_BOTCLASS_NAMES`, `ADDR_MACHINE_NAMES` | static `std::string` tables, found from one of their strings |

## Methods

- **A changing number** (core, energy, heat): take two snapshots where the value differs, search for
  aligned 32-bit values that equal the first in one and the second in the other.
- **Def fields**: compare every offset across all live definitions against
  [Cog-Minder](https://github.com/noemica/cog-minder)'s items.json / bots.json; the right offset
  matches for all of them.
- **Who points at this?** `revpath.py snap.dmp <address> <levels>` walks pointers backwards until
  it reaches a static in .data. `ptrpath.py` goes forwards from statics.
- **Animations**: `ui_record=<class>` in config.txt records a console's children every frame they
  change (`hack\snapshots\rec_N.bin`); `tools/re/uirec.py` lists the changes, or iterate
  `uirec.frames(path)` to get each frame's consoles and cells (`Console.cell(x, y)`).
- **UI objects**: every UI console has a vtable whose RTTI names an `XConsole` subclass.
  `include/cm_ui.h` reads the tree; `replay.exe snap.dmp --ui` prints it.
- **Screen text**: cells are 20 bytes, stored column by column (layout in `include/cogmind.h`, the
  REXPaint conventions behind it in [art.md](art.md)). Searching a snapshot for a line of
  on-screen text with that stride finds the console that drew it.
- `tools/re/minidump.py` reads snapshots from Python (`Dump(path).read(va, n)`, `.find(bytes)`,
  `.msvc_string(va)`).
