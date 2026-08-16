# AlloyFlux build wrapper.
#
# Three build targets share one DSP codebase (see AGENTS.md):
#
#   firmware  RP2350 / Pico 2, PlatformIO + Arduino-Pico          (./platformio.ini)
#   vcv       VCV Rack 2 plugin, Rack SDK                         (./vcv-plugin)
#   web       Web Configurator, Svelte 5 + Vite                   (./web-configurator)
#
# `make` builds the firmware — the image you flash. `make everything` builds
# all three, which is what to run before committing a change under common/,
# since a shared header edit can break either C++ target.
#
#   make help        list every target
#
# Structure and the Windows toolchain handling below follow the ForgeSeries
# repository's root Makefile.

# ── Windows: find the toolchain ourselves ────────────────────────────────────
# Rack's plugin.mk is POSIX, and the usual Windows trap is Chocolatey's make
# arriving first on PATH: it drives cmd.exe as SHELL and cannot run those
# recipes, failing with an opaque "SLUG could not be found in manifest".
#
# We cannot repair the PATH of the shell that invoked us, but we can pick the
# shell and PATH our own recipes use, which is what actually matters. Override
# MSYS if msys2 lives elsewhere.
#
# Deliberately NOT keyed on $(OS): Git Bash sets OS and USERPROFILE as shell
# variables without exporting them, so make sees neither and the whole block was
# silently skipped there — it worked from PowerShell, where they are exported.
# Probing for msys2 itself is the one signal available in every shell.
MSYS ?= C:/msys64

WIN_SH  := $(wildcard $(MSYS)/usr/bin/sh.exe)
WIN_GXX := $(wildcard $(MSYS)/mingw64/bin/g++.exe)
WIN_JQ  := $(wildcard $(MSYS)/mingw64/bin/jq.exe)

ifneq ($(WIN_SH)$(WIN_GXX)$(WIN_JQ),)
  WINDOWS := 1
endif

# Both spellings of home are tried throughout, since which one is exported
# depends on the shell: USERPROFILE from PowerShell, HOME from Git Bash.
HOMEDIRS := $(subst \,/,$(USERPROFILE)) $(HOME)

ifdef WINDOWS
  # PlatformIO: point PIO at the executable rather than putting its directory on
  # PATH. A native path's backslashes and drive colon corrupt a PATH that make
  # splits on ":" under msys. An absolute path to the exe needs no PATH entry.
  PIO_EXE := $(firstword \
      $(wildcard $(addsuffix /.platformio/penv/Scripts/pio.exe,$(HOMEDIRS))))
  #
  # Neither is found under Git Bash: it exports neither USERPROFILE nor a
  # Windows-shaped HOME (msys2 sets HOME to /home/<user>). PIO then falls back
  # to plain `pio`, so put PlatformIO on PATH there:
  #     export PATH="$USERPROFILE/.platformio/penv/Scripts:$PATH"
  ifneq ($(PIO_EXE),)
    PIO := $(PIO_EXE)
  endif

  # msys2 is only for the Rack plugin. When it is present we also take its sh
  # as SHELL, which is what lets even Chocolatey's make run plugin.mk. msys2's
  # sh converts the inherited Windows PATH to POSIX form on the way in, so
  # node/npm/git stay reachable in recipes without any help from us.
  ifneq ($(WIN_SH),)
    SHELL := $(WIN_SH)
    .SHELLFLAGS := -c
    export PATH := $(MSYS)/usr/bin:$(MSYS)/mingw64/bin:$(PATH)
  endif
endif

