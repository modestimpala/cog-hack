// SPDX-License-Identifier: MPL-2.0
// hack\config.txt: settings, read once at startup. Same style as the game's own user\advanced.cfg:
//   key=value      one per line; "//" (or "#") starts a comment; booleans 1/0 (also true/false, on/off)
// Unknown keys are kept but ignored. Generated from SETTINGS on first launch; when an older file lacks a
// key that exists now, the file is rewritten in the full layout with the user's values kept, so new
// options show up where they belong.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "base/config.h"
#include "base/log.h"

#define MAX_KEYS 96
static struct { char key[40]; char value[96]; } g_cfg[MAX_KEYS];
static int g_ncfg;

// Every setting, in file order. group != NULL starts a new commented block.
typedef struct { const char *group, *key, *def, *comment; } setting;
static const setting SETTINGS[] = {
    { "Game build", "version_check", "1", "unknown game build: 1 = restrict to what's safe (README \"Game versions\"), 0 = run anyway" },

    { "Overlay (Pause / Ctrl+F12 toggles it)", "overlay", "1", "draw our overlay at start: labels, markers, panels" },
    { NULL, "overlay_stats", "0", "the stats / hotkeys box" },
    { NULL, "overlay_font", "0", "game font for our text (0 = the UI font), -1 = built-in pixel font" },
    { NULL, "overlay_pos", "1", "corner for panels: 0 = top-left, 1 = bottom-left" },

    { "Console (AngelScript)", "console_at_start", "0", "1 = open the console window when the game starts" },
    { NULL, "console_timeout_ms", "5000", "abort a console line / script main() that runs longer than this" },
    { NULL, "script_timeout_ms", "250", "abort an onFrame / onCommand call that runs longer than this" },

    { "Hotkeys (ctrl/shift/alt + f1-f15, a-z, 0-9, pause, insert, home, end, ...; comma = either)",
            "hotkey_overlay", "pause, ctrl+f12", NULL },
    { NULL, "hotkey_console", "ctrl+f9", NULL },
    { NULL, "hotkey_state_dump", "ctrl+f10", "text dump of player, items, bots" },
    { NULL, "hotkey_snapshot", "ctrl+f11", "memory snapshot + .txt screen text + .bmp screen" },
    { NULL, "hotkey_ui_capture", "ctrl+shift+f11", "UI text only (screen, hover, UI tree): instant, never pruned" },
    { NULL, "hotkey_ui_record", "ctrl+shift+f10", "start / stop a UI recording of the ui_record console" },

    { "Snapshots (hack\\snapshots)", "snapshot_keep", "3", "keep the newest N memory snapshots (0 = all); .txt files are never deleted" },
    { NULL, "snapshot_full", "0", "1 = full memory incl. mapped images (much bigger files)" },
    { NULL, "snapshot_bmp", "1", "also save the screen as snap_N.bmp" },
    { NULL, "crash_report", "1", "on a crash: a report in hook.log and hack\\snapshots\\crash_N.dmp" },
    { NULL, "ui_record", "", "record the cells of this console's children every frame they change, e.g. CTitle (rec_N.bin)" },
    { NULL, "ui_record_max_s", "30", "stop a UI recording after this many seconds" },
    { NULL, "ui_record_bmp", "0", "while recording, also save the screen as .bmp every N frames (0 = never)" },

    { "Title screen and boot sequence (cog-hack's mark on them)", "title_art", "1", "1 = once the title logo is lit, it glitches into our COG-HACK (art/coghack.txt)" },
    { NULL, "intro_line", "1", "1 = the new-game boot sequence gets a cog-hack line" },
    { NULL, "intro_after", "PREPARING CONTROL OVERLAY", "the game's line it is typed below" },
    { NULL, "intro_text", "INJECTING COG-HACK", "our line" },
    { NULL, "intro_tag", "ACTIVE", "the status box after it (empty = none)" },
    { NULL, "intro_alert", "0", "1 = red alert box like VIRUS DETECTED instead of the green okay box" },
    { NULL, "intro_process_ms", "600", "how long the process cursor runs before the box" },

    { "Developer", "dev_mode", "0", "1 = peek()/poke(), set_core() and the other stat setters, writes to read-only/code memory" },
};
#define NSET ((int)(sizeof SETTINGS / sizeof *SETTINGS))

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) ++s;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static int find(const char *key) {
    for (int i = 0; i < g_ncfg; ++i) if (!strcmp(g_cfg[i].key, key)) return i;
    return -1;
}

