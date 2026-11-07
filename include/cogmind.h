// SPDX-License-Identifier: MIT
// Reverse-engineered Cogmind layout: Beta 17.1 (build 260906a), COGMIND.exe 32-bit, MSVC10.
// Every offset here was verified against live memory snapshots and/or Cog-Minder data.
// Addresses are absolute (image base 0x400000, no ASLR on this exe). A game update WILL move them;
// docs/rederive.md says how to find them again.
#pragma once
#include <stdint.h>

// ---- static globals ----
#define ADDR_WORLD_PTR CM_ADDR(ADDR_WORLD_PTR)        // 0x00cf0d28 World*            (getPlayer @0x463490 reads world+0x66c)
#define ADDR_ITEM_REGISTRY CM_ADDR(ADDR_ITEM_REGISTRY)    // 0x00d3d2fc HandleRegistry*   items (parts, matter, ...)
#define ADDR_ENT_REGISTRY CM_ADDR(ADDR_ENT_REGISTRY)     // 0x00d3d300 HandleRegistry*   entities (robots); FUN_009c1cb0 returns it
#define WORLD_PLAYER_HANDLE 0x66c        // uint32 handle of the player entity
#define WORLD_GAME_TIME     0x314        // i32 game time == HUD "Time:"

// ---- handles: generational IDs, resolved by FUN_009d2860 ----
//   idx = h & 0xffff, gen = h >> 16, 0 = null
//   registry: +0x00 std::vector<void*> ptrs   (begin,end,cap)
//             +0x10 std::vector<uint32> gens  (gens[idx] must == gen)
#define HANDLE_IDX(h) ((h) & 0xffff)
#define HANDLE_GEN(h) ((h) >> 16)

// ---- MSVC10 std::string (28 bytes): 16-byte SSO buffer / heap ptr, size @+16, capacity @+20 ----
#define MSVC_STRING_SIZE 28

// ---- Entity (robot, incl. the player). No vtable. ----
#define ENT_SELF_HANDLE   0x04   // u32
#define ENT_DEF_PTR       0x08   // -> robot definition
#define ENT_NAME          0x0c   // std::string, e.g. "Cogmind", "G-47 Trooper"
#define ENT_POS_VEC       0x30   // std::vector<int> {x, y}: another copy of position
#define ENT_POS_X         0x44   // i32 (verified: one step north = y-1)
#define ENT_POS_Y         0x48   // i32
#define ENT_SLOTS_POWER   0x78   // i32 slot counts (1/4/6/2 matched HUD in two loadouts)
#define ENT_SLOTS_PROP    0x7c
#define ENT_SLOTS_UTIL    0x80
#define ENT_SLOTS_WEAPON  0x84
#define ENT_CORE_COVERAGE 0x88   // i32 (Entity::getExposure @0x5cda60 uses it)
#define ENT_CORE          0x8c   // i32 current core integrity
#define ENT_ENERGY        0x90   // i32 current energy
#define ENT_MATTER        0x94   // i32 current matter
#define ENT_HEAT          0x98   // i32 current heat (matches the HUD; 0 when Cool)
#define ENT_CORRUPTION    0xb0   // i32 % (verified 3% and 12%)
#define ENT_ITEMS_VEC     0x134  // std::vector<uint32 item handle>: equipped parts first, then inventory in slot order

// ---- Item instance ----
#define ITEM_ID           0x00   // u32 unique-ish id
#define ITEM_SELF_HANDLE  0x04
#define ITEM_DEF_PTR      0x08   // -> ItemDef
#define ITEM_LOCATION     0x0c   // u32 enum item_loc (below)
#define ITEM_OWNER_HANDLE 0x10   // entity handle of holder
#define ITEM_INTEGRITY    0x1c   // i32 current integrity (matches HUD damage colors)
#define ITEM_ACTIVE_SINCE 0x28   // i32 game time the part was switched on; -1 = switched off
                                 //   (seen on one disabled part; active parts held recent times)

enum item_loc {                  // verified vs HUD: all 22 player items + 1318 world items
    LOC_POWER = 0, LOC_PROPULSION = 1, LOC_UTILITY = 2, LOC_WEAPON = 3,
    LOC_INVENTORY = 4, LOC_FLOOR = 5, LOC_UNKNOWN7 = 7   // 7: unowned, 10 seen: caches/machines?
};

