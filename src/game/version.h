// SPDX-License-Identifier: MPL-2.0
// Game build detection, run once at startup: fingerprints COGMIND.exe, re-finds every address
// with the signatures (sigs.h) and decides what is safe to run.
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
enum { VERSION_NOT_CHECKED, VERSION_KNOWN, VERSION_COMPATIBLE, VERSION_ADAPTED, VERSION_UNSUPPORTED };
void        version_init(void);
int         version_mode(void);
const char *version_text(void);           // "Beta 17.1.260906a, build 6a9cc02c: known"
int         version_safe_mode(void);      // UNSUPPORTED and version_check=1: no plugins, writes or game calls
int         version_writes_allowed(void); // not ADAPTED/UNSUPPORTED (or version_check=0)
#ifdef __cplusplus
}
#endif
