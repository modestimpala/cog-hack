// SPDX-License-Identifier: MPL-2.0
// Native plugins: hack\plugins\*.dll, opt-in via hack\plugins.txt (see include/plugin_api.h).
//
// A native plugin runs inside the game with the user's rights, so it has full access to their
// system: nothing in-process can sandbox it. What this loader *can* do is make loading deliberate:
// a DLL only loads if hack\plugins.txt names it, optionally pinned to a SHA-256 so a swapped or
// updated file doesn't load silently.
#include <windows.h>
#include <bcrypt.h>
#include <angelscript.h>
#include <string>
#include <vector>
#include <new>
#include <cstdio>
#include <cstring>
#include "cogmind.h"
#include "plugin_api.h"
#include "base/config.h"
#include "base/mem.h"
#include "game/commands.h"
#include "game/defs.h"
#include "game/player.h"
#include "game/version.h"
#include "hook/input.h"
#include "hook/ui_events.h"
#include "overlay/panels.h"
#include "script/plugin_host.h"
#include "script/script_internal.h"

static const char *PLUGIN_DIR = "hack\\plugins\\";
static const char *ALLOWLIST = "hack\\plugins.txt";

// ---------------- host API given to plugins ----------------
struct cm_call { asIScriptGeneric *g; };

static void __cdecl p_log(const char *m) { out(std::string("[plugin] ") + (m ? m : "")); }
static void plugin_trampoline(asIScriptGeneric *g) {
    cm_call c = { g };
    ((cm_fn)g->GetAuxiliary())(&c);
}
static int __cdecl p_register(const char *decl, cm_fn fn) {
    if (!decl || !fn) return -1;
    // the plugin's function pointer rides along as the generic call's auxiliary
    int r = g_engine->RegisterGlobalFunction(decl, asFUNCTION(plugin_trampoline), asCALL_GENERIC, (void *)fn);
    if (r < 0) out(std::string("plugin register failed: ") + decl);
    return r;
}
static int    __cdecl p_arg_int(cm_call *c, int i)    { return (int)c->g->GetArgDWord(i); }
static double __cdecl p_arg_double(cm_call *c, int i) { return c->g->GetArgDouble(i); }
static float  __cdecl p_arg_float(cm_call *c, int i)  { return c->g->GetArgFloat(i); }
static int    __cdecl p_arg_bool(cm_call *c, int i)   { return c->g->GetArgByte(i) != 0; }
static const char *__cdecl p_arg_string(cm_call *c, int i) {
    std::string *s = (std::string *)c->g->GetArgObject(i);   // `const string &in` -> object ref
    return s ? s->c_str() : "";
}
static void __cdecl p_ret_int(cm_call *c, int v)       { c->g->SetReturnDWord((asDWORD)v); }
static void __cdecl p_ret_double(cm_call *c, double v) { c->g->SetReturnDouble(v); }
static void __cdecl p_ret_float(cm_call *c, float v)   { c->g->SetReturnFloat(v); }
static void __cdecl p_ret_bool(cm_call *c, int v)      { c->g->SetReturnByte(v ? 1 : 0); }
static void __cdecl p_ret_string(cm_call *c, const char *s) {
    new (c->g->GetAddressOfReturnLocation()) std::string(s ? s : "");   // string returned by value
}
static void __cdecl p_set_exception(cm_call *, const char *m) {
    asIScriptContext *ctx = asGetActiveContext();
    if (ctx) ctx->SetException(m ? m : "plugin error");
}
static int      __cdecl p_read_mem(uint32_t a, void *o, size_t n)        { return rd(a, o, n); }
static int      __cdecl p_read_string(uint32_t a, char *o, size_t n)     { return rd_string(a, o, n); }
static uint32_t __cdecl p_player(void)                                   { return get_player(); }
static uint32_t __cdecl p_res_ent(uint32_t h)                            { return resolve(ADDR_ENT_REGISTRY, h); }
static uint32_t __cdecl p_res_item(uint32_t h)                           { return resolve(ADDR_ITEM_REGISTRY, h); }
static int      __cdecl p_cmd_find(const char *n)                        { return command_find(n); }
static int      __cdecl p_cmd(int id)                                    { return game_command(id); }
static void     __cdecl p_press(int sym, int mod)                        { inject_key(sym, mod); }
static int      __cdecl p_write_mem(uint32_t a, const void *s, size_t n) { return wr(a, s, n); }
static uint32_t __cdecl p_item_def(uint32_t i)                           { return item_def(i); }
static uint32_t __cdecl p_bot_def(uint32_t i)                            { return bot_def(i); }

