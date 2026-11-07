/* SPDX-License-Identifier: MIT */
/* cog-hack native plugin API: plain C, compiler-independent (MSVC or mingw, 32-bit).
 *
 * A plugin is a 32-bit DLL in <game>\hack\plugins\ that exports:
 *
 *     __declspec(dllexport) int __cdecl cogmind_plugin_init(const cm_host_api *api);
 *
 * Return 0 on success. Keep `api`: it stays valid for the life of the process.
 * Everything here runs on the game's main thread (plugins are loaded there, script calls into plugin
 * functions and on_tick callbacks happen there). Don't call ANY api function from your own threads:
 * hand work to an on_tick callback instead (cogmind-mcp's bridge does this).
 *
 * Registering script functions:
 *     api->register_function("int add(int, int)", my_add);
 *     static void my_add(cm_call *c) { api->ret_int(c, api->arg_int(c, 0) + api->arg_int(c, 1)); }
 * Supported argument/return types through the accessors: int/uint, double/float, bool, string
 * (declare string args as `const string &in`; returned strings are copied).
 *
 * Plugins built with the SAME compiler + AngelScript 2.38.0 may also use `as_engine`
 * (asIScriptEngine*) directly for anything fancier (classes, callbacks). That is not ABI-stable.
 */
#ifndef COGMIND_PLUGIN_API_H
#define COGMIND_PLUGIN_API_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* The host fills cm_host_api with version = CM_API_VERSION. Functions are only ever added at the end
 * of the struct, and each addition raises the version, so a plugin checks api->version against the
 * version it needs and can use everything up to that. */
#define CM_API_VERSION 1

typedef struct cm_call cm_call;                 /* opaque: one script->plugin call */
typedef void (__cdecl *cm_fn)(cm_call *call);
typedef void (__cdecl *cm_tick_fn)(void *user);

/* UI events. Positions: px/py window pixels, cx/cy root UI cells (see cm_ui.h). */
enum { CM_UI_HOVER = 1, CM_UI_MOUSE_DOWN = 2, CM_UI_MOUSE_UP = 3 };
typedef struct cm_ui_event {
    int type;
    int px, py, cx, cy;
    int button;          /* mouse events: SDL button, 1 left, 2 middle, 3 right, 4/5 wheel up/down */
    uint32_t node;       /* XConsole*: hover: the one the game now treats as hovered (0 = none);
                            mouse: the deepest visible console under the cursor */
    uint32_t prev;       /* hover: the previously hovered one */
} cm_ui_event;
/* Return 1 from a MOUSE_DOWN to swallow the click (the game never sees it, nor its MOUSE_UP). */
typedef int (__cdecl *cm_ui_fn)(void *user, const cm_ui_event *e);