// ---- ItemDef (offsets matched 100% vs Cog-Minder unless noted) ----
#define IDEF_NAME         0x08   // std::string
#define IDEF_TYPE         0x44   // enum item_type (below); from particles.bin records (ItemDef::read)
#define IDEF_SLOT         0x48   // 0 power 1 propulsion 2 utility 3 weapon (same numbering as item_loc); else none
#define IDEF_SIZE         0x4c
#define IDEF_RATING       0x50
#define IDEF_MASS         0xa4
#define IDEF_INTEGRITY    0xa8   // max integrity
#define IDEF_COVERAGE     0xac   // 180/186 match
#define IDEF_HEAT_GEN     0xb0
#define IDEF_ENERGY_UPKEEP 0xb4  // float per turn (Matter Filter 5.0, Imp. Heat Sink 1.0)
#define IDEF_ENERGY_GEN   0xbc
#define IDEF_ENERGY_STORE 0xc0
#define IDEF_TIME_PER_MOVE 0xcc
#define IDEF_DRAG         0xd4
#define IDEF_ENERGY_PER_MOVE 0xd8  // float (Titanium Leg 4.0)
#define IDEF_HEAT_PER_MOVE 0xdc
#define IDEF_SUPPORT      0xe0
#define IDEF_PENALTY      0xe4
#define IDEF_RANGE        0x100
#define IDEF_SHOT_ENERGY  0x108
#define IDEF_SHOT_MATTER  0x10c
#define IDEF_SHOT_HEAT    0x110
#define IDEF_DELAY        0x114
#define IDEF_PROJECTILES  0x118
#define IDEF_DAMAGE_MIN   0x120
#define IDEF_DAMAGE_MAX   0x124
#define IDEF_SALVAGE      0x12c
#define IDEF_TARGETING    0x130
#define IDEF_RECOIL       0x14c

// IDEF_TYPE values == index into the type-name table at ADDR_ITEMTYPE_NAMES (verified vs Cog-Minder "Type")
enum item_type {
    IT_MATTER, IT_DATA_CORE, IT_SCRAP, IT_PROTOMATTER, IT_ITEM, IT_TRAP,
    IT_ENGINE, IT_POWER_CORE, IT_REACTOR,                                   // power
    IT_TREADS, IT_LEG, IT_WHEEL, IT_HOVER, IT_FLIGHT,                       // propulsion
    IT_DEVICE, IT_STORAGE, IT_PROCESSOR, IT_HACKWARE, IT_PROTECTION, IT_ARTIFACT,   // utility
    IT_ENERGY_GUN, IT_ENERGY_CANNON, IT_BALLISTIC_GUN, IT_BALLISTIC_CANNON, IT_LAUNCHER,
    IT_SPECIAL_WEAPON, IT_IMPACT, IT_SLASHING, IT_PIERCING, IT_SPECIAL_MELEE, IT_COUNT
};

// ---- command dispatch (same path the key-binding code takes in FUN_00427460) ----
//   sink = *(XRoot**)(ADDR_STATE_MGR + 0x6c)        (FUN_004aeeb0)
//   Command c; FN_COMMAND_CTOR(&c, id)              (__thiscall; c.id=id, c.pos=(-10000,-10000))
//   sink->vtable[XROOT_VT_HANDLE/4](sink, &c)       (__thiscall; XRoot::handleCommand @0x42da90, returns 1 if handled)
#define ADDR_STATE_MGR CM_ADDR(ADDR_STATE_MGR)        // 0x00d234d0
#define STATE_MGR_ROOT       0x6c
#define FN_COMMAND_CTOR CM_ADDR(FN_COMMAND_CTOR)       // 0x00415e10
#define FN_COMMAND_CTOR_POS CM_ADDR(FN_COMMAND_CTOR_POS)   // 0x00415e50 (id, x, y) variant used for mouse bindings
#define XROOT_VT_HANDLE      0x10
#define FN_XROOT_HANDLE CM_ADDR(FN_XROOT_HANDLE)       // 0x0042da90 expected vtable target (sanity check)
#define ADDR_CMD_OBSERVER CM_ADDR(ADDR_CMD_OBSERVER)     // 0x00d2360c global fn ptr XRoot calls with every command (if set)
#define ADDR_COMMAND_NAMES CM_ADDR(ADDR_COMMAND_NAMES)    // 0x00cff7f8 std::string[423], index == command id
#define COMMAND_COUNT        423

// ---- definition tables (static std::vector<Def*>) ----
#define ADDR_ITEM_DEFS CM_ADDR(ADDR_ITEM_DEFS)        // 0x00d2e2a4 vector<ItemDef*>, 1359 entries; index == Cog-Minder item Index
#define ADDR_BOT_DEFS CM_ADDR(ADDR_BOT_DEFS)         // 0x00d26ed8 vector<BotDef*>, 337 entries; [0] = Cogmind

