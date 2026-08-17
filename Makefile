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
ifneq ($(filter vcv vcv-% audrey-host audrey-ab audrey-ab-% everything,$(or $(MAKECMDGOALS),all)),)
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

# ── Modules ──────────────────────────────────────────────────────────────────
# The platform hosts one engine per firmware image, so each module is its own
# PlatformIO env; ENV selects which. The VCV plugin is the opposite — one .dll
# carries every module and Rack offers them all, so there is nothing per-module
# to build there.
MODULES := alloyflux audrey

# `all` is the firmware you flash — AlloyFlux unless ENV says otherwise.
all: firmware

# Everything a shared change can break. Both firmware images, because a change
# under platform/ compiles differently against each module, and both web builds
# for the same reason.
everything: firmware-all vcv web-all

.PHONY: firmware-all web-all
firmware-all:
	@for m in $(MODULES); do \
	  echo ""; echo "--- firmware: $$m ---"; \
	  $(MAKE) --no-print-directory firmware ENV=$$m || exit 1; \
	done

web-all:
	@for m in $(MODULES); do \
	  echo ""; echo "--- web: $$m ---"; \
	  $(MAKE) --no-print-directory web MODULE=$$m || exit 1; \
	done

help list:
	@echo ""
	@echo "Alloy Platform - make targets"
	@echo ""
	@echo "  Modules: $(MODULES)"
	@echo "    ENV=<module>      picks the firmware image   (current: $(ENV))"
	@echo "    MODULE=<module>   picks the params/web target (current: $(MODULE))"
	@echo ""
	@echo "  Firmware (RP2350, one image per module)"
	@echo "    firmware          build firmware.uf2                  (default)"
	@echo "    firmware-all      build every module's image"
	@echo "    upload            build and flash over USB"
	@echo "    upload-monitor    flash, then open the serial monitor"
	@echo "    monitor           serial monitor only"
	@echo "    test              PlatformIO native unit tests"
	@echo "    firmware-clean    clean the PlatformIO build"
	@echo "      e.g.  make upload ENV=audrey"
	@echo ""
	@echo "  VCV Rack plugin (ONE plugin, all modules in it)"
	@echo "    vcv               build vcv-plugin/plugin.dll"
	@echo "    vcv-install       build and install into Rack's user plugin dir"
	@echo "    vcv-dist          package the .vcvplugin for the VCV library"
	@echo "    vcv-clean         clean the plugin build"
	@echo "    print-plugins-dir print Rack's user plugin directory"
	@echo ""
	@echo "  Web Configurator (Svelte + Vite, one build per module)"
	@echo "    web               production build into $(WEB)/dist"
	@echo "    web-all           build every module's configurator in turn"
	@echo "    web-dev           vite dev server on localhost:5173"
	@echo "    web-check         svelte-check + tsc type validation"
	@echo "    web-deps          npm install"
	@echo "    web-clean         remove dist/ and node_modules/"
	@echo "      e.g.  make web-dev MODULE=audrey"
	@echo ""
	@echo "  Parameters"
	@echo "    params            regenerate tables from modules/\$$(MODULE)/params.json"
	@echo "    params-check      CI gate: fail if the committed tables are stale"
	@echo ""
	@echo "  Audrey engine"
	@echo "    audrey-host       host-build + run the engine, print its footprint"
	@echo "    audrey-ab         measure echo aliasing + quantiser noise"
	@echo "    audrey-ab-sweep   the same, across the whole switch matrix"
	@echo ""
	@echo "  Across the repo"
	@echo "    everything        every firmware image + vcv + every web build"
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

AUDREY_BIN := $(BUILD_TMP)/audrey_host$(EXE)

# The three engine sources, named rather than globbed. modules/audrey/src/ also
# holds the firmware integration (main, hooks, commands, config, param_map),
# which needs Arduino and the platform headers and has no business in a host
# build — this harness exercises the DSP, not the module.
AUDREY_SRCS := modules/audrey/test/host_build.cpp \
               modules/audrey/src/FeedbackSynthEngine.cpp \
               modules/audrey/src/KarplusString.cpp \
               modules/audrey/src/BiquadFilters.cpp \
               vendor/daisysp/dcblock.cpp \
               vendor/daisysp/tone.cpp \
               vendor/daisysp/crossfade.cpp \
               vendor/daisysp/overdrive.cpp \
               vendor/daisysp/reverbsc.cpp

# HOST_EXTRA passes build switches through, which is how the size/quality
# trade-offs get compared rather than argued about:
#   make audrey-host HOST_EXTRA="-DAUDREY_ECHO_MAX_S=3"
#   make audrey-host HOST_EXTRA="-DAUDREY_ECHO_DECIMATION=1 -DAUDREY_ECHO_Q15=0"
audrey-host:
	@mkdir -p $(BUILD_TMP)
	$(HOST_CXX) -std=c++14 -O2 -Wall -Wextra -Wno-unused-parameter \
	  -Ivendor/daisysp -Imodules/audrey/include $(HOST_EXTRA) \
	  $(AUDREY_SRCS) -o $(AUDREY_BIN)
	@$(AUDREY_BIN)