typedef struct cm_host_api {
    int   version;                              /* CM_API_VERSION the host implements */
    void *as_engine;                            /* asIScriptEngine* (same-compiler plugins only) */

    /* ---- log and script functions ---- */
    void (__cdecl *log)(const char *msg);       /* -> hook.log and the console */
    int  (__cdecl *register_function)(const char *decl, cm_fn fn);   /* <0 on error */
    int         (__cdecl *arg_int)(cm_call *c, int i);
    double      (__cdecl *arg_double)(cm_call *c, int i);
    float       (__cdecl *arg_float)(cm_call *c, int i);
    int         (__cdecl *arg_bool)(cm_call *c, int i);
    const char *(__cdecl *arg_string)(cm_call *c, int i);    /* valid until the call returns */
    void (__cdecl *ret_int)(cm_call *c, int v);
    void (__cdecl *ret_double)(cm_call *c, double v);
    void (__cdecl *ret_float)(cm_call *c, float v);
    void (__cdecl *ret_bool)(cm_call *c, int v);
    void (__cdecl *ret_string)(cm_call *c, const char *s);
    void (__cdecl *set_exception)(cm_call *c, const char *msg);   /* raise a script exception */

    /* ---- game memory (the same guarded functions the script API uses) ---- */
    int      (__cdecl *read_mem)(uint32_t addr, void *out, size_t n);         /* 1 ok, 0 bad address */
    int      (__cdecl *read_string)(uint32_t addr, char *out, size_t cap);     /* MSVC std::string */
    int      (__cdecl *write_mem)(uint32_t addr, const void *src, size_t n);  /* 1 ok, 0 refused */
    /* A game address by its cogmind.h name ("ADDR_WORLD_PTR", "FN_ENT_MAX_ENERGY"), as the host
     * resolved it for the running build (signature scan at startup); 0 if unknown. Plugins using
     * cogmind.h addresses call cm_addr_init(api) (include/cm_addr.h) once in their init. */
    uint32_t    (__cdecl *addr)(const char *name);
    const char *(__cdecl *game_version)(void);      /* "Beta 17.1.260906a, build 6a9cc02c: known" */

    /* ---- game state ---- */
    uint32_t (__cdecl *player)(void);                 /* entity address or 0 */
    uint32_t (__cdecl *resolve_entity)(uint32_t handle);
    uint32_t (__cdecl *resolve_item)(uint32_t handle);
    int      (__cdecl *max_core)(void);               /* 0 when not in a run */
    int      (__cdecl *max_energy)(void);
    int      (__cdecl *max_matter)(void);
    uint32_t (__cdecl *item_def)(uint32_t index);     /* ItemDef* by Cog-Minder index, or 0 */
    uint32_t (__cdecl *bot_def)(uint32_t index);      /* BotDef* (0 = Cogmind), or 0 */
    uint32_t (__cdecl *item_def_count)(void);
    uint32_t (__cdecl *bot_def_count)(void);

    /* ---- commands and input ---- */
    int  (__cdecl *command_find)(const char *name);
    int  (__cdecl *command)(int id);                  /* 1 handled, 0 not, -1 refused */
    void (__cdecl *press_key)(int sdl_sym, int sdl_mod);

    /* ---- callbacks (called on the game thread) ---- */
    /* fn(user) at every input poll (many times a second, also in menus). Keep it cheap: throttle
     * real work yourself. <0 on error. */
    int (__cdecl *on_tick)(cm_tick_fn fn, void *user);
    int (__cdecl *on_ui)(cm_ui_fn fn, void *user);    /* hover and click events; <0 on error */

    /* ---- drawing (with the overlay: Pause / Ctrl+F12 hides it all) ---- */
    /* A text panel in the overlay's corner. Lines split on '\n'; a line starting with '!' is drawn as
     * a warning. Text is copied; "" or NULL removes the panel. */
    void (__cdecl *overlay_set)(const char *panel_id, const char *text);
    /* Text in the game's UI font at root cell (cx, cy). UTF-8; box drawing characters (─ │ ┌ ╔ ═ ...)
     * use the game's frame glyphs, so a multi-line label with a background makes a framed window.
     * Lines split on '\n'. fg/bg are 0xRRGGBB; bg CM_LABEL_NO_BG draws text only. "" or NULL text
     * removes the label. Up to 32 labels, 2 KB of text each. */
    void (__cdecl *label_set)(const char *id, int cx, int cy, const char *text, uint32_t fg, uint32_t bg);
    /* Like label_set, but starting with a tile: glyph `glyph` of game font `font` tinted tile_fg (read
     * them off a game cell with cm_ui_cell + its font: e.g. an inventory row's icon is font 2). A tile
     * takes whole root cells (a 32 px tile = 2). text may be "" for just the tile. Remove with label_set. */
    void (__cdecl *label_tile)(const char *id, int cx, int cy, int font, int glyph, uint32_t tile_fg,
                               const char *text, uint32_t fg, uint32_t bg);

    /* ---- settings ---- */
    /* A value from hack\config.txt ("overlay_pos" -> "1"), or def if the key isn't there. Plugins
     * should prefix their keys with their name; unknown keys are fine (the user adds them). */
    const char *(__cdecl *setting)(const char *key, const char *def);
} cm_host_api;
#define CM_LABEL_NO_BG 0xff000000u

typedef int (__cdecl *cm_plugin_init_fn)(const cm_host_api *api);

#ifdef __cplusplus
}
#endif
#endif
