// SPDX-License-Identifier: MPL-2.0
// UI recordings: the cells of a console's children, every frame they change (tools/re/uirec.py reads them).
#pragma once
void record_frame(void);    // call once per presented frame, before the overlay draws
void record_toggle(void);
void record_bmp(void);      // every ui_record_bmp frames of a recording: the screen as rec_<tick>_<frame>.bmp   // hotkey: start a recording of the configured console now, or stop the current one