static void put(const char *key, const char *value) {
    int i = find(key);
    if (i < 0) {
        if (g_ncfg >= MAX_KEYS || strlen(key) >= sizeof g_cfg[0].key) return;
        i = g_ncfg++;
        snprintf(g_cfg[i].key, sizeof g_cfg[i].key, "%s", key);
    }
    snprintf(g_cfg[i].value, sizeof g_cfg[i].value, "%s", value);
}

static void line_for(FILE *f, const setting *s, const char *v) {
    if (s->comment) {
        int pad = 30 - (int)strlen(s->key) - (int)strlen(v) - 1;
        fprintf(f, "%s=%s%*s // %s\n", s->key, v, pad > 0 ? pad : 1, "", s->comment);
    } else fprintf(f, "%s=%s\n", s->key, v);
}

static int known_key(const char *k) {
    for (int i = 0; i < NSET; ++i) if (!strcmp(SETTINGS[i].key, k)) return 1;
    return 0;
}

// the whole file in the standard layout, with the current values (user's, else defaults); keys we
// don't know (a plugin's own, say) are kept at the end
static void write_file(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs("// cog-hack settings, read when the game starts. Delete this file to regenerate the defaults.\n"
          "// Native plugins are enabled in plugins.txt (with optional sha256 pins), not here.\n", f);
    for (int i = 0; i < NSET; ++i) {
        if (SETTINGS[i].group) fprintf(f, "\n// %s\n", SETTINGS[i].group);
        int k = find(SETTINGS[i].key);
        line_for(f, &SETTINGS[i], k >= 0 ? g_cfg[k].value : SETTINGS[i].def);
    }
    int other = 0;
    for (int i = 0; i < g_ncfg; ++i)
        if (!known_key(g_cfg[i].key)) {
            if (!other++) fputs("\n// Other settings (plugins' own keys)\n", f);
            fprintf(f, "%s=%s\n", g_cfg[i].key, g_cfg[i].value);
        }
    fclose(f);
}

void config_load(const char *path) {
    FILE *f = fopen(path, "r");
    g_ncfg = 0;
    if (f) {
        char line[512];
        while (fgets(line, sizeof line, f)) {
            char *c = strstr(line, "//");
            if (c) *c = 0;
            if ((c = strchr(line, '#'))) *c = 0;
            char *eq = strchr(line, '=');
            if (!eq) continue;
            *eq = 0;
            char *k = trim(line), *v = trim(eq + 1);
            size_t n = strlen(v);
            if (n >= 2 && v[0] == '"' && v[n - 1] == '"') { v[n - 1] = 0; ++v; }   // "quoted" like options.cfg
            if (*k) put(k, v);
        }
        fclose(f);
    }
    // a key this version knows is missing (new file, or one from an older version): rewrite the file in
    // the full layout, keeping every value already set
    int missing = 0;
    for (int i = 0; i < NSET; ++i) if (find(SETTINGS[i].key) < 0) ++missing;
    if (missing) {
        write_file(path);
        if (g_ncfg) hlog("config: %s updated with %d new setting%s (your values kept)", path, missing, missing == 1 ? "" : "s");
    }
}

const char *cfg_str(const char *key, const char *def) {
    int i = find(key);
    if (i >= 0) return g_cfg[i].value;
    for (int j = 0; j < NSET; ++j) if (!strcmp(SETTINGS[j].key, key)) return SETTINGS[j].def;
    return def;
}

int cfg_int(const char *key, int def) {
    const char *v = cfg_str(key, NULL);
    if (!v || !*v) return def;
    if (!_stricmp(v, "true") || !_stricmp(v, "on") || !_stricmp(v, "yes")) return 1;
    if (!_stricmp(v, "false") || !_stricmp(v, "off") || !_stricmp(v, "no")) return 0;
    char *end;
    long n = strtol(v, &end, 0);
    return end == v ? def : (int)n;
}
