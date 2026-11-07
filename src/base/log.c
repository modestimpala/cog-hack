// SPDX-License-Identifier: MPL-2.0
// Logging to hack\hook.log (falls back to stdout when no log file is open, e.g. in tests).
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "base/log.h"

static FILE *g_log;

// the previous session's log is kept as <name>.prev.log (a crash's report survives one restart)
void log_open(const char *path) {
    if (g_log) return;
    char prev[MAX_PATH];
    const char *dot = strrchr(path, '.');
    size_t stem = dot ? (size_t)(dot - path) : strlen(path);
    if (stem + 10 < sizeof prev) {
        snprintf(prev, sizeof prev, "%.*s.prev%s", (int)stem, path, dot ? dot : "");
        MoveFileExA(path, prev, MOVEFILE_REPLACE_EXISTING);
    }
    g_log = fopen(path, "w");
}

void log_close(void) {
    if (g_log) { fclose(g_log); g_log = NULL; }
}

void hlog(const char *fmt, ...) {
    FILE *f = g_log ? g_log : stdout;
    va_list ap;
    va_start(ap, fmt);
    fprintf(f, "[%8lu] ", GetTickCount());
    vfprintf(f, fmt, ap);
    fputc('\n', f);
    fflush(f);
    va_end(ap);
}
