// SPDX-License-Identifier: MPL-2.0
// Game build detection, run once at startup (hook init).
//
// Every address the mod uses is a slot in cm_addr_table (include/cogmind.h) with byte signatures in
// sigs/cogmind.sigs (compiled in; hack\sigs.txt replaces it). Here we:
//   1. fingerprint the running COGMIND.exe (PE timestamp + image size) and read its version strings
//      ("Beta 17.1", build "260906a");
//   2. scan its .text for every signature and read each address back out of the game's own code;
//   3. decide, and fill the table:
//        KNOWN        the build cogmind.h was mapped on
//        COMPATIBLE   another build, every address where we expect
//        ADAPTED      some addresses moved, every one was found again: the table gets the new values,
//                     plugins/reads run; memory writes and game calls stay off (struct layouts unverified)
//        UNSUPPORTED  an address vanished or its sites disagree: safe mode - no native plugins, no
//                     writes, no game calls; reads stay on (guarded) so scripts can look around
//      version_check=0 lifts the ADAPTED / UNSUPPORTED restrictions.
// hook.log lists what moved and where it is now.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "cogmind.h"
#include "base/config.h"
#include "base/log.h"
#include "game/sigs.h"
#include "game/version.h"
#include "overlay/panels.h"

#define SIGS_SIDECAR "hack\\sigs.txt"

uint32_t cm_addr_table[CM_A_COUNT] = { CM_ADDRESSES(CM_X_VALUE) };   // the host's (shared) address table

static int g_mode = VERSION_NOT_CHECKED;
static char g_text[160] = "not checked";

int version_mode(void) { return g_mode; }
const char *version_text(void) { return g_text; }
static int enforce(void) { return cfg_int("version_check", 1); }
int version_safe_mode(void) { return g_mode == VERSION_UNSUPPORTED && enforce(); }
int version_writes_allowed(void) { return !((g_mode == VERSION_UNSUPPORTED || g_mode == VERSION_ADAPTED) && enforce()); }

// first "Beta 17.1"-like string in .rdata and the build id after it ("260906a")
static void read_version_strings(const uint8_t *rd_, size_t n, char *ver, size_t vcap, char *build, size_t bcap) {
    ver[0] = build[0] = 0;
    for (size_t i = 0; i + 8 < n; ++i) {
        if (memcmp(rd_ + i, "Beta ", 5) || rd_[i + 5] < '0' || rd_[i + 5] > '9' || (i && rd_[i - 1])) continue;
        snprintf(ver, vcap, "%.15s", (const char *)rd_ + i);
        for (size_t j = i + strlen(ver); j < i + 64 && j + 7 < n; ++j) {
            int digits = 0;
            while (digits < 8 && rd_[j + digits] >= '0' && rd_[j + digits] <= '9') ++digits;
            if (digits == 6 && rd_[j - 1] == 0) { snprintf(build, bcap, "%.10s", (const char *)rd_ + j); break; }
        }
        return;
    }
}

void version_init(void) {
    const uint8_t *img = (const uint8_t *)GetModuleHandleA(NULL);
    const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)(img + ((const IMAGE_DOS_HEADER *)img)->e_lfanew);
    uint32_t stamp = nt->FileHeader.TimeDateStamp, image = nt->OptionalHeader.SizeOfImage;
    const IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    const uint8_t *text = NULL, *rdata = NULL;
    size_t text_n = 0, rdata_n = 0;
    uint32_t text_va = 0;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (!memcmp(sec[i].Name, ".text", 6)) { text = img + sec[i].VirtualAddress; text_n = sec[i].Misc.VirtualSize; text_va = (uint32_t)(uintptr_t)text; }
        if (!memcmp(sec[i].Name, ".rdata", 7)) { rdata = img + sec[i].VirtualAddress; rdata_n = sec[i].Misc.VirtualSize; }
    }
    char ver[24] = "", build[16] = "";
    if (rdata) read_version_strings(rdata, rdata_n, ver, sizeof ver, build, sizeof build);
    // the built-in set names the build cogmind.h was mapped on; a sidecar, if any, replaces its signatures
    uint32_t mapped_stamp = 0, mapped_image = 0, file_stamp, file_image;
    int nsigs = sigs_load_builtin(&mapped_stamp, &mapped_image);
    int side = sigs_load_file(SIGS_SIDECAR, &file_stamp, &file_image);
    if (side >= 0) hlog("game: signatures from %s (%d, made from build %08x)", SIGS_SIDECAR, side, file_stamp);
    else if (GetFileAttributesA(SIGS_SIDECAR) != INVALID_FILE_ATTRIBUTES) {
        hlog("game: %s is not usable, using the built-in signatures", SIGS_SIDECAR);
        nsigs = sigs_load_builtin(&mapped_stamp, &mapped_image);
    }
    if (side < 0 && nsigs <= 0) text = NULL;                                  // nothing to scan with
    int known = stamp == mapped_stamp && image == mapped_image;
    hlog("game: %s %s, PE timestamp %08x, image %x%s", ver[0] ? ver : "(no version string)", build, stamp, image,
         known ? " - the build cog-hack was mapped on" : " - NOT the build cog-hack was mapped on");
    DWORD t0 = GetTickCount();
    static uint32_t found[CM_A_COUNT];
    int moved = 0, lost = text ? 0 : CM_A_COUNT;
    if (text) sigs_resolve(text, text_n, text_va, 0, found, &moved, &lost);
    g_mode = lost ? VERSION_UNSUPPORTED : moved ? VERSION_ADAPTED : known ? VERSION_KNOWN : VERSION_COMPATIBLE;
    if (g_mode == VERSION_ADAPTED || (g_mode == VERSION_UNSUPPORTED && !enforce()))
        for (int i = 0; i < CM_A_COUNT; ++i) if (found[i]) cm_addr_table[i] = found[i];   // use what the code uses
    static const char *MODE[] = { "not checked", "known", "compatible", "ADAPTED", "UNSUPPORTED" };
    snprintf(g_text, sizeof g_text, "%s%s%s, build %08x: %s", ver[0] ? ver : "?", build[0] ? "." : "", build, stamp, MODE[g_mode]);
    hlog("game: %d/%d addresses where expected, %d moved, %d not found (%lu ms) -> %s", CM_A_COUNT - moved - lost,
         CM_A_COUNT, moved, lost, GetTickCount() - t0, g_text);
    if (g_mode == VERSION_ADAPTED)
        hlog("game: using the moved addresses; struct layouts are unverified, so memory writes and game calls are %s",
             enforce() ? "off (version_check=0 turns them on)" : "ON (version_check=0)");
    if (g_mode == VERSION_UNSUPPORTED) {
        if (enforce()) {
            hlog("game: safe mode - native plugins, memory writes and game calls are off (version_check=0 overrides)");
            panel_set("version", "!cog-hack: unsupported game build - plugins off (see hack\\hook.log)");
        } else hlog("game: version_check=0 - running anyway on an unsupported build");
    }
}
