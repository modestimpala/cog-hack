// SPDX-License-Identifier: MPL-2.0
// Minidumps of our own process (dbghelp), and pruning old ones.
#pragma once
#include <windows.h>
// full = 1: all memory incl. mapped images; 0: heap + writable data (enough for tools/re).
// ep / tid: the exception and its thread for a crash dump, or NULL / 0. 1 ok.
int  minidump_write(const char *path, int full, EXCEPTION_POINTERS *ep, DWORD tid);
// keep the newest `keep` files matching dir\pattern (0 = all); also delete each one's
// same-named sidecar_ext file (".bmp"), if given
void minidump_prune(const char *dir, const char *pattern, int keep, const char *sidecar_ext);
