// SPDX-License-Identifier: MPL-2.0
// UI test: a framed window drawn as one label, a button handled through onUiClick
void main() {
    ui_label("win", 2, 2, "┌─ test ─┐\n│ hello  │\n└────────┘", 0x00b200, 0x000000);
    ui_label("btn", 3, 4, " OK ", 0x000000, 0x00b200);
    ui_panel("p", "panel line");
    ui_label_tile("icon", 1, 1, 2, 65, 0xffffff);
    int font, ch; uint fg, bg;
    print("cell=" + ui_cell(3, 4, font, ch, fg, bg) + " " + font + " " + ch);
}

bool onUiClick(int x, int y, int button) {
    bool ok = y == 4 && x >= 3 && x < 7;
    print("click " + x + "," + y + " button " + button + (ok ? " -> OK" : " -> passed on"));
    return ok;
}