# The firmware and the web app need none of the above — only PlatformIO and
# node — so the check fires just for the plugin goals. Doing it at parse time
# means one clear message instead of plugin.mk's "SLUG could not be found in
# manifest", which names neither the missing tool nor the wrong shell.
ifdef WINDOWS
ifneq ($(filter vcv vcv-% audrey-host everything,$(or $(MAKECMDGOALS),all)),)
  WIN_MISSING :=
  ifeq ($(WIN_SH),)
    WIN_MISSING += msys2($(MSYS)/usr/bin/sh.exe)
  endif
  ifeq ($(WIN_GXX),)
    WIN_MISSING += mingw-w64-g++($(MSYS)/mingw64/bin/g++.exe)
  endif
  ifeq ($(WIN_JQ),)
    WIN_MISSING += jq($(MSYS)/mingw64/bin/jq.exe)
  endif
  ifneq ($(WIN_MISSING),)
    $(info )
    $(info Building the VCV Rack plugin on Windows needs msys2, and it is)
    $(info incomplete or missing. Not found:)
    $(info )
    $(foreach m,$(WIN_MISSING),$(info   - $(m)))
    $(info )
    $(info Check install instructions on https://vcvrack.com/manual/Building)
    $(info )
    $(info If msys2 lives elsewhere: make MSYS=D:/msys64 vcv)
    $(info )
    $(error missing Windows toolchain)
  endif
endif
endif

PIO ?= pio

# Host C++ compiler, used only by `make audrey-host`. On Windows the reliable
# one is msys2's — the same requirement the Rack plugin already has — so reuse
# the path probed above rather than hoping g++ is on PATH.
ifdef WINDOWS
  HOST_CXX ?= $(if $(WIN_GXX),$(WIN_GXX),g++)
  EXE      := .exe
else
  HOST_CXX ?= g++
  EXE      :=
endif
BUILD_TMP ?= .build

# Only needed by `make params`; PlatformIO ships one, so fall back to that
# rather than requiring a system Python.
PYTHON ?= $(if $(shell command -v python 2>/dev/null),python,$(HOME)/.platformio/penv/Scripts/python.exe)
ENV ?= alloyflux
NPM ?= npm

# Rack SDK checkout. vcv-plugin/Makefile defaults to ../../Rack-SDK, i.e. a
# sibling of this repository, so normally nothing needs setting. Override with
# an ABSOLUTE path if it lives elsewhere — a relative one would be resolved
# from vcv-plugin/, not from here.
#     make RACK_DIR=D:/Rack-SDK vcv
RACK_ARG = $(if $(RACK_DIR),RACK_DIR=$(RACK_DIR),)

WEB := web-configurator

.DEFAULT_GOAL := all
.PHONY: all everything help list

# `all` is the firmware you flash.
all: firmware

# Everything a change under common/ can break, plus the web app.
everything: firmware vcv web

help list:
	@echo ""
	@echo "AlloyFlux - make targets"
	@echo ""
	@echo "  Firmware (RP2350, env:$(ENV))"
	@echo "    firmware          build firmware.uf2                  (default)"
	@echo "    upload            build and flash over USB"
	@echo "    upload-monitor    flash, then open the serial monitor"
	@echo "    monitor           serial monitor only"
	@echo "    test              PlatformIO native unit tests"
	@echo "    firmware-clean    clean the PlatformIO build"
	@echo ""
	@echo "  VCV Rack plugin"
	@echo "    vcv               build vcv-plugin/plugin.dll"
	@echo "    vcv-install       build and install into Rack's user plugin dir"
	@echo "    vcv-dist          package the .vcvplugin for the VCV library"
	@echo "    vcv-clean         clean the plugin build"
	@echo "    print-plugins-dir print Rack's user plugin directory"
	@echo ""
	@echo "  Web Configurator (Svelte + Vite)"
	@echo "    web               production build into $(WEB)/dist"
	@echo "    web-dev           vite dev server on localhost:5173"
	@echo "    web-check         svelte-check + tsc type validation"
	@echo "    web-deps          npm install"
	@echo "    web-clean         remove dist/ and node_modules/"
	@echo ""
	@echo "  Across the repo"
	@echo "    everything        firmware + vcv + web"
	@echo "    format            clang-format every tracked C/C++ file"
	@echo "    format-check      verify formatting without writing (CI gate)"
	@echo "    clean             all three clean targets"
	@echo ""

# ── Parameters ───────────────────────────────────────────────────────────────
# A module declares its parameters once, in modules/<name>/params.json. This
# regenerates every table derived from it. The output is committed, so an
# ordinary build never needs Python — only editing params.json does.
.PHONY: params params-check
MODULE ?= alloyflux

params:
	$(PYTHON) tools/gen_params.py modules/$(MODULE)

# CI gate: regenerates, then fails if that produced a diff — i.e. the committed
# tables do not match params.json. On a clean checkout (CI) that is exactly the
# staleness check. In a dirty working tree it will also fire on generated
# changes you have made but not yet committed, which is the same instruction:
# commit them alongside the params.json edit that caused them.
params-check: params
	@git diff --exit-code -- modules/$(MODULE)/include/param_manifest.generated.h \
	                         web-configurator/src/lib/paramMap.ts \
	  || { echo "Generated parameter tables differ from what is committed."; \
	       echo "If you just edited params.json, commit the regenerated files too."; exit 1; }

# ── Firmware ─────────────────────────────────────────────────────────────────
# One PlatformIO environment per module: the RP2350 image. See platformio.ini.
.PHONY: firmware upload upload-monitor monitor firmware-clean

firmware:
	$(PIO) run -e $(ENV)

upload:
	$(PIO) run -e $(ENV) -t upload

upload-monitor:
	$(PIO) run -e $(ENV) -t upload -t monitor

monitor:
	$(PIO) device monitor

firmware-clean:
	$(PIO) run -e $(ENV) -t clean

# `pio test` fails outright ("Nothing to build. Please put your test suites to
# the 'test' folder") when test/ holds only its README, which is the case
# today. Ask the filesystem rather than hardcoding it, so this starts working
# the moment a suite is added.
.PHONY: test
TEST_SUITES := $(wildcard test/test_*)

test:
ifeq ($(TEST_SUITES),)
	@echo "No test suites under test/ - nothing to run."
	@echo "Add test/test_<name>/ to enable 'make test'."
else
	$(PIO) test -e $(ENV)
endif

# ── Audrey engine, host build ────────────────────────────────────────────────
# Compiles the vendored Audrey engine (M63e) against nothing but the vendored
# DaisySP subset and the standard library — no Daisy headers, no SDRAM
# allocator, no heap. Then runs it for 10 s at maximum feedback and echo
# feedback above unity, checking for NaN, silence and DC drift, and prints the
# static footprint of each big member.
#
# That footprint is the point: it is what decides whether the engine can fit a
# 520 KB RP2350 at all, and it costs nothing to learn here rather than on a
# flashed board.
.PHONY: audrey-host audrey-host-clean

AUDREY_BIN  := $(BUILD_TMP)/audrey_host$(EXE)
AUDREY_SRCS := modules/audrey/test/host_build.cpp \
               $(wildcard modules/audrey/src/*.cpp) \
               vendor/daisysp/dcblock.cpp \
               vendor/daisysp/tone.cpp \
               vendor/daisysp/crossfade.cpp \
               vendor/daisysp/overdrive.cpp \
               vendor/daisysp/reverbsc.cpp

audrey-host:
	@mkdir -p $(BUILD_TMP)
	$(HOST_CXX) -std=c++14 -O2 -Wall -Wextra -Wno-unused-parameter \
	  -Ivendor/daisysp -Imodules/audrey/include \
	  $(AUDREY_SRCS) -o $(AUDREY_BIN)
	@$(AUDREY_BIN)

audrey-host-clean:
	rm -f $(AUDREY_BIN)

# ── VCV Rack plugin ──────────────────────────────────────────────────────────
# PlatformIO does NOT compile vcv-plugin/, so `make firmware` passing says
# nothing about the Rack port. Any edit under modules/alloyflux/ or
# platform/include/ has to be checked here too.
#
# install / dist come from Rack's plugin.mk, which vcv-plugin/Makefile includes.
.PHONY: vcv vcv-install vcv-dist vcv-clean print-plugins-dir

vcv:
	$(MAKE) -C vcv-plugin $(RACK_ARG)

vcv-install:
	$(MAKE) -C vcv-plugin install $(RACK_ARG)

vcv-dist:
	$(MAKE) -C vcv-plugin dist $(RACK_ARG)

vcv-clean:
	$(MAKE) -C vcv-plugin clean $(RACK_ARG)

# Where Rack loads user plugins from. plugin.mk works this out per platform
# (LOCALAPPDATA on Windows, XDG on Linux, Application Support on macOS), so ask
# it rather than guessing.
print-plugins-dir:
	@$(MAKE) --no-print-directory -C vcv-plugin print-plugins-dir $(RACK_ARG)

# ── Web Configurator ─────────────────────────────────────────────────────────
# Chrome or Edge only at runtime — Web Serial and Web MIDI are not implemented
# in Firefox or Safari.
#
# The build targets bootstrap node_modules on first run so a fresh clone works
# with a single `make web`.
.PHONY: web web-dev web-check web-deps web-clean

$(WEB)/node_modules:
	cd $(WEB) && $(NPM) install

web-deps:
	cd $(WEB) && $(NPM) install

web: $(WEB)/node_modules
	cd $(WEB) && $(NPM) run build

web-dev: $(WEB)/node_modules
	cd $(WEB) && $(NPM) run dev

web-check: $(WEB)/node_modules
	cd $(WEB) && $(NPM) run check

web-clean:
	rm -rf $(WEB)/dist $(WEB)/node_modules

# ── Formatting ───────────────────────────────────────────────────────────────
# Style is governed by .clang-format (4-space, 80 columns, Allman). clang-format
# is usually not on PATH on Windows: the C/C++ extension ships one, so fall back
# to that. Override with `make CLANG_FORMAT=/path/to/clang-format format`.
CLANG_FORMAT ?= $(firstword \
    $(shell command -v clang-format 2>/dev/null) \
    $(wildcard $(MSYS)/mingw64/bin/clang-format.exe) \
    $(lastword $(sort $(wildcard \
        $(addsuffix /.vscode/extensions/ms-vscode.cpptools-*/LLVM/bin/clang-format.exe,$(HOMEDIRS))))) \
    clang-format)

# --others --exclude-standard so a newly added file is covered before its first
# commit; plain `git ls-files` sees only tracked files and would silently skip
# it. Ignored paths (.pio/, node_modules/) stay excluded either way.
#
# Vendored trees are excluded on purpose. Both carry a recorded upstream commit
# and are expected to be re-vendored against a newer one; restyling them would
# turn every future diff against upstream into noise and bury the handful of
# lines we actually changed. Their own style is upstream's business.
FORMAT_FILES = git ls-files --cached --others --exclude-standard \
    "*.h" "*.hpp" "*.c" "*.cc" "*.cpp" \
    ":(exclude)vendor/" ":(exclude)modules/audrey/"

.PHONY: format format-check

format:
	@echo "clang-format: $(CLANG_FORMAT)"
	$(FORMAT_FILES) | xargs -r "$(CLANG_FORMAT)" -i -style=file

format-check:
	@echo "clang-format: $(CLANG_FORMAT)"
	$(FORMAT_FILES) | xargs -r "$(CLANG_FORMAT)" --dry-run --Werror -style=file

# ── Clean ────────────────────────────────────────────────────────────────────
.PHONY: clean
clean: firmware-clean vcv-clean
	rm -rf $(WEB)/dist
