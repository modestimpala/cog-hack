// SPDX-License-Identifier: MPL-2.0
// AngelScript core: engine lifecycle, console window, line execution, script loading, events.
//
// Threading: the console thread only *reads lines* into a queue. Everything else: script lines,
// loads, events, game calls: runs on the game's main thread (script_pump / script_frame / the
// command observer), the same place the game itself turns key bindings into commands.
#include <windows.h>
#include <angelscript.h>
#include "scriptstdstring/scriptstdstring.h"
#include "scriptarray/scriptarray.h"
#include "scriptbuilder/scriptbuilder.h"
#include "scripthelper/scripthelper.h"
#include "scriptmath/scriptmath.h"
#include "scriptdictionary/scriptdictionary.h"
#include <string>
#include <deque>
#include <cstdio>
#include "base/config.h"
#include "cm_ui.h"
#include "base/log.h"
#include "base/mem.h"
#include "debug/crash.h"
#include "game/commands.h"
#include "hook/ui_events.h"
#include "script/script.h"
#include "script/script_internal.h"

asIScriptEngine *g_engine;
static asIScriptContext *g_ctx;      // console lines, main()
static asIScriptContext *g_evctx;    // events fired while g_ctx is busy
static HANDLE g_con_out = INVALID_HANDLE_VALUE;
static HWND g_con_wnd;
static bool g_console_open;
static CRITICAL_SECTION g_qlock;
static std::deque<std::string> g_queue;
static volatile LONG g_pending;      // lines queued; lets script_pump skip the lock when idle

static const char *SCRIPT_DIR = "hack\\scripts\\";
static char g_script_root[MAX_PATH]; // absolute, with trailing backslash

// cached handlers of the "user" module; null = not defined or disabled after an exception
static asIScriptFunction *g_onFrame, *g_onCommand, *g_onUiHover, *g_onUiClick;
static int g_in_event;

// ---------------- output ----------------
void out(const std::string &s) {
    if (g_con_out != INVALID_HANDLE_VALUE) {
        DWORD w;
        WriteConsoleA(g_con_out, s.data(), (DWORD)s.size(), &w, NULL);
        WriteConsoleA(g_con_out, "\r\n", 2, &w, NULL);
    }
    hlog("[as] %s", s.c_str());
}

static void message_cb(const asSMessageInfo *msg, void *) {
    const char *type = msg->type == asMSGTYPE_ERROR ? "ERR " : msg->type == asMSGTYPE_WARNING ? "WARN" : "INFO";
    char buf[1024];
    snprintf(buf, sizeof buf, "%s (%d,%d) %s: %s", msg->section, msg->row, msg->col, type, msg->message);
    out(buf);
}

// ---------------- watchdog ----------------
// A runaway script (e.g. while(true) in onFrame) would freeze the game. The line callback aborts
// execution once the per-call deadline passes.
struct Watch { DWORD deadline; DWORD limit; };
static Watch g_watch_ctx, g_watch_ev;

static void line_cb(asIScriptContext *ctx, Watch *w) {
    if (w->deadline && (LONG)(GetTickCount() - w->deadline) > 0) ctx->Abort();
}
static void arm(Watch &w) { w.deadline = GetTickCount() + w.limit; if (!w.deadline) w.deadline = 1; }

static void report(asIScriptContext *ctx, int r, const Watch &w) {
    char buf[512];
    if (r == asEXECUTION_EXCEPTION) {
        const asIScriptFunction *f = ctx->GetExceptionFunction();
        snprintf(buf, sizeof buf, "exception: %s (in %s, line %d)", ctx->GetExceptionString(),
                 f ? f->GetDeclaration() : "?", ctx->GetExceptionLineNumber());
        out(buf);
    } else if (r == asEXECUTION_ABORTED) {
        snprintf(buf, sizeof buf, "aborted: ran longer than %lu ms (script_timeout_ms / console_timeout_ms in hack\\config.txt)", w.limit);
        out(buf);
    }
}

// ---------------- safe paths ----------------
// Resolve `rel` against hack\scripts\ and refuse anything that escapes it (.., absolute paths).
static bool script_path(const std::string &rel, const char *base_dir, std::string &full) {
    char buf[MAX_PATH];
    std::string joined = std::string(base_dir) + rel;
    DWORD n = GetFullPathNameA(joined.c_str(), sizeof buf, buf, NULL);
    if (!n || n >= sizeof buf) return false;
    if (_strnicmp(buf, g_script_root, strlen(g_script_root)) != 0) return false;
    full = buf;
    return true;
}

static int include_cb(const char *include, const char *from, CScriptBuilder *b, void *) {
    std::string dir = from, full;
    size_t slash = dir.find_last_of("\\/");
    dir = slash == std::string::npos ? std::string(g_script_root) : dir.substr(0, slash + 1);
    if (!script_path(include, dir.c_str(), full)) {
        out(std::string("#include refused (outside hack\\scripts): ") + include);
        return -1;
    }
    return b->AddSectionFromFile(full.c_str());
}

