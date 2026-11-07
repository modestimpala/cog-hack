// SPDX-License-Identifier: MPL-2.0
// Shared between the script_*.cpp files. Not part of the plugin API.
#pragma once
#include <angelscript.h>
#include <string>

extern asIScriptEngine *g_engine;

void out(const std::string &s);                // console window + hack\hook.log
int  load_script(const std::string &file);     // build hack\scripts\<file> into module "user", run main()
void register_api(asIScriptEngine *e);         // script_api.cpp
void load_plugins(asIScriptEngine *e);         // plugin_host.cpp