// ---- BotDef (verified vs Cog-Minder bots.json, 19 live types; core 246/246 across the table) ----
#define BDEF_NAME            0x04   // std::string
#define BDEF_IS_NPC          0x28   // 0 for Cogmind (max-energy code branches on it)
#define BDEF_TIER            0x68
#define BDEF_THREAT          0x6c
#define BDEF_VALUE           0x70
#define BDEF_MEMORY          0x7c
#define BDEF_SPOT            0x80   // Spot %
#define BDEF_SIZE            0x9c
#define BDEF_INV_CAPACITY    0x1c8
#define BDEF_CORE            0x1dc  // base max core integrity
#define BDEF_ENERGY_STORE    0x1e4  // innate
#define BDEF_ENERGY_GEN      0x1e8  // innate
#define BDEF_MATTER_STORE    0x1ec  // innate
#define BDEF_HEAT_DISSIP     0x1f0  // innate
#define BDEF_SIGHT           0x21c

// ---- player maxima ----
#define ADDR_PLAYER_MAX_CORE CM_ADDR(ADDR_PLAYER_MAX_CORE)  // 0x00cf5a4c global int (HUD CHudData::drawContent reads it)
#define FN_ENT_MAX_ENERGY CM_ADDR(FN_ENT_MAX_ENERGY)     // 0x005cb4f0 int __thiscall Entity::maxEnergy()  (innate + parts + bonus)
#define FN_ENT_MAX_MATTER CM_ADDR(FN_ENT_MAX_MATTER)     // 0x005cb760 int __thiscall Entity::maxMatter()

// ---- UI (verified against a live snapshot at 3440x1440) ----
#define ADDR_SCREEN_SURFACE CM_ADDR(ADDR_SCREEN_SURFACE)   // 0x00cf0b50 SDL_Surface* the game presents (SDL_Flip in FUN_00426d30)
#define ADDR_FONT_MGR CM_ADDR(ADDR_FONT_MGR)         // 0x00d235a0 -> font manager; renderer FUN_0042a710 reads it
#define FONTMGR_FONTS        0x1c         //    std::vector<XFontData*>, indexed by a console's font id
#define XFONT_SURFACE        0x28         // XFontData: SDL_Surface* glyph sheet (white, shape in alpha)
#define XFONT_GLYPH_W        0x2c         //    glyph cell size in pixels
#define XFONT_GLYPH_H        0x30
// font 0 = UI text font: 32 glyphs per row; row 0 = ' '..'?', row 1 = '@' [\]^_` {|}~ + shades,
// row 2 = 'A'..'Z', row 3 = small-caps 'a'..'z'  (see glyph_index() in src/overlay/font.c)

// message logs: std::vector<LogMsg*>, oldest first
#define ADDR_MSG_LOG CM_ADDR(ADDR_MSG_LOG)          // 0x00cf2094 main message log (what the top-left log window shows)
#define ADDR_COMBAT_LOG CM_ADDR(ADDR_COMBAT_LOG)       // 0x00d305e4 combat log (hits, damage, kills), capped ~1000
#define LOGMSG_TEXT          0x04         // std::string, e.g. "05100_ System corrupted: Heat flow error (+198)."
#define LOGMSG_SEQ           0x20         // i32, increases per message (per log)
#define LOGMSG_TURN          0x24         // i32 game time of the message

// ---- current location (HUD "Loc: -7/Factory" = FUN_0087d3a0 @0x8833a2) ----
#define ADDR_CUR_LOCATION CM_ADDR(ADDR_CUR_LOCATION)     // 0x00d1f960 u32 generational handle of the current Location
#define ADDR_LOC_REGISTRY CM_ADDR(ADDR_LOC_REGISTRY)     // 0x00d3d314 HandleRegistry* for locations (FUN_009c22f0)
#define LOC_MAP_TYPE         0x04         // i32 index into ADDR_MAP_NAMES (3 Factory, 1 Scrapyard, 2 Materials)
#define LOC_DEPTH            0x08         // i32 positive depth (7 = -7; the start area is 11)
#define LOC_NAME_SHOWN       0x25         // u8: 0 -> the HUD prints "???" instead of the map name
#define ADDR_MAP_NAMES CM_ADDR(ADDR_MAP_NAMES)        // 0x00cfbd78 std::string[48]: "Sandbox","Scrapyard","Materials","Factory",...
#define MAP_NAME_COUNT       48