struct Tick { cm_tick_fn fn; void *user; };
static std::vector<Tick> g_ticks;
static int  __cdecl p_on_tick(cm_tick_fn fn, void *user) {
    if (!fn) return -1;
    g_ticks.push_back({ fn, user });
    return 0;
}
static void __cdecl p_overlay_set(const char *id, const char *text) { panel_set(id, text); }
static int  __cdecl p_max_core(void)       { return player_max_core(); }
static int  __cdecl p_max_energy(void)     { return player_max_energy(); }
static int  __cdecl p_max_matter(void)     { return player_max_matter(); }
static uint32_t __cdecl p_idef_count(void) { return item_def_count(); }
static uint32_t __cdecl p_bdef_count(void) { return bot_def_count(); }
static int  __cdecl p_on_ui(cm_ui_fn fn, void *user) { return ui_event_register(fn, user); }
static uint32_t __cdecl p_addr(const char *name) {
    for (int i = 0; name && i < CM_A_COUNT; ++i) if (!strcmp(cm_addr_names[i], name)) return cm_addr_table[i];
    return 0;
}
static const char *__cdecl p_game_version(void) { return version_text(); }
static const char *__cdecl p_setting(const char *key, const char *def) { return key ? cfg_str(key, def) : def; }
static void __cdecl p_label_tile(const char *id, int cx, int cy, int font, int glyph, uint32_t tfg, const char *text, uint32_t fg, uint32_t bg) {
    label_set_tile(id, cx, cy, font, glyph, tfg, text, fg, bg);
}
static void __cdecl p_label_set(const char *id, int cx, int cy, const char *text, uint32_t fg, uint32_t bg) {
    label_set(id, cx, cy, text, fg, bg);
}

extern "C" void plugins_tick(void) {
    for (size_t i = 0; i < g_ticks.size(); ++i) g_ticks[i].fn(g_ticks[i].user);
}

static cm_host_api g_api;

static void fill_api(asIScriptEngine *e) {
    g_api.version = CM_API_VERSION;
    g_api.as_engine = e;
    g_api.log = p_log; g_api.register_function = p_register;
    g_api.arg_int = p_arg_int; g_api.arg_double = p_arg_double; g_api.arg_float = p_arg_float;
    g_api.arg_bool = p_arg_bool; g_api.arg_string = p_arg_string;
    g_api.ret_int = p_ret_int; g_api.ret_double = p_ret_double; g_api.ret_float = p_ret_float;
    g_api.ret_bool = p_ret_bool; g_api.ret_string = p_ret_string; g_api.set_exception = p_set_exception;
    g_api.read_mem = p_read_mem; g_api.read_string = p_read_string; g_api.player = p_player;
    g_api.resolve_entity = p_res_ent; g_api.resolve_item = p_res_item;
    g_api.command_find = p_cmd_find; g_api.command = p_cmd; g_api.press_key = p_press;
    g_api.write_mem = p_write_mem; g_api.item_def = p_item_def; g_api.bot_def = p_bot_def;
    g_api.on_tick = p_on_tick; g_api.overlay_set = p_overlay_set;
    g_api.max_core = p_max_core; g_api.max_energy = p_max_energy; g_api.max_matter = p_max_matter;
    g_api.item_def_count = p_idef_count; g_api.bot_def_count = p_bdef_count;
    g_api.on_ui = p_on_ui; g_api.label_set = p_label_set;
    g_api.addr = p_addr; g_api.game_version = p_game_version; g_api.setting = p_setting; g_api.label_tile = p_label_tile;
}