// ---------------- running code ----------------
static void cache_handlers() {
    asIScriptModule *m = g_engine->GetModule("user");
    g_onFrame = m ? m->GetFunctionByDecl("void onFrame()") : NULL;
    g_onCommand = m ? m->GetFunctionByDecl("void onCommand(int, const string &in)") : NULL;
    g_onUiHover = m ? m->GetFunctionByDecl("void onUiHover(const string &in)") : NULL;
    g_onUiClick = m ? m->GetFunctionByDecl("bool onUiClick(int, int, int)") : NULL;
}

int load_script(const std::string &file) {
    std::string path;
    if (!script_path(file, g_script_root, path)) { out("load refused (outside hack\\scripts): " + file); return -1; }
    CScriptBuilder b;
    b.SetIncludeCallback(include_cb, NULL);
    if (b.StartNewModule(g_engine, "user") < 0 || b.AddSectionFromFile(path.c_str()) < 0 || b.BuildModule() < 0) {
        out("load failed: " + file);
        cache_handlers();
        return -1;
    }
    cache_handlers();
    out("loaded " + file);
    asIScriptFunction *main = g_engine->GetModule("user")->GetFunctionByDecl("void main()");
    if (main && g_ctx->Prepare(main) >= 0) {
        arm(g_watch_ctx);
        report(g_ctx, g_ctx->Execute(), g_watch_ctx);
        g_ctx->Unprepare();
    }
    return 0;
}

static void exec_line(std::string line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
    if (line.empty()) return;
    if (line[0] == '.') {
        if (!line.compare(0, 6, ".load ")) { load_script(line.substr(6)); return; }
        if (line == ".help") {
            out("AngelScript console. Lines run as statements on the game thread; ';' optional.\n"
                "  .load <file.as>      build hack\\scripts\\<file> into the user module and run main()\n"
                "  print(core());       cmd(\"CMD_INVENTORY_EQUIP4\");   print(cmd_list(\"INVENTORY\").length());\n"
                "  for (uint i=0;i<item_count();i++) print(item_name(i)+\" \"+item_hp(i)+\"/\"+item_max_hp(i));\n"
                "  Ctrl+F9 hides this window again.");
            return;
        }
        out("unknown meta command; .help");
        return;
    }
    char last = line.back();
    if (last != ';' && last != '}') line += ';';
    asIScriptModule *mod = g_engine->GetModule("user", asGM_CREATE_IF_NOT_EXISTS);
    arm(g_watch_ctx);
    int r = ExecuteString(g_engine, line.c_str(), mod, g_ctx);
    if (r == asEXECUTION_EXCEPTION || r == asEXECUTION_ABORTED) report(g_ctx, r, g_watch_ctx);
    else if (r < 0) { char b[64]; snprintf(b, sizeof b, "(build/exec error %d)", r); out(b); }
}

// ---------------- events ----------------
// Run an event handler; set_args fills its arguments. Returns what a bool handler returned (false
// for void handlers, or when it failed).
template <class F> static bool call_event(asIScriptFunction *&fn, F set_args) {
    if (!fn || g_in_event) return false;             // no re-entrancy: cmd() inside onCommand won't recurse
    ++g_in_event;
    bool busy = g_ctx->GetState() == asEXECUTION_ACTIVE;   // a console line may be running
    asIScriptContext *ctx = busy ? g_evctx : g_ctx;
    Watch &w = busy ? g_watch_ev : g_watch_ctx;
    DWORD saved_limit = w.limit;
    w.limit = cfg_int("script_timeout_ms", 250);
    bool result = false;
    if (ctx->Prepare(fn) >= 0) {
        set_args(ctx);
        arm(w);
        int r = ctx->Execute();
        if (r == asEXECUTION_FINISHED && fn->GetReturnTypeId() == asTYPEID_BOOL) result = ctx->GetReturnByte() != 0;
        if (r == asEXECUTION_EXCEPTION || r == asEXECUTION_ABORTED) {
            report(ctx, r, w);
            out(std::string("(") + fn->GetName() + " disabled until the next .load)");
            fn = NULL;
        }
        ctx->Unprepare();
    }
    w.limit = saved_limit;
    --g_in_event;
    return result;
}
static void no_args(asIScriptContext *) {}

// UI events from hook/ui_events.c: hover changes and mouse presses
static int __cdecl ui_event(void *, const cm_ui_event *e) {
    if (e->type == CM_UI_HOVER && g_onUiHover) {
        char b[320];
        cm_ui_describe(rd, e->node, b, sizeof b);
        std::string s = b;
        call_event(g_onUiHover, [&](asIScriptContext *c) { c->SetArgObject(0, &s); });
    }
    if (e->type == CM_UI_MOUSE_DOWN && g_onUiClick)   // true from the script swallows the click
        return call_event(g_onUiClick, [&](asIScriptContext *c) {
            c->SetArgDWord(0, (asDWORD)e->cx); c->SetArgDWord(1, (asDWORD)e->cy); c->SetArgDWord(2, (asDWORD)e->button);
        }) ? 1 : 0;
    return 0;
}