audrey-host-clean:
	rm -f $(AUDREY_BIN)

# ── Audrey echo A/B ──────────────────────────────────────────────────────────
# The two DSP risks M63f has been carrying since it landed, measured instead of
# argued: does the anti-alias filter catch the /4 fold-down, and does int16
# quantisation noise regenerate when echo feedback goes past unity.
#
# Exercises EchoDelay directly — no string, no reverb, no feedback loop — so
# nothing else can colour the answer. Builds are the comparison: these are
# compile-time switches by design, so `audrey-ab-sweep` rebuilds across the
# matrix and prints each configuration in turn.
.PHONY: audrey-ab audrey-ab-sweep audrey-ab-clean

AUDREY_AB_BIN  := $(BUILD_TMP)/audrey_ab$(EXE)
AUDREY_AB_SRCS := modules/audrey/test/echo_ab.cpp \
                  modules/audrey/src/BiquadFilters.cpp

# Same HOST_EXTRA convention as audrey-host:
#   make audrey-ab HOST_EXTRA="-DAUDREY_ECHO_ANTIALIAS=0"
#   make audrey-ab HOST_EXTRA="-DAUDREY_ECHO_Q15=0"
audrey-ab:
	@mkdir -p $(BUILD_TMP)
	@$(HOST_CXX) -std=c++14 -O2 -Wall -Wextra -Wno-unused-parameter \
	  -Ivendor/daisysp -Imodules/audrey/include $(HOST_EXTRA) \
	  $(AUDREY_AB_SRCS) -o $(AUDREY_AB_BIN)
	@$(AUDREY_AB_BIN)

# The whole matrix, in the order that makes the argument:
#   1. /1 float          the reference — no decimation, no quantiser
#   2. /4, AA off        what fold-down looks like unmitigated
#   3. /4, AA on         what the filter buys  (= the shipping alias behaviour)
#   4. /4 int16, shaping off   the quantiser without its error feedback
#   5. shipping default        everything on
audrey-ab-sweep:
	@$(MAKE) --no-print-directory audrey-ab \
	  HOST_EXTRA="-DAUDREY_ECHO_DECIMATION=1 -DAUDREY_ECHO_Q15=0"
	@$(MAKE) --no-print-directory audrey-ab \
	  HOST_EXTRA="-DAUDREY_ECHO_Q15=0 -DAUDREY_ECHO_ANTIALIAS=0"
	@$(MAKE) --no-print-directory audrey-ab \
	  HOST_EXTRA="-DAUDREY_ECHO_Q15=0"
	@$(MAKE) --no-print-directory audrey-ab \
	  HOST_EXTRA="-DAUDREY_ECHO_NOISE_SHAPE=0"
	@$(MAKE) --no-print-directory audrey-ab

audrey-ab-clean:
	rm -f $(AUDREY_AB_BIN)

# ── Panels ───────────────────────────────────────────────────────────────────
# Rack's SVG parser (nanosvg) renders <path> and nothing else — it does not
# understand <text>, so a label typed in Inkscape simply does not appear. The
# fix is object-to-path before export, and doing it by hand is exactly the step
# that gets forgotten right before a release.
#
# So: edit `res/<Name>_src.svg` in Inkscape and leave text as text. The build
# converts it into `res/<Name>.svg`, which is what the plugin loads and what
# gets committed. Both files are tracked — the _src because it is the source,
# the output because a machine without Inkscape still has to be able to build.
#
# ⚠ Use plain `select-all`, NOT `select-all:all`. The docs make `:all` ("every
# object including groups") sound safer, but `object-to-path` then recurses into
# every group in the document and on a 270 KB panel it does not finish — killed
# after ten minutes. Plain `select-all` (documented as `no-groups`) does reach
# text inside layers and groups here: verified, 0 <text> left in the output.
#
# `export-plain-svg` drops the inkscape:/sodipodi: namespaces, which nanosvg
# ignores anyway — 272 KB in, 258 KB out.
#
# ⚠ `--batch-process` is not optional. Without it Inkscape writes the export and
# then keeps running with its GUI event loop alive, so make blocks forever on a
# job that already finished. It is intermittent enough to look like a slow
# conversion rather than a hang: small documents happened to exit on their own,
# a 390 KB panel did not.
#
# ⚠ And `>/dev/null 2>&1 </dev/null` — all three, not just stdout. Even with
# --batch-process, Inkscape leaves a process behind that inherits whatever file
# descriptors the recipe had. It writes GTK warnings to *stderr*, so redirecting
# only stdout leaves that orphan holding the write end of the build's output
# pipe — and a reader on that pipe blocks until every writer closes. The visible
# symptom is `make vcv` freezing *after* the panel is already converted, while
# `make panels` on its own appears fine because nothing was reading a pipe.
# Probed with a shell loop, not $(wildcard)/$(firstword) like the other tools in
# this file: the default install path is "C:/Program Files/Inkscape/...", and
# both of those functions split their arguments on whitespace — so the space
# turns one path into two and neither exists. Every use site quotes $(INKSCAPE)
# for the same reason.
INKSCAPE ?= $(shell for p in \
      "$$(command -v inkscape 2>/dev/null)" \
      "C:/Program Files/Inkscape/bin/inkscape.com" \
      "C:/Program Files (x86)/Inkscape/bin/inkscape.com" \
      $(foreach h,$(HOMEDIRS),"$(h)/AppData/Local/Programs/Inkscape/bin/inkscape.com") \
      ; do [ -n "$$p" ] && [ -x "$$p" ] && printf '%s' "$$p" && break; done)