// ---------------- allowlist ----------------
static bool sha256_file(const std::string &path, std::string &hex) {
    HANDLE f = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE h = NULL;
    bool ok = false;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0 &&
        BCryptCreateHash(alg, &h, NULL, 0, NULL, 0, 0) == 0) {
        unsigned char buf[65536];
        DWORD n;
        ok = true;
        while (ReadFile(f, buf, sizeof buf, &n, NULL) && n)
            if (BCryptHashData(h, buf, n, 0) != 0) { ok = false; break; }
        unsigned char digest[32];
        if (ok && BCryptFinishHash(h, digest, sizeof digest, 0) == 0) {
            char b[65];
            for (int i = 0; i < 32; ++i) snprintf(b + i * 2, 3, "%02x", digest[i]);
            hex = b;
        } else ok = false;
    }
    if (h) BCryptDestroyHash(h);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(f);
    return ok;
}

struct Allowed { std::string name, sha; };

static std::vector<Allowed> read_allowlist() {
    std::vector<Allowed> v;
    FILE *f = fopen(ALLOWLIST, "r");
    if (!f) {
        f = fopen(ALLOWLIST, "w");
        if (f) {
            fputs("# Native plugins to load from hack\\plugins\\, one per line:   name.dll [sha256]\n"
                  "# A plugin has full access to your system (it runs with your rights): only list ones you trust.\n"
                  "# With a sha256, the plugin is refused if the file changes. hook.log prints each\n"
                  "# DLL's hash so you can pin it.\n"
                  "#\n"
                  "# example.dll       sample plugin (script functions add, shout, safe_div)\n"
                  "# ui_inspect.dll    hover inspector: class + bounds label next to the hovered UI element\n", f);
            fclose(f);
        }
        return v;
    }
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        char name[MAX_PATH] = "", sha[80] = "";
        if (sscanf(line, "%259s %79s", name, sha) >= 1 && name[0]) {
            for (char *p = sha; *p; ++p) if (*p >= 'A' && *p <= 'F') *p += 32;
            v.push_back({ name, sha });
        }
    }
    fclose(f);
    return v;
}

void load_plugins(asIScriptEngine *e) {
    fill_api(e);
    if (version_safe_mode()) { out(std::string("native plugins not loaded: unsupported game build (") + version_text() + ")"); return; }
    CreateDirectoryA("hack\\plugins", NULL);
    std::vector<Allowed> allow = read_allowlist();

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA("hack\\plugins\\*.dll", &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::string name = fd.cFileName, path = std::string(PLUGIN_DIR) + name, sha;
        if (!sha256_file(path, sha)) { out("plugin " + name + ": cannot hash, skipped"); continue; }
        const Allowed *a = NULL;
        for (auto &x : allow) if (!_stricmp(x.name.c_str(), name.c_str())) a = &x;
        if (!a) { out("plugin " + name + " not enabled (add to hack\\plugins.txt): sha256 " + sha); continue; }
        if (!a->sha.empty() && a->sha != sha) { out("plugin " + name + " REFUSED: sha256 " + sha + " != pinned " + a->sha); continue; }

        char full[MAX_PATH];
        GetFullPathNameA(path.c_str(), sizeof full, full, NULL);
        // altered search path: the plugin's own dependencies resolve from hack\plugins\ first
        HMODULE m = LoadLibraryExA(full, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!m) { char b[32]; snprintf(b, sizeof b, " (error %lu)", GetLastError()); out("plugin load failed: " + name + b); continue; }
        cm_plugin_init_fn init = (cm_plugin_init_fn)GetProcAddress(m, "cogmind_plugin_init");
        if (!init) { out("plugin " + name + ": no cogmind_plugin_init export, unloading"); FreeLibrary(m); continue; }
        int r = init(&g_api);
        char b[48];
        snprintf(b, sizeof b, " -> %d%s", r, a->sha.empty() ? " (unpinned)" : " (pinned)");
        out("plugin " + name + b + " sha256 " + sha);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}
