// SPDX-License-Identifier: MPL-2.0
// The game API exposed to AngelScript. Thin wrappers over src/game and src/base.
#include <windows.h>
#include <angelscript.h>
#include "scriptarray/scriptarray.h"
#include <string>
#include <map>
#include <cstring>
#include <cstdlib>
#include "cogmind.h"
#include "cm_ui.h"
#include "base/config.h"
#include "base/log.h"
#include "base/mem.h"
#include "game/commands.h"
#include "game/defs.h"
#include "game/msglog.h"
#include "game/player.h"
#include "game/version.h"
#include "hook/input.h"
#include "overlay/panels.h"
#include "script/script_internal.h"

// dev_mode=1 only: raw memory, and changing your own (or a robot's) core, energy, matter,
// corruption or part integrity directly. Those are cheats, not mods.
static bool dev(const char *what) {
    static int d = -1;
    if (d < 0) d = cfg_int("dev_mode", 0) != 0;
    if (!d) out(std::string(what) + " needs dev_mode=1 in hack\\config.txt");
    return d;
}

// ---- commands ----
static void as_print(const std::string &s) { out(s); }
static int as_cmd_name_id(const std::string &name) {
    int id = command_find(name.c_str());
    if (id < 0) { out("cmd: unknown command '" + name + "' (try cmd_list(\"" + name + "\"))"); return -1; }
    return game_command(id);
}
static int as_cmd_id(int id) { return game_command(id); }
static int as_cmd_at(const std::string &name, int x, int y) {
    int id = command_find(name.c_str());
    if (id < 0) { out("cmd_at: unknown command '" + name + "'"); return -1; }
    return game_command_at(id, x, y);
}
static int as_cmd_find(const std::string &name) { return command_find(name.c_str()); }
static std::string as_cmd_name(int id) { char b[96]; command_name(id, b, sizeof b); return b; }
static CScriptArray *as_cmd_list(const std::string &filter) {
    CScriptArray *arr = CScriptArray::Create(g_engine->GetTypeInfoByDecl("array<string>"));
    char b[96];
    for (int i = 0; i < COMMAND_COUNT; ++i)
        if (command_name(i, b, sizeof b) && (filter.empty() || strstr(b, filter.c_str()))) {
            std::string s = b;
            arr->InsertLast(&s);
        }
    return arr;
}

// ---- player ----
static int pfield(int off) { uint32_t p = get_player(); return p ? rdi(p + off) : 0; }
static bool as_in_game()   { return get_player() != 0; }
static int as_core()       { return pfield(ENT_CORE); }
static int as_energy()     { return pfield(ENT_ENERGY); }
static int as_matter()     { return pfield(ENT_MATTER); }
static int as_corruption() { return pfield(ENT_CORRUPTION); }
static int as_px()         { return pfield(ENT_POS_X); }
static int as_py()         { return pfield(ENT_POS_Y); }
static int as_max_core()   { return player_max_core(); }
static int as_max_energy() { return player_max_energy(); }
static int as_max_matter() { return player_max_matter(); }
static bool as_set_core(int v)       { return dev("set_core") && set_player_core(v); }
static bool as_set_energy(int v)     { return dev("set_energy") && set_player_energy(v); }
static bool as_set_matter(int v)     { return dev("set_matter") && set_player_matter(v); }
static bool as_set_corruption(int v) { return dev("set_corruption") && set_player_corruption(v); }

// ---- player items ----
static uint32_t as_item_count() { player_info pi; return read_player(&pi) ? pi.n_items : 0; }
static std::string as_item_name(uint32_t i) {
    uint32_t it = player_item(i);
    char b[96] = "";
    if (it) rd_string(rd32(it + ITEM_DEF_PTR) + IDEF_NAME, b, sizeof b);
    return b;
}
static int as_item_hp(uint32_t i)     { uint32_t it = player_item(i); return it ? rdi(it + ITEM_INTEGRITY) : 0; }
static int as_item_max_hp(uint32_t i) { uint32_t it = player_item(i); return it ? rdi(rd32(it + ITEM_DEF_PTR) + IDEF_INTEGRITY) : 0; }
static int as_item_slot(uint32_t i)   { uint32_t it = player_item(i); return it ? (int)rd32(it + ITEM_LOCATION) : -1; }
static bool as_set_item_hp(uint32_t i, int v) { return dev("set_item_hp") && set_player_item_hp(i, v); }
static int as_item_def_of(uint32_t i) { uint32_t it = player_item(i); return it ? item_def_index(rd32(it + ITEM_DEF_PTR)) : -1; }