static void __cdecl command_observer(void *cmd) {   // called by the game for every command
    if (!cmd) return;
    int id = *(int *)cmd;
    crash_note_command(id);
    if (!g_onCommand) return;
    const char *phase = crash_phase("onCommand script");
    char b[96];
    command_name(id, b, sizeof b);
    std::string name = b;
    call_event(g_onCommand, [&](asIScriptContext *c) { c->SetArgDWord(0, (asDWORD)id); c->SetArgObject(1, &name); });
    crash_phase(phase);
}

// ---------------- console window ----------------
static DWORD WINAPI console_reader(LPVOID) {
    HANDLE in = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    char buf[2048];
    DWORD n;
    for (;;) {
        if (!ReadConsoleA(in, buf, sizeof buf - 1, &n, NULL)) { Sleep(100); continue; }
        buf[n] = 0;
        EnterCriticalSection(&g_qlock);
        g_queue.push_back(buf);
        InterlockedIncrement(&g_pending);
        LeaveCriticalSection(&g_qlock);
    }
    return 0;
}

static void console_create() {
    if (!AllocConsole()) return;
    SetConsoleTitleA("cog-hack AngelScript console");
    g_con_out = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    g_con_wnd = GetConsoleWindow();
    HMENU m = GetSystemMenu(g_con_wnd, FALSE);       // closing an attached console kills the game:
    if (m) DeleteMenu(m, SC_CLOSE, MF_BYCOMMAND);    // remove the close button entirely
    SetConsoleCtrlHandler(NULL, TRUE);               // ignore Ctrl+C
    CreateThread(NULL, 0, console_reader, NULL, 0, NULL);
    out("cog-hack console ready. Type .help");
}

extern "C" void script_toggle_console(void) {
    if (!g_engine) return;
    if (!g_con_wnd) { console_create(); g_console_open = true; return; }
    g_console_open = !g_console_open;
    ShowWindow(g_con_wnd, g_console_open ? SW_SHOW : SW_HIDE);
    if (g_console_open) SetForegroundWindow(g_con_wnd);
}

// ---------------- lifecycle ----------------
extern "C" void script_init(void) {
    if (g_engine) return;
    InitializeCriticalSection(&g_qlock);
    CreateDirectoryA("hack", NULL);
    CreateDirectoryA("hack\\scripts", NULL);
    if (!GetFullPathNameA(SCRIPT_DIR, sizeof g_script_root, g_script_root, NULL)) strcpy(g_script_root, SCRIPT_DIR);

    g_engine = asCreateScriptEngine();
    if (!g_engine) { hlog("AngelScript: engine creation failed"); return; }
    g_engine->SetMessageCallback(asFUNCTION(message_cb), 0, asCALL_CDECL);
    RegisterStdString(g_engine);
    RegisterScriptArray(g_engine, true);
    RegisterStdStringUtils(g_engine);
    RegisterScriptMath(g_engine);
    RegisterScriptDictionary(g_engine);
    register_api(g_engine);

    g_watch_ctx.limit = cfg_int("console_timeout_ms", 5000);
    g_watch_ev.limit = cfg_int("script_timeout_ms", 250);
    g_ctx = g_engine->CreateContext();
    g_evctx = g_engine->CreateContext();
    g_ctx->SetLineCallback(asFUNCTION(line_cb), &g_watch_ctx, asCALL_CDECL);
    g_evctx->SetLineCallback(asFUNCTION(line_cb), &g_watch_ev, asCALL_CDECL);

    install_command_observer(command_observer);
    ui_event_register(ui_event, NULL);
    hlog("AngelScript %s ready (%s)", asGetLibraryVersion(), asGetLibraryOptions());
    if (cfg_int("console_at_start", 0)) { console_create(); g_console_open = true; }   // config.txt
    load_plugins(g_engine);                          // before autoexec so scripts can use plugin functions
    if (GetFileAttributesA("hack\\scripts\\autoexec.as") != INVALID_FILE_ATTRIBUTES) load_script("autoexec.as");
}

extern "C" void script_pump(void) {
    if (!g_engine || !InterlockedCompareExchange(&g_pending, 0, 0)) return;   // idle: no lock taken
    for (;;) {
        std::string line;
        EnterCriticalSection(&g_qlock);
        if (!g_queue.empty()) { line = g_queue.front(); g_queue.pop_front(); InterlockedDecrement(&g_pending); }
        LeaveCriticalSection(&g_qlock);
        if (line.empty()) break;
        exec_line(line);
    }
}

extern "C" void script_frame(void) {
    if (!g_engine) return;
    call_event(g_onFrame, no_args);
    g_engine->GarbageCollect(asGC_ONE_STEP);         // incremental: reclaim script garbage cycles steadily
}

extern "C" int script_exec_for_test(const char *line) {   // offline tests only
    exec_line(line);
    return 0;
}
