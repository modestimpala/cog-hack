// SPDX-License-Identifier: MPL-2.0
// hack\hook.log (stdout when no log file is open, e.g. in tests).
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void hlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_open(const char *path);
void log_close(void);
#ifdef __cplusplus
}
#endif