// ---- bots on the level ----
// entity in slot i, or 0. A live entity holds its own handle (index == slot), so a stale or
// garbage slot can't turn bot_set_core into a write to arbitrary memory.
static uint32_t bot(uint32_t i) {
    uint32_t e = registry_slot(ADDR_ENT_REGISTRY, i);
    return e && HANDLE_IDX(rd32(e + ENT_SELF_HANDLE)) == i ? e : 0;
}
static uint32_t as_bot_count() { return registry_count(ADDR_ENT_REGISTRY); }
static std::string as_bot_name(uint32_t i) { uint32_t e = bot(i); char b[96] = ""; if (e) rd_string(e + ENT_NAME, b, sizeof b); return b; }
static int as_bot_core(uint32_t i) { uint32_t e = bot(i); return e ? rdi(e + ENT_CORE) : 0; }
static int as_bot_x(uint32_t i)    { uint32_t e = bot(i); return e ? rdi(e + ENT_POS_X) : 0; }
static int as_bot_y(uint32_t i)    { uint32_t e = bot(i); return e ? rdi(e + ENT_POS_Y) : 0; }
static bool as_bot_set_core(uint32_t i, int v) {
    if (!dev("bot_set_core")) return false;
    uint32_t e = bot(i);
    int m = e ? rdi(rd32(e + ENT_DEF_PTR) + BDEF_CORE) : 0;
    return m > 0 && wr32(e + ENT_CORE, (uint32_t)(v < 1 ? 1 : v > m ? m : v));
}

