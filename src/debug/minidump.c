// SPDX-License-Identifier: MPL-2.0
// Minidumps of our own process: Ctrl+F11 snapshots and crash dumps. Read them with WinDbg or
// tools/re/minidump.py.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "base/log.h"
#include "debug/minidump.h"

// MINIDUMP_TYPE flags
#define MDW_DATA_SEGS          0x00000001
#define MDW_FULL_MEMORY        0x00000002
#define MDW_PRIVATE_RW_MEMORY  0x00000200
#define MDW_FULL_MEMORY_INFO   0x00000800
#define MDW_THREAD_INFO        0x00001000
typedef struct { DWORD tid; EXCEPTION_POINTERS *ep; BOOL client_pointers; } __attribute__((packed)) mdw_exception;
typedef BOOL (WINAPI *MiniDumpWriteDump_t)(HANDLE, DWORD, HANDLE, int, void *, void *, void *);

int minidump_write(const char *path, int full, EXCEPTION_POINTERS *ep, DWORD tid) {
    static MiniDumpWriteDump_t mdwd;
    if (!mdwd) {
        HMODULE dh = LoadLibraryA("dbghelp.dll");
        if (dh) mdwd = (MiniDumpWriteDump_t)GetProcAddress(dh, "MiniDumpWriteDump");
        if (!mdwd) { hlog("minidump: dbghelp unavailable"); return 0; }
    }
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) { hlog("minidump: cannot create %s", path); return 0; }
    int type = full ? MDW_FULL_MEMORY | MDW_FULL_MEMORY_INFO | MDW_THREAD_INFO
                    : MDW_PRIVATE_RW_MEMORY | MDW_DATA_SEGS | MDW_FULL_MEMORY_INFO | MDW_THREAD_INFO;
    mdw_exception ex = { tid, ep, FALSE };
    BOOL ok = mdwd(GetCurrentProcess(), GetCurrentProcessId(), f, type, ep ? &ex : NULL, NULL, NULL);
    CloseHandle(f);
    if (!ok) DeleteFileA(path);
    return ok;
}

void minidump_prune(const char *dir, const char *pattern, int keep, const char *sidecar_ext) {
    if (keep <= 0) return;
    WIN32_FIND_DATAA fd;
    struct { FILETIME t; char name[MAX_PATH]; } files[256];
    int n = 0;
    char glob[MAX_PATH];
    snprintf(glob, sizeof glob, "%s\\%s", dir, pattern);
    HANDLE h = FindFirstFileA(glob, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (n < 256) { files[n].t = fd.ftLastWriteTime; snprintf(files[n].name, MAX_PATH, "%s", fd.cFileName); ++n; }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    while (n > keep) {                               // delete the oldest until `keep` remain
        int oldest = 0;
        for (int i = 1; i < n; ++i)
            if (CompareFileTime(&files[i].t, &files[oldest].t) < 0) oldest = i;
        char path[2 * MAX_PATH];
        snprintf(path, sizeof path, "%s\\%s", dir, files[oldest].name);
        if (DeleteFileA(path)) hlog("pruned: %s", path);
        char *dot = strrchr(path, '.');
        if (sidecar_ext && dot && (size_t)(dot - path) + strlen(sidecar_ext) < sizeof path) { strcpy(dot, sidecar_ext); DeleteFileA(path); }
        files[oldest] = files[--n];
    }
}