// ---- UI tree (REX engine consoles) ----
// Every window/row/button is an XConsole subclass; the class name comes from MSVC RTTI
// (vtable[-1] -> CompleteObjectLocator +0x0c -> TypeDescriptor +0x08 ".?AVCParts@@").
#define ADDR_UI_ROOT CM_ADDR(ADDR_UI_ROOT)          // 0x00d2353c XRoot* (214x45 at 3440x1440): its cells = the composited screen
#define ADDR_DISPLAY CM_ADDR(ADDR_DISPLAY)          // 0x00d234d0 display: +0x14/+0x18 pixel origin of cell (0,0), +0x1c/+0x20 window size
#define DISPLAY_ORIGIN_X     0x14
#define DISPLAY_ORIGIN_Y     0x18
#define ADDR_MOUSE CM_ADDR(ADDR_MOUSE)            // 0x00cf0b64 -> mouse state (SDL_MOUSEMOTION handler FUN_0041a980)
#define MOUSE_CELL_X         0x00         //    i32 cell under the cursor (root grid)
#define MOUSE_CELL_Y         0x04
#define MOUSE_PX             0x10         //    i32 window pixel position
#define MOUSE_PY             0x14
#define MOUSE_HOVER          0x30         //    XConsole* the game considers hovered (FUN_00432330: leave/enter vcalls)
#define CON_PARENT           0x04         // XConsole*
#define CON_W                0x08
#define CON_H                0x0c
#define CON_CELLS            0x10         // cell[w*h], COLUMN-major: cell(x,y) = cells[x*h + y]
#define CON_X                0x1c         // relative to the parent
#define CON_Y                0x20
#define CON_ABS_X            0x24         // absolute, in root cells
#define CON_ABS_Y            0x28
#define CON_CHILDREN         0x44         // std::vector<XConsole*>, draw order (the game hit-tests last first)
#define CON_HIDDEN           0x54         // u8, XConsole::isHidden (FUN_00417740); a hidden parent hides its children
#define CMAP_VIEW_X          0x6c         // CMap: i32, world x = map tile x - this (verified on 2 views)
#define CMAP_VIEW_Y          0x70         //       i32, world y = map tile y - this
#define CELL_SIZE            20
#define CELL_CHAR            0x04         // u32 character code (CP437; >127 = box drawing / map glyphs)
#define CELL_GLYPH           0x08         // u32 index into the font sheet
#define CELL_FG              0x0c         // r,g,b bytes
#define CELL_BG              0x0f         // r,g,b bytes (right after fg; +0x12 is padding)

// ---- current floor: knowledge, floor items, machines ----
#define WORLD_KNOWN_W        0x674        // World: i32 width of the player's map memory (100 on -9)
#define WORLD_KNOWN_H        0x678
#define WORLD_KNOWN          0x67c        //   u8[w*h] column-major ([x*h + y]), 1 = tile known (what CMap draws)
#define ADDR_MAP_W CM_ADDR(ADDR_MAP_W)            // 0x00cfe528 map grid: i32 w, i32 h, Cell** column-major (+0x30 x, +0x34 y,
#define ADDR_MAP_H CM_ADDR(ADDR_MAP_H)            // 0x00cfe52c +0x44 prop handle, +0x48 entity handle, +0x4c vector<item handle>)
#define ADDR_MAP_CELLS CM_ADDR(ADDR_MAP_CELLS)        // 0x00cfe530
#define ITEM_X               0x14         // floor items (ITEM_LOCATION == LOC_FLOOR): map position
#define ITEM_Y               0x18
#define IDEF_ID              0x00         // index into the identified table
#define IDEF_PROTO_KIND      0x94         // 0 Unknown, 1 Prototype, 2 Construct, 3 Alien: hidden name until identified
#define ADDR_ITEM_IDENTIFIED CM_ADDR(ADDR_ITEM_IDENTIFIED)  // 0x00cf5928 std::vector<i32> by IDEF_ID; 0 = not identified (name fn FUN_0055fab0)
#define ADDR_PROP_REGISTRY CM_ADDR(ADDR_PROP_REGISTRY)    // 0x00d3d304 HandleRegistry* props = machine pieces, traps, ... (cell +0x44)
#define PROP_DEF             0x04         // -> PropDef
#define PROP_X               0x08
#define PROP_Y               0x0c
#define PDEF_NAME            0x04         // std::string "Terminal vZn.06a", "Fusion Modulator", "Alarm Trap"
#define PDEF_TYPE            0x20         // std::string "Terminal", "Repair Station", "Machine"