// ---- input ----
// press("a"), press("shift+3"), press("enter"), press("f5"): synthetic key down+up
static int key_sym(const std::string &k) {
    static const struct { const char *n; int sym; } named[] = {
        {"enter",13},{"return",13},{"esc",27},{"escape",27},{"space",32},{"tab",9},{"backspace",8},
        {"up",273},{"down",274},{"right",275},{"left",276},{"insert",277},{"home",278},{"end",279},
        {"pageup",280},{"pagedown",281},{"delete",127},
        {"kp0",256},{"kp1",257},{"kp2",258},{"kp3",259},{"kp4",260},{"kp5",261},{"kp6",262},{"kp7",263},{"kp8",264},{"kp9",265},
    };
    if (k.size() == 1) { char c = k[0]; return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
    for (auto &n : named) if (k == n.n) return n.sym;
    if (k.size() >= 2 && k[0] == 'f') { int n = atoi(k.c_str() + 1); if (n >= 1 && n <= 15) return 281 + n; }
    return -1;
}
static bool as_press(const std::string &spec) {
    std::string k = spec;
    int mod = 0;
    for (;;) {
        size_t plus = k.find('+');
        if (plus == std::string::npos || plus == k.size() - 1) break;
        std::string m = k.substr(0, plus);
        k = k.substr(plus + 1);
        if (m == "shift") mod |= 0x0001; else if (m == "ctrl") mod |= 0x0040; else if (m == "alt") mod |= 0x0100;
        else { out("press: unknown modifier '" + m + "'"); return false; }
    }
    int sym = key_sym(k);
    if (sym < 0) { out("press: unknown key '" + k + "'"); return false; }
    inject_key(sym, mod | 0x1000);   // 0x1000 = NumLock, as real events from this machine carry it
    return true;
}

// ---- raw memory (dev_mode only) ----
static uint32_t as_peek(uint32_t a)             { return dev("peek") ? rd32(a) : 0; }
static bool     as_poke(uint32_t a, uint32_t v) { return dev("poke") && wr32(a, v); }

// ---- definitions (content patching); writes remember the original value -> defs_reset() ----
static std::map<uint32_t, int32_t> g_def_orig;
static int def_get(uint32_t d, int off) { return d && off >= 0 ? rdi(d + off) : 0; }
static bool def_set(uint32_t d, int off, int v) {
    if (!d || off < 0) return false;
    uint32_t a = d + off;
    if (!g_def_orig.count(a)) g_def_orig[a] = rdi(a);
    return wr32(a, (uint32_t)v);
}
static int field_or_complain(int off, const std::string &f, const char *list) {
    if (off < 0) out("unknown field '" + f + "'. fields: " + list);
    return off;
}
static uint32_t    as_item_def_count()                    { return item_def_count(); }
static int         as_item_def_find(const std::string &n)  { return item_def_find(n.c_str()); }
static std::string as_item_def_name(uint32_t i)           { char b[96] = ""; rd_string(item_def(i) + IDEF_NAME, b, sizeof b); return b; }
static int  as_item_def_get(uint32_t i, const std::string &f) { return def_get(item_def(i), field_or_complain(item_field_offset(f.c_str()), f, item_field_names())); }
static bool as_item_def_set(uint32_t i, const std::string &f, int v) { return def_set(item_def(i), field_or_complain(item_field_offset(f.c_str()), f, item_field_names()), v); }
static uint32_t    as_bot_def_count()                     { return bot_def_count(); }
static int         as_bot_def_find(const std::string &n)   { return bot_def_find(n.c_str()); }
static std::string as_bot_def_name(uint32_t i)            { char b[96] = ""; rd_string(bot_def(i) + BDEF_NAME, b, sizeof b); return b; }
static int  as_bot_def_get(uint32_t i, const std::string &f) { return def_get(bot_def(i), field_or_complain(bot_field_offset(f.c_str()), f, bot_field_names())); }
static bool as_bot_def_set(uint32_t i, const std::string &f, int v) { return def_set(bot_def(i), field_or_complain(bot_field_offset(f.c_str()), f, bot_field_names()), v); }
static std::string as_item_fields() { return item_field_names(); }
static std::string last_lines(uint32_t log, int n) {   // newest n messages, oldest first, '\n'-joined
    int total = log_count(log);
    if (n < 1) n = 1;
    if (n > 500) n = 500;
    std::string r;
    char b[256];
    for (int i = total > n ? total - n : 0; i < total; ++i)
        if (log_message(log, i, b, sizeof b, NULL)) { if (!r.empty()) r += '\n'; r += b; }
    return r;
}
static std::string as_log(int n)        { return last_lines(ADDR_MSG_LOG, n); }
static std::string as_location()        { char b[48] = ""; location_text(b, sizeof b); return b; }
static std::string as_combat_log(int n) { return last_lines(ADDR_COMBAT_LOG, n); }
static std::string as_bot_fields()  { return bot_field_names(); }
static int as_defs_reset() {
    int n = 0;
    for (auto &kv : g_def_orig) n += wr32(kv.first, (uint32_t)kv.second);
    g_def_orig.clear();
    return n;
}

// ---- UI (cm_ui.h over the guarded reader) ----
static cm_mouse mouse_now() { cm_mouse m; cm_ui_mouse(rd, &m); return m; }
static int as_mouse_x()  { return mouse_now().cx; }
static int as_mouse_y()  { return mouse_now().cy; }
static int as_mouse_px() { return mouse_now().px; }
static int as_mouse_py() { return mouse_now().py; }
static int mouse_map(int axis) {
    cm_mouse m = mouse_now();
    int x, y;
    return cm_ui_map_tile(rd, m.cx, m.cy, &x, &y) ? (axis ? y : x) : -1;
}
static int as_mouse_map_x() { return mouse_map(0); }
static int as_mouse_map_y() { return mouse_map(1); }
static std::string describe(uint32_t a) { char b[320]; cm_ui_describe(rd, a, b, sizeof b); return b; }
static std::string as_ui_hovered() { return describe(mouse_now().hovered); }
static std::string as_ui_at(int x, int y) { return describe(cm_ui_at(rd, x, y)); }
static std::string as_screen_text(int x, int y, int w, int h) {
    static char b[96 * 1024];
    cm_screen_text(rd, x, y, w, h, b, sizeof b);
    return b;
}
static void tree_lines(uint32_t a, int depth, int max_depth, std::string &out) {
    cm_ui_node n;
    if (depth > max_depth || !cm_ui_node_read(rd, a, &n) || n.hidden) return;
    char line[400], t[256] = "";
    cm_ui_row_text(rd, &n, 0, t, sizeof t);
    const char *tt = t;
    while (*tt == ' ') ++tt;
    snprintf(line, sizeof line, "%*s%s %d,%d %dx%d%s%.100s\n", depth * 2, "", n.cls, n.ax, n.ay, n.rw, n.rh, *tt ? "  " : "", tt);
    out += line;
    for (uint32_t i = 0; i < n.nkids; ++i) tree_lines(cm_ui_child(rd, &n, i), depth + 1, max_depth, out);
}
static std::string as_ui_tree(int max_depth) { std::string r; tree_lines(cm_ui_root(rd), 0, max_depth, r); return r; }
static std::string as_ui_text(const std::string &cls) {
    cm_ui_node n;
    static char b[64 * 1024];
    if (!cm_ui_node_read(rd, cm_ui_find(rd, cm_ui_root(rd), cls.c_str(), 1), &n)) return "";
    cm_ui_subtree_text(rd, &n, b, sizeof b);   // its cells with its children's drawn over them
    return b;
}
static void as_ui_label(const std::string &id, int x, int y, const std::string &text, uint32_t fg, uint32_t bg) {
    label_set(("script:" + id).c_str(), x, y, text.c_str(), fg, bg);
}

static void as_ui_label_tile(const std::string &id, int x, int y, int font, int glyph, uint32_t tile_fg,
                             const std::string &text, uint32_t fg, uint32_t bg) {
    label_set_tile(("script:" + id).c_str(), x, y, font, glyph, tile_fg, text.c_str(), fg, bg);
}
static void as_ui_panel(const std::string &id, const std::string &text) { panel_set(("script:" + id).c_str(), text.c_str()); }
// the cell the game draws at root cell x, y: in the deepest visible console there, in its own font
static bool as_ui_cell(int x, int y, int &font, int &ch, uint32_t &fg, uint32_t &bg) {
    cm_ui_node n;
    cm_cell c;
    font = ch = 0; fg = bg = 0;
    if (!cm_ui_node_read(rd, cm_ui_at(rd, x, y), &n) || !n.rw || !n.rh) return false;
    if (!cm_ui_cell(rd, &n, (x - n.ax) * n.w / n.rw, (y - n.ay) * n.h / n.rh, &c)) return false;
    font = n.font; ch = (int)c.ch;
    fg = (uint32_t)c.fg[0] << 16 | c.fg[1] << 8 | c.fg[2];
    bg = (uint32_t)c.bg[0] << 16 | c.bg[1] << 8 | c.bg[2];
    return true;
}

static std::string as_game_version() { return version_text(); }
static std::string as_setting(const std::string &k) { const char *v = cfg_str(k.c_str(), NULL); return v ? v : ""; }
static void as_load(const std::string &f) { load_script(f); }

void register_api(asIScriptEngine *e) {
    struct { const char *decl; asSFuncPtr fn; } api[] = {
        {"void print(const string &in)", asFUNCTION(as_print)},
        {"void load(const string &in)", asFUNCTION(as_load)},
        // commands / input
        {"int cmd(const string &in)", asFUNCTION(as_cmd_name_id)},
        {"int cmd(int)", asFUNCTION(as_cmd_id)},
        {"int cmd_at(const string &in, int, int)", asFUNCTION(as_cmd_at)},
        {"int cmd_find(const string &in)", asFUNCTION(as_cmd_find)},
        {"string cmd_name(int)", asFUNCTION(as_cmd_name)},
        {"array<string>@ cmd_list(const string &in filter = \"\")", asFUNCTION(as_cmd_list)},
        {"bool press(const string &in)", asFUNCTION(as_press)},
        // player
        {"bool in_game()", asFUNCTION(as_in_game)},
        {"int core()", asFUNCTION(as_core)},
        {"int energy()", asFUNCTION(as_energy)},
        {"int matter()", asFUNCTION(as_matter)},
        {"int corruption()", asFUNCTION(as_corruption)},
        {"int px()", asFUNCTION(as_px)},
        {"int py()", asFUNCTION(as_py)},
        {"int max_core()", asFUNCTION(as_max_core)},
        {"int max_energy()", asFUNCTION(as_max_energy)},
        {"int max_matter()", asFUNCTION(as_max_matter)},
        {"bool set_core(int)", asFUNCTION(as_set_core)},
        {"bool set_energy(int)", asFUNCTION(as_set_energy)},
        {"bool set_matter(int)", asFUNCTION(as_set_matter)},
        {"bool set_corruption(int)", asFUNCTION(as_set_corruption)},
        // items
        {"uint item_count()", asFUNCTION(as_item_count)},
        {"string log(int n = 20)", asFUNCTION(as_log)},
        {"int heat()", asFUNCTION(player_heat)},
        {"int game_time()", asFUNCTION(game_time)},
        {"string location()", asFUNCTION(as_location)},
        {"string game_version()", asFUNCTION(as_game_version)},
        {"string setting(const string &in)", asFUNCTION(as_setting)},
        {"string combat_log(int n = 20)", asFUNCTION(as_combat_log)},
        {"string item_name(uint)", asFUNCTION(as_item_name)},
        {"int item_hp(uint)", asFUNCTION(as_item_hp)},
        {"int item_max_hp(uint)", asFUNCTION(as_item_max_hp)},
        {"int item_slot(uint)", asFUNCTION(as_item_slot)},
        {"int item_def_of(uint)", asFUNCTION(as_item_def_of)},
        {"bool set_item_hp(uint, int)", asFUNCTION(as_set_item_hp)},
        // bots
        {"uint bot_count()", asFUNCTION(as_bot_count)},
        {"string bot_name(uint)", asFUNCTION(as_bot_name)},
        {"int bot_core(uint)", asFUNCTION(as_bot_core)},
        {"int bot_x(uint)", asFUNCTION(as_bot_x)},
        {"int bot_y(uint)", asFUNCTION(as_bot_y)},
        {"bool bot_set_core(uint, int)", asFUNCTION(as_bot_set_core)},
        // definitions
        {"uint item_def_count()", asFUNCTION(as_item_def_count)},
        {"int item_def_find(const string &in)", asFUNCTION(as_item_def_find)},
        {"string item_def_name(uint)", asFUNCTION(as_item_def_name)},
        {"int item_def_get(uint, const string &in)", asFUNCTION(as_item_def_get)},
        {"bool item_def_set(uint, const string &in, int)", asFUNCTION(as_item_def_set)},
        {"uint bot_def_count()", asFUNCTION(as_bot_def_count)},
        {"int bot_def_find(const string &in)", asFUNCTION(as_bot_def_find)},
        {"string bot_def_name(uint)", asFUNCTION(as_bot_def_name)},
        {"int bot_def_get(uint, const string &in)", asFUNCTION(as_bot_def_get)},
        {"bool bot_def_set(uint, const string &in, int)", asFUNCTION(as_bot_def_set)},
        {"string item_fields()", asFUNCTION(as_item_fields)},
        {"string bot_fields()", asFUNCTION(as_bot_fields)},
        {"int defs_reset()", asFUNCTION(as_defs_reset)},
        // UI: positions are root cells (cog-hack README "UI"); text is UTF-8
        {"int mouse_x()", asFUNCTION(as_mouse_x)},
        {"int mouse_y()", asFUNCTION(as_mouse_y)},
        {"int mouse_px()", asFUNCTION(as_mouse_px)},
        {"int mouse_py()", asFUNCTION(as_mouse_py)},
        {"int mouse_map_x()", asFUNCTION(as_mouse_map_x)},   // world map tile under the cursor, -1 off the map
        {"int mouse_map_y()", asFUNCTION(as_mouse_map_y)},
        {"string ui_hovered()", asFUNCTION(as_ui_hovered)},
        {"string ui_at(int, int)", asFUNCTION(as_ui_at)},
        {"string ui_tree(int depth = 4)", asFUNCTION(as_ui_tree)},
        {"string ui_text(const string &in)", asFUNCTION(as_ui_text)},
        {"string screen_text(int x = 0, int y = 0, int w = 0, int h = 0)", asFUNCTION(as_screen_text)},
        {"void ui_label(const string &in, int, int, const string &in, uint fg = 0xffc040, uint bg = 0x101010)", asFUNCTION(as_ui_label)},
        {"void ui_label_tile(const string &in, int, int, int, int, uint, const string &in text = \"\", uint fg = 0xffc040, uint bg = 0xff000000)", asFUNCTION(as_ui_label_tile)},
        {"void ui_panel(const string &in, const string &in)", asFUNCTION(as_ui_panel)},
        {"bool ui_cell(int, int, int &out, int &out, uint &out, uint &out)", asFUNCTION(as_ui_cell)},
        // raw memory (dev_mode)
        {"uint peek(uint)", asFUNCTION(as_peek)},
        {"bool poke(uint, uint)", asFUNCTION(as_poke)},
    };
    for (auto &f : api)
        if (e->RegisterGlobalFunction(f.decl, f.fn, asCALL_CDECL) < 0) hlog("register failed: %s", f.decl);

    e->RegisterEnum("Slot");
    e->RegisterEnumValue("Slot", "POWER", LOC_POWER);
    e->RegisterEnumValue("Slot", "PROPULSION", LOC_PROPULSION);
    e->RegisterEnumValue("Slot", "UTILITY", LOC_UTILITY);
    e->RegisterEnumValue("Slot", "WEAPON", LOC_WEAPON);
    e->RegisterEnumValue("Slot", "INVENTORY", LOC_INVENTORY);
}