# Guide layers stripped before conversion — `components` holds a shape per pot,
# jack and LED so widget coordinates can be read off the drawing, and Rack would
# happily render all of it on top of the finished panel. Keyed on the Inkscape
# layer label, which is the name you chose and will keep, rather than on the
# generated id.
PANEL_HIDE_LAYERS ?= components

# The action list, overridable so a variant can be tried without editing this
# file: make panels PANEL_ACTIONS="..."
PANEL_ACTIONS ?= select-all; object-to-path; export-plain-svg; export-filename:$@; export-do

# Sources live OUTSIDE vcv-plugin/res/ on purpose. vcv-plugin/Makefile ships the
# whole of res/ via `DISTRIBUTABLES += res`, so an editable _src sitting there was
# packaged into every .vcvplugin — several hundred KB of Inkscape working file per
# panel, in a release. Keeping them here means `make vcv-dist` cannot pick them up
# by accident rather than because someone remembered to exclude them.
PANEL_SRCDIR := panel-src
PANEL_OUTDIR := vcv-plugin/res

# Anchor the patsubst on both directories. A bare `%_src.svg` pattern captures
# the source directory into `%` as well, which quietly nests the output
# (vcv-plugin/res/panel-src/Foo.svg) instead of relocating it.
PANEL_SRC := $(wildcard $(PANEL_SRCDIR)/*_src.svg)
PANEL_OUT := $(patsubst $(PANEL_SRCDIR)/%_src.svg,$(PANEL_OUTDIR)/%.svg,$(PANEL_SRC))
PANEL_TMP := $(BUILD_TMP)/panels

.PHONY: panels panels-force panel-coords

# Print the true component positions from the master panel's `components` layer,
# ready to paste into platform/vcv/PanelLayout.h. Honours the ancestor transforms
# and the viewBox scale, which Rack's module-helper stub does not — see the
# warning at the top of PanelLayout.h.
panel-coords:
	@$(PYTHON) tools/panel_coords.py $(PANEL_SRCDIR)/AlloyPlatform.svg $(PANEL_HIDE_LAYERS)

panels: $(PANEL_OUT)

# Rebuild every panel regardless of timestamps — for when Inkscape's output
# changed under you (a version bump) rather than the source.
panels-force:
	rm -f $(PANEL_OUT)
	@$(MAKE) --no-print-directory panels

# Missing Inkscape is not an error: the converted SVG is committed, so only
# someone actually editing a panel needs the tool. Warn and use what is there.
$(PANEL_OUTDIR)/%.svg: $(PANEL_SRCDIR)/%_src.svg
ifeq ($(INKSCAPE),)
	@echo "  Inkscape not found - keeping the committed $@."
	@echo "  Install Inkscape, or set INKSCAPE=/path/to/inkscape.com, to rebuild"
	@echo "  it from $<. Text in the _src will not render in Rack until you do."
	@touch $@
else
	@echo "panel: $< -> $@"
	@mkdir -p $(PANEL_TMP)
	@$(PYTHON) tools/prep_panel.py $< $(PANEL_TMP)/$(notdir $<) $(PANEL_HIDE_LAYERS)
	@"$(INKSCAPE)" --batch-process $(PANEL_TMP)/$(notdir $<) \
	  --actions="$(PANEL_ACTIONS)" \
	  >/dev/null 2>&1 </dev/null
	@$(PYTHON) tools/prep_panel.py --check $@
endif

# ── VCV Rack plugin ──────────────────────────────────────────────────────────
# PlatformIO does NOT compile vcv-plugin/, so `make firmware` passing says
# nothing about the Rack port. Any edit under modules/alloyflux/ or
# platform/include/ has to be checked here too.
#
# install / dist come from Rack's plugin.mk, which vcv-plugin/Makefile includes.
.PHONY: vcv vcv-install vcv-dist vcv-clean print-plugins-dir

vcv: panels
	$(MAKE) -C vcv-plugin $(RACK_ARG)

vcv-install: panels
	$(MAKE) -C vcv-plugin install $(RACK_ARG)

vcv-dist: panels
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

# MODULE selects which module''s parameter map and SysEx signature the build
# targets — `make web MODULE=audrey`. See web-configurator/src/lib/activeModule.ts.
web: $(WEB)/node_modules
	cd $(WEB) && VITE_MODULE=$(MODULE) $(NPM) run build

web-dev: $(WEB)/node_modules
	cd $(WEB) && VITE_MODULE=$(MODULE) $(NPM) run dev

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
