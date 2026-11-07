// SPDX-License-Identifier: MPL-2.0
// Crash reports. A vectored exception handler sees a fault before anything else does, writes a
// short report to hook.log and a minidump to hack\snapshots\crash_N.dmp, then lets the exception
// go on as if we weren't there (the game's own handlers, Windows Error Reporting).
//
// The report answers the first question about any crash with the mod installed: was our code
// running? Every call from the hook into our code sets a phase ("plugin tick", "overlay draw", ...);
// "game" means the fault came while the game itself was running. It also lists the last commands
// and key/mouse presses, and return addresses found on the stack as module+offset.
//
// It never tries to recover: skipping a faulting instruction in game code would only corrupt state.
// Vectored handlers see first-chance exceptions, so a report can belong to one that was handled
// afterwards; if the game kept running, it was.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "base/config.h"
#include "base/log.h"
#include "debug/crash.h"
#include "debug/minidump.h"
#include "game/commands.h"

#define CRASH_DIR    "hack\\snapshots"
#define MAX_REPORTS  3
#define RING         8

static volatile const char *g_phase = "startup";
static volatile LONG g_reports, g_busy;
static struct { DWORD t; int id; } g_cmds[RING];
static struct { DWORD t; int type, code, mod; } g_input[RING];
static volatile LONG g_ncmd, g_ninput;

const char *crash_phase(const char *phase) {
    const char *prev = (const char *)g_phase;
    g_phase = phase;
    return prev;
}

void crash_note_command(int id) {
    LONG i = InterlockedIncrement(&g_ncmd) - 1;
    g_cmds[i % RING].t = GetTickCount();
    g_cmds[i % RING].id = id;
}

void crash_note_input(const uint8_t *ev) {
    int type = ev[0];
    if (type != 2 && type != 5) return;              // SDL_KEYDOWN, SDL_MOUSEBUTTONDOWN
    LONG i = InterlockedIncrement(&g_ninput) - 1;
    g_input[i % RING].t = GetTickCount();
    g_input[i % RING].type = type;
    g_input[i % RING].code = type == 2 ? *(const int32_t *)(ev + 8) : ev[2];   // key sym / mouse button
    g_input[i % RING].mod = type == 2 ? *(const int32_t *)(ev + 12) : *(const uint16_t *)(ev + 4) | *(const uint16_t *)(ev + 6) << 16;
}

// "COGMIND.exe+28154" (0 if addr isn't in a module)
static int module_of(uint32_t addr, char *out, size_t cap) {
    HMODULE m;
    char path[MAX_PATH];
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)(uintptr_t)addr, &m) || !GetModuleFileNameA(m, path, sizeof path)) {
        snprintf(out, cap, "?");
        return 0;
    }
    const char *base = strrchr(path, '\\');
    snprintf(out, cap, "%s+%x", base ? base + 1 : path, addr - (uint32_t)(uintptr_t)m);
    return 1;
}

static int readable(uint32_t a, size_t n) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((LPCVOID)(uintptr_t)a, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return 0;
    return a + n <= (uint32_t)(uintptr_t)mbi.BaseAddress + mbi.RegionSize;
}

