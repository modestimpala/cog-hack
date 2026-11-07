// SPDX-License-Identifier: MIT
// Loaded automatically at startup (hack\scripts\autoexec.as). Functions defined here are callable
// from the console:   status()   parts()   bots(15)   swap(3)
// Note: ".load other.as" replaces this module, so put shared helpers in here.

void main() {
    print("autoexec loaded: status() parts() bots(radius) equip(slot) swap(slot) cmds(\"filter\") trace(true) item(\"name\") bot(\"name\") heal() repair()");
}

// ---- events ----
bool g_trace = false;
// trace(true): print every command the game receives (from your keys or from scripts).
// Handy for discovering which commands a UI flow uses, e.g. a real inventory swap.
void trace(bool on) { g_trace = on; print("command trace " + (on ? "on" : "off")); }

void onCommand(int id, const string &in name) {
    if (g_trace) print("  [cmd " + id + "] " + name);
}

void status() {
    if (!in_game()) { print("not in a run"); return; }
    print("core " + core() + "/" + max_core() + "  energy " + energy() + "/" + max_energy() +
          "  matter " + matter() + "/" + max_matter() + "  corruption " + corruption() + "%  @(" + px() + "," + py() + ")");
}

// ---- definitions ----
// item("Ion Engine"): every known stat of an item type
void item(const string &in name) {
    int d = item_def_find(name);
    if (d < 0) { print("no item named '" + name + "'"); return; }
    array<string>@ f = item_fields().split(", ");
    string line = "#" + d + " " + item_def_name(d) + ":";
    for (uint i = 0; i < f.length(); i++) { int v = item_def_get(d, f[i]); if (v != 0) line += " " + f[i] + "=" + v; }
    print(line);
}
void bot(const string &in name) {
    int d = bot_def_find(name);
    if (d < 0) { print("no bot named '" + name + "'"); return; }
    array<string>@ f = bot_fields().split(", ");
    string line = "#" + d + " " + bot_def_name(d) + ":";
    for (uint i = 0; i < f.length(); i++) line += " " + f[i] + "=" + bot_def_get(d, f[i]);
    print(line);
}

// ---- writes (cheats / testing) ----
// heal() / repair() are cheats: they need dev_mode=1 in hack\config.txt
void heal()   { set_core(max_core()); set_energy(max_energy()); set_matter(max_matter()); status(); }
void repair() { for (uint i = 0; i < item_count(); i++) set_item_hp(i, item_max_hp(i)); parts(); }

string slotName(int s) {
    switch (s) {
        case POWER: return "power";  case PROPULSION: return "prop";
        case UTILITY: return "util"; case WEAPON: return "weapon";
        case INVENTORY: return "inv";
    }
    return "?";
}

void parts() {
    for (uint i = 0; i < item_count(); i++)
        print(formatInt(i, "", 2) + " " + slotName(item_slot(i)) + "\t" + item_name(i) +
              "  " + item_hp(i) + "/" + item_max_hp(i));
}

void bots(int radius) {
    int x = px(), y = py(), n = 0;
    for (uint i = 1; i < bot_count(); i++) {
        string name = bot_name(i);
        if (name == "") continue;
        int d = max(abs(bot_x(i) - x), abs(bot_y(i) - y));
        if (d <= radius) { print("  " + name + "  core " + bot_core(i) + "  dist " + d); n++; }
    }
    print(n + " bots within " + radius);
}

// Inventory commands, exactly like pressing the bound key. Slots 1-10 (10 = the game's "0").
string res(int r) { return r == 1 ? "handled" : r == 0 ? "not handled here" : "error"; }
string invCmd(const string &in what, int slot) { return "CMD_INVENTORY_" + what + (slot == 10 ? 0 : slot); }

// equip(4): attach inventory item 4: the game picks the slot / part to replace (pure-keyboard swap)
void equip(int slot) { string c = invCmd("EQUIP", slot); print(c + " -> " + res(cmd(c))); }

// swap(3): the game's "swap" flow: it first asks which equipped part to swap out
void swap(int slot)  { string c = invCmd("SWAP", slot);  print(c + " -> " + res(cmd(c))); }

void cmds(const string &in filter) {
    array<string>@ a = cmd_list(filter);
    for (uint i = 0; i < a.length(); i++) print("  " + cmd_find(a[i]) + "  " + a[i]);
}

int abs(int v) { return v < 0 ? -v : v; }
int max(int a, int b) { return a > b ? a : b; }
