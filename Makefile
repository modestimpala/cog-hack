# SPDX-License-Identifier: MPL-2.0
# cog-hack, cross-built from WSL/Linux with mingw-w64 (32-bit: the game is i386).
#   make deps      download + verify AngelScript (once)
#   make           build/SDL.dll + build/plugins/*.dll
#   make test      offline tests (Windows exes run through WSL interop)
#   make install   copy into the game folder (game must be closed)

CC   := i686-w64-mingw32-gcc
CXX  := i686-w64-mingw32-g++
AR   := i686-w64-mingw32-ar
B    := build
SDK  := third_party/sdk

WARN     := -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type
CFLAGS   := -O2 $(WARN) -Iinclude -Isrc -I$(B) -DCM_HOST     # CM_HOST: one shared address table (cogmind.h)
CXXFLAGS := $(CFLAGS) -I$(SDK)/angelscript/include -I$(SDK)/add_on
LDFLAGS  := -s -static -static-libgcc -static-libstdc++

HOOKED   := SDL_PollEvent SDL_Flip SDL_UpdateRect SDL_SetVideoMode

# HOOK_SRC needs a live SDL (the real SDL_orig.dll); everything else also links into the test exes
HOOK_SRC := src/hook/hook.c src/hook/hotkeys.c src/overlay/overlay.c src/overlay/font.c \
            src/overlay/backing.c src/debug/snapshot.c
CORE_SRC := $(filter-out $(HOOK_SRC),$(wildcard src/*/*.c src/*/*.cpp))
obj       = $(patsubst src/%,$(B)/obj/%.o,$(basename $(1)))
HOOK_OBJ := $(call obj,$(HOOK_SRC))
CORE_OBJ := $(call obj,$(CORE_SRC))
HEADERS  := $(wildcard src/*/*.h include/*.h) $(B)/sigs_builtin.inc

AS_ADDONS := scriptstdstring/scriptstdstring.cpp scriptstdstring/scriptstdstring_utils.cpp \
             scriptarray/scriptarray.cpp scriptbuilder/scriptbuilder.cpp scripthelper/scripthelper.cpp \
             scriptdictionary/scriptdictionary.cpp scriptmath/scriptmath.cpp
AS_SRC   := $(wildcard $(SDK)/angelscript/source/*.cpp) $(addprefix $(SDK)/add_on/,$(AS_ADDONS))
AS_OBJ   := $(patsubst $(SDK)/%.cpp,$(B)/as/%.o,$(AS_SRC))
AS_LIB   := $(B)/libangelscript.a

PLUGINS  := $(patsubst plugins/%/,$(B)/plugins/%.dll,$(wildcard plugins/*/))
TESTS    := $(B)/test_script.exe $(B)/test_ui.exe $(B)/test_sigs.exe $(B)/replay.exe

.PHONY: all deps test install uninstall clean
all: $(B)/SDL.dll $(PLUGINS)

deps:
	tools/fetch_angelscript.sh

$(SDK)/angelscript/include/angelscript.h:
	@echo "AngelScript SDK missing: run make deps" && false

# the built-in signatures (src/game/sigs.c includes this)
$(B)/sigs_builtin.inc: sigs/cogmind.sigs tools/embed_text.py
	@mkdir -p $(B)
	python3 tools/embed_text.py $< $@

$(B)/obj/%.o: src/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(B)/obj/%.o: src/%.cpp $(HEADERS) $(SDK)/angelscript/include/angelscript.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(B)/as/%.o: $(SDK)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) -O2 -I$(SDK)/angelscript/include -I$(SDK)/add_on -c $< -o $@

$(AS_LIB): $(AS_OBJ)
	$(AR) rcs $@ $^

$(B)/sdl.def: src/hook/sdl_exports.txt tools/gen_sdl_def.py
	@mkdir -p $(B)
	python3 tools/gen_sdl_def.py $< $@ $(HOOKED)

$(B)/SDL.dll: $(HOOK_OBJ) $(CORE_OBJ) $(B)/sdl.def $(AS_LIB)
	$(CXX) -shared $(LDFLAGS) -o $@ $(HOOK_OBJ) $(CORE_OBJ) $(B)/sdl.def $(AS_LIB) -lbcrypt

.SECONDEXPANSION:
$(B)/plugins/%.dll: $$(wildcard plugins/$$*/*.c) $(wildcard include/*.h)
	@mkdir -p $(dir $@)
	$(CC) -O2 $(WARN) -Iinclude -shared -s -static-libgcc -o $@ $(filter %.c,$^) -ladvapi32

# ---- tests ----
# test_script.exe: the script layer + plugin loader outside the game (also used by the plugin repos)
$(B)/test_script.exe: tests/test_script.c $(CORE_OBJ) $(AS_LIB) $(HEADERS)
	$(CXX) $(CFLAGS) $(LDFLAGS) -x c -o $@ $< -x none $(CORE_OBJ) $(AS_LIB) -lbcrypt

$(B)/test_sigs.exe: tests/test_sigs.c $(CORE_OBJ) $(AS_LIB) $(HEADERS)
	$(CXX) $(CFLAGS) $(LDFLAGS) -x c -o $@ $< -x none $(CORE_OBJ) $(AS_LIB) -lbcrypt

$(B)/test_ui.exe: tests/test_ui.c $(wildcard include/*.h)
	@mkdir -p $(B)
	$(CC) -O2 $(WARN) -Iinclude -s -o $@ $<

# replay.exe <snapshot.dmp>: plugin-side state/UI/map code run against a Ctrl+F11 snapshot
$(B)/replay.exe: tests/replay.c $(wildcard include/*.h)
	@mkdir -p $(B)
	$(CC) -O2 $(WARN) -Iinclude -s -o $@ $<

test: $(TESTS) $(PLUGINS)
	tests/run.sh

install: all
	./install.sh

uninstall:
	./uninstall.sh

clean:
	rm -rf $(B)