static int executable(uint32_t a) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((LPCVOID)(uintptr_t)a, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) return 0;
    return (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

// is there a call instruction right before this address? (so it's likely a return address)
static int after_call(uint32_t a) {
    if (a < 0x10006 || !readable(a - 6, 6)) return 0;
    const uint8_t *p = (const uint8_t *)(uintptr_t)(a - 6);
    return p[1] == 0xE8                                   // call rel32
        || (p[0] == 0xFF && (p[1] & 0x38) == 0x10)        // call [disp32] / [reg+disp32]
        || (p[3] == 0xFF && (p[4] & 0x38) == 0x10)        // call [reg+disp8]
        || (p[4] == 0xFF && (p[5] & 0x38) == 0x10);       // call reg / [reg]
}

static const char *code_name(DWORD c) {
    switch (c) {
    case EXCEPTION_ACCESS_VIOLATION:      return "access violation";
    case EXCEPTION_ILLEGAL_INSTRUCTION:   return "illegal instruction";
    case EXCEPTION_PRIV_INSTRUCTION:      return "privileged instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "integer divide by zero";
    case EXCEPTION_STACK_OVERFLOW:        return "stack overflow";
    case EXCEPTION_IN_PAGE_ERROR:         return "in-page error";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
    case 0xC0000374:                      return "heap corruption";
    case 0xC0000409:                      return "stack buffer overrun";
    }
    return "exception";
}

typedef struct { EXCEPTION_POINTERS *ep; DWORD tid; char path[MAX_PATH]; int ok; } dump_job;

static DWORD WINAPI dump_thread(void *p) {        // a dump is written from another thread, never the faulting one
    dump_job *j = p;
    j->ok = minidump_write(j->path, 0, j->ep, j->tid);
    return 0;
}

static void report(EXCEPTION_POINTERS *ep) {
    const EXCEPTION_RECORD *er = ep->ExceptionRecord;
    const CONTEXT *c = ep->ContextRecord;
    uint32_t at = (uint32_t)(uintptr_t)er->ExceptionAddress;
    char where[MAX_PATH + 16], buf[MAX_PATH + 16];
    module_of(at, where, sizeof where);
    char what[96] = "";
    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2)
        snprintf(what, sizeof what, " (%s %08lx)", er->ExceptionInformation[0] == 1 ? "write to" :
                 er->ExceptionInformation[0] == 8 ? "execute at" : "read of", (unsigned long)er->ExceptionInformation[1]);
    hlog("CRASH: %s%s at %08x %s, thread %lu", code_name(er->ExceptionCode), what, at, where, GetCurrentThreadId());
    hlog("  running: %s%s", (const char *)g_phase,
         strcmp((const char *)g_phase, "game") ? " (cog-hack code was on the stack)" : " (the game's own code; ours was not running)");
    hlog("  eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx ebp=%08lx esp=%08lx",
         c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);

    // return addresses on the stack (a scan, not an unwind: most of the game has no frame info)
    if (er->ExceptionCode != EXCEPTION_STACK_OVERFLOW) {
        NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
        uint32_t sp = c->Esp, top = (uint32_t)(uintptr_t)tib->StackBase;
        if (top > sp + 0x10000) top = sp + 0x10000;
        int n = 0;
        for (uint32_t a = sp; a + 4 <= top && n < 24; a += 4) {
            uint32_t v = *(const uint32_t *)(uintptr_t)a;
            if (v < 0x10000 || !executable(v) || !after_call(v) || !module_of(v, buf, sizeof buf)) continue;
            hlog("  stack %08x  %08x %s", a, v, buf);
            ++n;
        }
    }

    char line[256];
    size_t len = 0;
    LONG nc = g_ncmd;
    for (LONG i = nc > RING ? nc - RING : 0; i < nc; ++i) {
        char name[48] = "";
        command_name(g_cmds[i % RING].id, name, sizeof name);
        len += snprintf(line + len, len < sizeof line ? sizeof line - len : 0, " %d%s%s", g_cmds[i % RING].id, name[0] ? ":" : "", name);
        if (len >= sizeof line) break;
    }
    hlog("  last commands:%s", nc ? line : " none");
    len = 0;
    LONG ni = g_ninput;
    for (LONG i = ni > RING ? ni - RING : 0; i < ni; ++i) {
        int k = i % RING;
        size_t room = len < sizeof line ? sizeof line - len : 0;
        unsigned long ago = GetTickCount() - g_input[k].t;
        if (g_input[k].type == 2) len += snprintf(line + len, room, " key %d mod %x (%lums ago)", g_input[k].code, g_input[k].mod, ago);
        else len += snprintf(line + len, room, " click %d at %d,%d (%lums ago)", g_input[k].code,
                             g_input[k].mod & 0xffff, (g_input[k].mod >> 16) & 0xffff, ago);
        if (len >= sizeof line) break;
    }
    hlog("  last input:%s", ni ? line : " none");

    if (g_reports == 1) {                          // one dump per session is plenty
        CreateDirectoryA(CRASH_DIR, NULL);
        static dump_job j;
        j.ep = ep; j.tid = GetCurrentThreadId();
        snprintf(j.path, sizeof j.path, CRASH_DIR "\\crash_%lu.dmp", GetTickCount());
        HANDLE t = CreateThread(NULL, 0, dump_thread, &j, 0, NULL);
        if (t) { WaitForSingleObject(t, 30000); CloseHandle(t); }
        hlog("  dump: %s%s", j.path, j.ok ? "" : " FAILED");
        minidump_prune(CRASH_DIR, "crash_*.dmp", 3, NULL);
    }
}

static LONG CALLBACK veh(EXCEPTION_POINTERS *ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if ((code & 0xF0000000) != 0xC0000000) return EXCEPTION_CONTINUE_SEARCH;   // C++ throws, debug output, ...
    if (InterlockedExchange(&g_busy, 1)) return EXCEPTION_CONTINUE_SEARCH;     // a fault inside the report
    if (InterlockedIncrement(&g_reports) <= MAX_REPORTS) report(ep);
    g_busy = 0;
    return EXCEPTION_CONTINUE_SEARCH;
}

void crash_init(void) {
    if (!cfg_int("crash_report", 1)) return;
    if (AddVectoredExceptionHandler(1, veh)) hlog("crash reports on (hack\\snapshots\\crash_N.dmp + hook.log)");
}