// ---- other tables found in .data (arrays of std::string, stride 28) ----
#define ADDR_ITEMTYPE_NAMES CM_ADDR(ADDR_ITEMTYPE_NAMES)   // 0x00d2a480 "Matter","Data Core","Scrap",... item type enum
#define ADDR_BOTCLASS_NAMES CM_ADDR(ADDR_BOTCLASS_NAMES)   // 0x00d2c5d0 "Worker","Builder","Tunneler",...
#define ADDR_MACHINE_NAMES CM_ADDR(ADDR_MACHINE_NAMES)    // 0x00d3b340 "Terminal","Fabricator",...

// ---------------- the address table ----------------
// Every ADDR_* / FN_* above is a slot in one table, not a constant: the defaults are the Beta 17.1
// (260906a) values; at startup the host re-finds each one with the signatures in sigs/cogmind.sigs and
// stores what the game's code actually uses (src/game/version.c), so a build where globals moved keeps
// working. Plugins copy the host's values with cm_addr_init(api) (include/cm_addr.h);
// offline tools (replay, tests) use the defaults. Keep this list in sync with the defines above
// (tools/re/gen_sigs.py reads it).
#define CM_ADDRESSES(X) \
    X(ADDR_WORLD_PTR, 0x00cf0d28) \
    X(ADDR_ITEM_REGISTRY, 0x00d3d2fc) \
    X(ADDR_ENT_REGISTRY, 0x00d3d300) \
    X(ADDR_STATE_MGR, 0x00d234d0) \
    X(FN_COMMAND_CTOR, 0x00415e10) \
    X(FN_COMMAND_CTOR_POS, 0x00415e50) \
    X(FN_XROOT_HANDLE, 0x0042da90) \
    X(ADDR_CMD_OBSERVER, 0x00d2360c) \
    X(ADDR_COMMAND_NAMES, 0x00cff7f8) \
    X(ADDR_ITEM_DEFS, 0x00d2e2a4) \
    X(ADDR_BOT_DEFS, 0x00d26ed8) \
    X(ADDR_PLAYER_MAX_CORE, 0x00cf5a4c) \
    X(FN_ENT_MAX_ENERGY, 0x005cb4f0) \
    X(FN_ENT_MAX_MATTER, 0x005cb760) \
    X(ADDR_SCREEN_SURFACE, 0x00cf0b50) \
    X(ADDR_FONT_MGR, 0x00d235a0) \
    X(ADDR_MSG_LOG, 0x00cf2094) \
    X(ADDR_COMBAT_LOG, 0x00d305e4) \
    X(ADDR_CUR_LOCATION, 0x00d1f960) \
    X(ADDR_LOC_REGISTRY, 0x00d3d314) \
    X(ADDR_MAP_NAMES, 0x00cfbd78) \
    X(ADDR_UI_ROOT, 0x00d2353c) \
    X(ADDR_DISPLAY, 0x00d234d0) \
    X(ADDR_MOUSE, 0x00cf0b64) \
    X(ADDR_MAP_W, 0x00cfe528) \
    X(ADDR_MAP_H, 0x00cfe52c) \
    X(ADDR_MAP_CELLS, 0x00cfe530) \
    X(ADDR_ITEM_IDENTIFIED, 0x00cf5928) \
    X(ADDR_PROP_REGISTRY, 0x00d3d304) \
    X(ADDR_ITEMTYPE_NAMES, 0x00d2a480) \
    X(ADDR_BOTCLASS_NAMES, 0x00d2c5d0) \
    X(ADDR_MACHINE_NAMES, 0x00d3b340) \

#define CM_X_ENUM(n, v) CM_A_##n,
#define CM_X_VALUE(n, v) v,
#define CM_X_NAME(n, v) #n,
enum cm_addr_id { CM_ADDRESSES(CM_X_ENUM) CM_A_COUNT };
#ifdef CM_HOST
extern uint32_t cm_addr_table[CM_A_COUNT];                 // defined in src/game/version.c
#else
static uint32_t cm_addr_table[CM_A_COUNT] __attribute__((unused)) = { CM_ADDRESSES(CM_X_VALUE) };   // per module
#endif
static const uint32_t cm_addr_defaults[CM_A_COUNT] __attribute__((unused)) = { CM_ADDRESSES(CM_X_VALUE) };
static const char *const cm_addr_names[CM_A_COUNT] __attribute__((unused)) = { CM_ADDRESSES(CM_X_NAME) };
#define CM_ADDR(n) (cm_addr_table[CM_A_##n])
