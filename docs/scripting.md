# Scripting API (AngelScript)

cog-hack embeds [AngelScript](https://www.angelcode.com/angelscript/) 2.38. Scripts can read the
game's state, send game commands and keys, read the UI and draw labels on it. Everything runs on
the game's main thread, between frames, so a script always sees a consistent game.

## Running scripts

- **Console:** Ctrl+F9 opens it in a separate window. Each line runs as a statement; the trailing
  `;` is optional. `.help` lists the console commands, and `.load file.as` loads a script.
- **autoexec.as:** `<game>\hack\scripts\autoexec.as` loads when the game starts.
- **Loading:** `.load file.as` (or `load("file.as")`) builds the file into the `user` module,
  replacing whatever was loaded before, then runs its `void main()` if there is one. Functions it
  defines can be called from the console. Paths are relative to `hack\scripts\`, and nothing
  outside that folder can be loaded.
- **Includes:** `#include "lib/util.as"` works, with the same folder rule.

```angelscript
// hack\scripts\autoexec.as
void main() {
    print("hello from autoexec");
}

void status() {       // type status() in the console
    print("core " + core() + "/" + max_core() + "  heat " + heat());
}
```

## Events

Define these in the loaded module and the hook calls them:

| function | when |
|---|---|
| `void main()` | once, right after the module loads |
| `void onFrame()` | every frame the game presents |
| `void onCommand(int id, const string &in name)` | before the game runs any command (keys, clicks or `cmd()`) |
| `void onUiHover(const string &in hovered)` | the element the game treats as hovered changed (same text as `ui_hovered()`) |
| `bool onUiClick(int x, int y, int button)` | a mouse button went down at root cell x, y (1 left, 2 middle, 3 right, 4/5 wheel); return true to keep the click from the game |

A `cmd()` call inside `onCommand` does not fire `onCommand` again.

## Limits

- A console line or `main()` that runs longer than `console_timeout_ms` (default 5000) is aborted.
  An event call is aborted after `script_timeout_ms` (default 250). Both are in `hack\config.txt`.
- An event handler that throws or is aborted is turned off until the next `.load`.
- Scripts have no file, network or process access. Only the functions below are available,
  along with AngelScript's string, array, dictionary and math add-ons.
- Changing your own core, energy, matter, corruption or part integrity, or a robot's core
  (`set_*`, `set_item_hp`, `bot_set_core`), needs `dev_mode=1`: those are cheats, not mods.
- Functions that change game memory (`set_*`, `*_def_set`, `bot_set_core`) are refused when the
  game build isn't verified (see "Game updates" in the README). Writes to code or read-only memory
  are refused unless `dev_mode=1`.
- The command that deletes your save (`CMD_GAME_DELETE_QUIT`) is always refused.

Nothing crashes on bad input: functions return 0, `""` or -1 when there is no run, an index is out
of range, or a name is unknown.

## General

| function | |
|---|---|
| `void print(const string &in text)` | print to the console and `hook.log` |
| `void load(const string &in file)` | load a script, like `.load` |
| `string setting(const string &in key)` | a value from `hack\config.txt` (plugins' own keys too), `""` if not set |
| `string game_version()` | the build check result, e.g. `"Beta 17.1.260906a, build 6a9cc02c: known"` |

## Player

| function | |
|---|---|
| `bool in_game()` | true while a run is in progress |
| `int core()`, `int max_core()` | core integrity |
| `int energy()`, `int max_energy()` | energy |
| `int matter()`, `int max_matter()` | matter |
| `int heat()` | current heat |
| `int corruption()` | corruption in percent |
| `int px()`, `int py()` | position on the map |
| `int game_time()` | the HUD's "Time:" |
| `string location()` | depth and map, e.g. `"-7/Factory"` (`"???"` when the HUD hides the name) |
| `bool set_core(int v)` | set core, clamped to 1..max (dev_mode only, like every setter here) |
| `bool set_energy(int v)`, `bool set_matter(int v)` | clamped to 0..max |
| `bool set_corruption(int v)` | clamped to 0..100 |

## Your items

Index `i` runs over your attached parts first, then your inventory, in the game's order.

| function | |
|---|---|
| `uint item_count()` | number of items (attached and in the bag) |
| `string item_name(uint i)` | name, e.g. `"Ion Engine"` |
| `int item_hp(uint i)`, `int item_max_hp(uint i)` | integrity |
| `int item_slot(uint i)` | where it is: a `Slot` value (below), -1 if no item |
| `int item_def_of(uint i)` | its definition index (for `item_def_*`), -1 if unknown |
| `bool set_item_hp(uint i, int v)` | set integrity, clamped to 1..max (dev_mode only) |

`Slot` is an enum: `POWER`, `PROPULSION`, `UTILITY`, `WEAPON`, `INVENTORY`.

```angelscript
for (uint i = 0; i < item_count(); i++)
    if (item_slot(i) != INVENTORY && item_hp(i) * 2 < item_max_hp(i))
        print("damaged: " + item_name(i));
```

## Robots on the level

Index `i` is a slot in the game's entity list (the player included). Empty slots have the name
`""`. This list includes robots you have not seen.

| function | |
|---|---|
| `uint bot_count()` | number of slots |
| `string bot_name(uint i)` | e.g. `"G-47 Trooper"` |
| `int bot_core(uint i)` | core integrity |
| `int bot_x(uint i)`, `int bot_y(uint i)` | position |
| `bool bot_set_core(uint i, int v)` | set core, clamped to 1..its maximum (dev_mode only) |

## Definitions

The game's item and robot tables, by index (item indexes match
[Cog-Minder](https://github.com/noemica/cog-minder)'s). Changing a definition changes every copy of
that item or robot for the rest of the session. The original values are remembered, and
`defs_reset()` puts them back.

| function | |
|---|---|
| `uint item_def_count()`, `uint bot_def_count()` | table sizes |
| `int item_def_find(const string &in name)`, `int bot_def_find(...)` | index by exact name, -1 if none |
| `string item_def_name(uint i)`, `string bot_def_name(uint i)` | name by index |
| `int item_def_get(uint i, const string &in field)` | a numeric field |
| `bool item_def_set(uint i, const string &in field, int v)` | change it |
| `int bot_def_get(...)`, `bool bot_def_set(...)` | the same for robots |
| `string item_fields()`, `string bot_fields()` | the field names, comma separated |
| `int defs_reset()` | undo every change; returns how many values were restored |

Item fields: `size`, `rating`, `mass`, `integrity`, `coverage`, `heat`, `energy_gen`,
`energy_store`, `time_per_move`, `drag`, `support`, `range`, `shot_energy`, `shot_matter`,
`shot_heat`, `delay`, `projectiles`, `damage_min`, `damage_max`, `targeting`, `recoil`.

Robot fields: `tier`, `threat`, `value`, `memory`, `spot`, `size`, `inventory`, `core`,
`energy_store`, `energy_gen`, `matter_store`, `heat_dissipation`, `sight`.

```angelscript
int d = item_def_find("Ion Engine");
item_def_set(d, "energy_gen", item_def_get(d, "energy_gen") * 2);
```

## Commands and keys

Game commands are what the game's key bindings turn into, like `CMD_INVENTORY_EQUIP4`.
`cmd_list("")` lists all 423.

| function | |
|---|---|
| `int cmd(const string &in name)`, `int cmd(int id)` | run a command: 1 handled, 0 not handled, -1 refused or unknown |
| `int cmd_at(const string &in name, int x, int y)` | a command that takes a position |
| `int cmd_find(const string &in name)` | id by name, -1 if none |
| `string cmd_name(int id)` | name by id |
| `array<string>@ cmd_list(const string &in filter = "")` | every command name containing `filter` |
| `bool press(const string &in key)` | send a key press as if typed |

`press` takes a key with optional modifiers: `"a"`, `"shift+3"`, `"ctrl+f5"`, `"enter"`, `"esc"`,
`"space"`, `"tab"`, `"backspace"`, `"up"` / `"down"` / `"left"` / `"right"`, `"home"`, `"end"`,
`"pageup"`, `"pagedown"`, `"insert"`, `"delete"`, `"f1"`..`"f15"`, `"kp0"`..`"kp9"`.
Keys are queued and reach the game on its next input poll.

To find out which commands a UI action uses, turn on `trace(true)` from the bundled `autoexec.as`:
it prints every command the game receives.

## Message logs

| function | |
|---|---|
| `string log(int n = 20)` | the last n lines of the message log, oldest first, one per line |
| `string combat_log(int n = 20)` | the same for the combat log (at most 500) |

## UI

The game's UI is a tree of consoles (`XRoot`, then windows like `CHud`, `CParts`, `CInventory`,
`CLog`, `CMap`, then rows like `CPart` and `CHudData`). Positions are **root cells**: the screen as a
grid of UI font cells (214x45 at 3440x1440). Text is UTF-8; window frames read as box drawing
characters and map tiles as `·`.

| function | |
|---|---|
| `int mouse_x()`, `int mouse_y()` | the cell under the mouse |
| `int mouse_px()`, `int mouse_py()` | the mouse in window pixels |
| `int mouse_map_x()`, `int mouse_map_y()` | the map tile under the mouse, -1 when not over the map |
| `string ui_hovered()` | the element the game treats as hovered, e.g. `"CPart 156,15 56x1: A │ Ion Engine"` |
| `string ui_at(int x, int y)` | the element drawn at a cell, same format |
| `string ui_tree(int depth = 4)` | the visible window tree with bounds and text |
| `string ui_text(const string &in cls)` | all text of the first visible element of a class, e.g. `ui_text("CParts")` |
| `string screen_text(int x = 0, int y = 0, int w = 0, int h = 0)` | the screen as text; w or h 0 = to the edge |
| `void ui_label(const string &in id, int x, int y, const string &in text, uint fg = 0xffc040, uint bg = 0x101010)` | draw text at a cell in the game's font |
| `void ui_label_tile(const string &in id, int x, int y, int font, int glyph, uint tile_fg, const string &in text = "", uint fg = 0xffc040, uint bg = 0xff000000)` | a label that starts with a game tile, such as an item icon |
| `void ui_panel(const string &in id, const string &in text)` | a text panel in the overlay's corner; lines starting with `!` are warnings |
| `bool ui_cell(int x, int y, int &out font, int &out ch, uint &out fg, uint &out bg)` | the game's cell at a root cell: its font, character (tile index in tile fonts) and colours |

Labels stay until changed or removed: call `ui_label` again with the same id to move or change
one, and with `""` as the text to remove it (the same for panels). Ids are up to 16 characters.
Lines split on `\n`. Colors are `0xRRGGBB`; `0xff000000` as `bg` means no background. Labels are
drawn with the overlay, so Pause hides them too. Up to 32 labels are shared by scripts and plugins,
each up to 2 KB of text.

Text is UTF-8. Box drawing characters are drawn with the game's own frame glyphs:
`─ │ ┌ ┐ └ ┘ ├ ┤ ┬ ┴ ┼ ═ ║ ╔ ╗ ╚ ╝ ╠ ╣ ╦ ╩ ╬ ■ ▮`. Other characters outside ASCII show as `?`.

To copy an item's icon into a label, read it off the game's screen with `ui_cell` and pass the
font, character and foreground to `ui_label_tile`.

### Windows and buttons

A window is a multi-line label with a background and a frame; a button is a label plus a check in
`onUiClick`. Everything is drawn over the game, so the game's own UI is never changed.

```angelscript
// a framed window: w x h cells at x, y, with a title and lines of text
void window(const string &in id, int x, int y, int w, int h, const string &in title,
            array<string>@ lines, uint fg = 0x00b200, uint bg = 0x000000) {
    string top = "┌─ " + title + " ";
    for (int i = title.length() + 4; i < w - 1; i++) top += "─";
    string text = top + "┐\n";
    for (int r = 0; r < h - 2; r++) {
        string line = r < int(lines.length()) ? lines[r] : "";
        while (int(line.length()) < w - 2) line += " ";
        text += "│" + line.substr(0, w - 2) + "│\n";
    }
    string bottom = "└";
    for (int i = 1; i < w - 1; i++) bottom += "─";
    ui_label(id, x, y, text + bottom + "┘", fg, bg);
}

void main() {
    window("info", 60, 10, 24, 5, "status", {"core " + core(), "heat " + heat()});
    ui_label("ok", 70, 13, " OK ", 0x000000, 0x00b200);    // a button inside it
}

bool onUiClick(int x, int y, int button) {
    if (y == 13 && x >= 70 && x < 74) {                  // the OK button
        ui_label("info", 0, 0, ""); ui_label("ok", 0, 0, "");
        return true;                                       // the game never sees this click
    }
    return x >= 60 && x < 84 && y >= 10 && y < 15;        // clicks inside the window stay ours
}
```

(The `window` helper counts characters with `length()`, which is bytes in AngelScript; titles and
lines should be ASCII.)

```angelscript
void onFrame() {
    ui_label("pos", 0, 0, "mouse " + mouse_x() + "," + mouse_y());
}
```

## Raw memory (dev_mode only)

| function | |
|---|---|
| `uint peek(uint addr)` | read 32 bits |
| `bool poke(uint addr, uint v)` | write 32 bits |

Both need `dev_mode=1` in `hack\config.txt`. Addresses are in `include/cogmind.h`.

## Functions from plugins

Native plugins can add their own functions. The bundled ones:

| plugin | functions |
|---|---|
| `example.dll` | `int add(int, int)`, `string shout(const string &in)`, `int raw_core()`, `double safe_div(double, double)`, `uint game_addr(const string &in name)` |
| `ui_inspect.dll` | `void ui_inspect(bool on)` |
| `advisor.dll` (cogmind-advisor) | `string advice()`, `void advisor_show(bool)`, `void advisor_panel(bool)` |

A plugin's functions only exist when it is enabled in `hack\plugins.txt`.
