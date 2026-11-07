// SPDX-License-Identifier: MPL-2.0
// Guarded memory access + handle registries.
#include <windows.h>
#include <string.h>
#include "cogmind.h"
#include "base/config.h"
#include "base/log.h"
#include "base/mem.h"
#include "game/version.h"

int rd(uint32_t addr, void *out, size_t n) {
    SIZE_T got = 0;
    if (addr < 0x10000) return 0;
    return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(uintptr_t)addr, out, n, &got) && got == n;
}
uint32_t rd32(uint32_t addr) { uint32_t v = 0; rd(addr, &v, 4); return v; }
int32_t  rdi(uint32_t addr)  { int32_t v = 0; rd(addr, &v, 4); return v; }

int rd_string(uint32_t addr, char *out, size_t cap) {
    uint8_t b[MSVC_STRING_SIZE];
    out[0] = 0;
    if (!rd(addr, b, sizeof b)) return 0;
    uint32_t size = *(uint32_t *)(b + 16), scap = *(uint32_t *)(b + 20);
    if (size > scap || scap > (1u << 20)) return 0;
    if (size >= cap) size = cap - 1;
    if (scap < 16) memcpy(out, b, size);
    else if (!rd(*(uint32_t *)b, out, size)) return 0;
    out[size] = 0;
    return 1;
}

// Is [addr, addr+n) committed and writable as-is? WriteProcessMemory would otherwise silently flip
// page protection, letting a script patch game *code*. Outside dev_mode we refuse that.
static int page_writable(uint32_t addr, size_t n) {
    while (n) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((LPCVOID)(uintptr_t)addr, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) return 0;
        DWORD p = mbi.Protect & 0xff;
        if (mbi.Protect & PAGE_GUARD) return 0;
        if (p != PAGE_READWRITE && p != PAGE_WRITECOPY && p != PAGE_EXECUTE_READWRITE && p != PAGE_EXECUTE_WRITECOPY) return 0;
        uintptr_t end = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        size_t span = end - addr < n ? end - addr : n;
        addr += span; n -= span;
    }
    return 1;
}

int wr(uint32_t addr, const void *src, size_t n) {
    static int dev = -1;
    if (dev < 0) dev = cfg_int("dev_mode", 0) != 0;
    if (addr < 0x10000 || !n) return 0;
    if (!version_writes_allowed()) {
        static int warned;
        if (!warned++) hlog("write to %08x refused: game build not verified (%s)", addr, version_text());
        return 0;
    }
    if (!dev && !page_writable(addr, n)) {
        static int warned;
        if (!warned++) hlog("write to %08x refused: not writable memory (set dev_mode=1 to allow)", addr);
        return 0;
    }
    SIZE_T done = 0;
    return WriteProcessMemory(GetCurrentProcess(), (LPVOID)(uintptr_t)addr, src, n, &done) && done == n;
}
int wr32(uint32_t addr, uint32_t v) { return wr(addr, &v, 4); }

int sig_ok(uint32_t addr, const uint8_t *bytes, size_t n, int *cache) {
    if (*cache == 0) {
        uint8_t got[32];
        *cache = (n <= sizeof got && rd(addr, got, n) && !memcmp(got, bytes, n)) ? 1 : -1;
        if (*cache < 0) hlog("code at %08x differs from expected (game update?): feature disabled", addr);
    }
    return *cache > 0;
}

// ---- generational handles: idx = h & 0xffff, gen = h >> 16; registry: ptrs vec @+0, gens vec @+0x10 ----
uint32_t resolve(uint32_t registry_global, uint32_t h) {
    if (!h) return 0;
    uint32_t R = rd32(registry_global);
    if (!R) return 0;
    uint32_t hdr[5];
    if (!rd(R, hdr, sizeof hdr)) return 0;            // first, last, cap, ?, gens.first
    uint32_t first = hdr[0], last = hdr[1], gens = hdr[4];
    uint32_t idx = HANDLE_IDX(h);
    if (!first || last < first || idx >= (last - first) / 4) return 0;
    if (rd32(gens + idx * 4) != HANDLE_GEN(h)) return 0;
    return rd32(first + idx * 4);
}

uint32_t registry_count(uint32_t registry_global) {
    uint32_t R = rd32(registry_global), v[2];
    if (!R || !rd(R, v, sizeof v)) return 0;
    return (v[0] && v[1] >= v[0] && (v[1] - v[0]) / 4 <= 0x10000) ? (v[1] - v[0]) / 4 : 0;   // garbage guard
}

uint32_t registry_slot(uint32_t registry_global, uint32_t idx) {
    uint32_t R = rd32(registry_global);
    if (!R || idx >= registry_count(registry_global)) return 0;   // past the end: not an object
    return rd32(rd32(R) + idx * 4);
}
