# AlloyFlux build wrapper.
#
# Three build targets share one DSP codebase (see AGENTS.md):
#
#   firmware  RP2350 / Pico 2, PlatformIO + Arduino-Pico          (./platformio.ini)
#   vcv       VCV Rack 2 plugin, Rack SDK                         (./vcv-plugin)
#   web       Alloy Controller, Svelte 5 + Vite                   (./web-configurator)
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
ifneq ($(filter vcv vcv-% coil-host coil-ab coil-ab-% everything,$(or $(MAKECMDGOALS),all)),)
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

# Host C++ compiler, used only by `make coil-host`. On Windows the reliable
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
NPM ?= npm

# Rack SDK checkout. vcv-plugin/Makefile defaults to ../../Rack-SDK, i.e. a
# sibling of this repository, so normally nothing needs setting. Override with
# an ABSOLUTE path if it lives elsewhere — a relative one would be resolved
# from vcv-plugin/, not from here.
#     make RACK_DIR=D:/Rack-SDK vcv
RACK_ARG = $(if $(RACK_DIR),RACK_DIR=$(RACK_DIR),)

WEB := web-configurator

.DEFAULT_GOAL := all
.PHONY: all everything help

# ── Modules ──────────────────────────────────────────────────────────────────
# The platform hosts one engine per firmware image, so each module is its own
# PlatformIO env, and the env is named after the module. The VCV plugin is the
# opposite — one .dll carries every module and Rack offers them all — and the
# controller is module-agnostic entirely, so neither has anything per-module to
# build.
#
# MODULE picks which one, everywhere it can mean anything: the firmware image,
# the params tables, the host-side checks. It used to share that job with ENV,
# which named the PlatformIO env — the same two values under two spellings, so
# `make upload ENV=alloycoil` and `make params MODULE=alloycoil` disagreed about
# what to call the same thing.
MODULE ?= alloyflux
MODULES := alloyflux alloycoil

# WIRELESS=1 builds the Pico 2W image of the same module: the identical engine
# at the same rate from the same sources, with BLE MIDI added (M78).
#
# A separate knob rather than another MODULE value, deliberately. MODULE names
# the module — its params tables, its sources, its host checks — and a 2W build
# is not a different module, it is the same one on a different board. Folding
# it into MODULE would mean `make params MODULE=alloyflux_wired` looking for a
# params.json that should never exist.
#
# Only AlloyFlux has a wireless env. Alloy Coil sits at 91% RAM after M63i and
# has nothing to give BTstack, so `make firmware MODULE=alloycoil WIRELESS=1`
# fails in PlatformIO with "unknown environment" — the honest error.
#
# Wireless is the default on BOTH modules, and the PlatformIO envs are named to
# match: the plain names (alloyflux, alloycoil) are the 2W builds and the
# non-radio ones carry the _wired suffix. The suffix marks the exception, so
# the IDE buttons, a bare `pio run` and make all produce the shipping image
# without anyone having to remember a flag to get it.
#
WIRELESS ?= 1

# ── Echo experiments (M78f) ──────────────────────────────────────────────────
# Sweep Alloy Coil's echo ceiling and decimation without editing platformio.ini,
# so a session of A/B leaves no diff behind:
#
#     make upload MODULE=alloycoil WIRELESS=1 ECHO_S=2 ECHO_DECIM=4
#     make firmware MODULE=alloycoil ECHO_DECIM=8
#
# Storage is exactly 48000/DECIM x 2 bytes x 2 channels x SECONDS, i.e. linear
# in both — /4 at 4 s is 192 000 B, /6 at 4 s is 128 000 B, /8 at 4 s is 96 000.
# Watch the RAM figure after the build: if it did not move, the override did not
# take, and you are measuring the old configuration.
#
# These reach the compiler through ${sysenv.ALLOY_FLAGS}, which is the LAST entry
# in each Coil env's build_flags. PLATFORMIO_BUILD_FLAGS cannot do this job —
# PlatformIO prepends it, so the env's own -D wins and the override is silently
# ignored.
#
# No -U: PlatformIO sorts -U into a LATER flag group than -D, so -UX -DX=2`r
# reaches the compiler as -DX=4 -DX=2 ... -UX and the undef wins — leaving the
# header's #ifndef default in force and the build silently unchanged. A plain
# -D is enough, because the later definition on the line is the one kept.
ALLOY_FLAGS :=
ifneq ($(ECHO_S),)
ALLOY_FLAGS += -DCOIL_ECHO_MAX_S=$(ECHO_S)
endif
ifneq ($(ECHO_DECIM),)
ALLOY_FLAGS += -DCOIL_ECHO_DECIMATION=$(ECHO_DECIM)
endif
# DMA_BUF=n — I2S output queue depth, n x 32 frames. 4 = 2.7 ms (default),
# 8 = 5.3 ms, 12 = 8.0 ms. Buys ride-through for stalls, costs output latency.
# See AUDIO_DMA_BUFFERS in platform/include/io/AudioDriver.h.
ifneq ($(DMA_BUF),)
ALLOY_FLAGS += -DAUDIO_DMA_BUFFERS=$(DMA_BUF)
endif
export ALLOY_FLAGS

ifeq ($(WIRELESS),1)
FW_ENV := $(MODULE)
else
FW_ENV := $(MODULE)_wired
endif

# `all` is the firmware you flash — AlloyFlux unless MODULE says otherwise.
all: firmware

# Everything a shared change can break. Both firmware images, because a change
# under platform/ compiles differently against each module. The controller is
# built once: one bundle drives every module, detected at runtime.
everything: firmware-all vcv web

# Every image a shared change can break, named as PlatformIO envs rather than
# as modules. The wireless build is a third image from the same sources, and
# the old MODULES loop never covered it — so an edit under platform/ could
# break it with nothing in the repo noticing.
#
# All four are in the list. The wireless Coil is the one a careless change breaks
# It sits at 94.3% of SRAM, so it is the env a careless change breaks first —
# which makes it the most useful one to have compiling here, not the least. If
# it starts failing, that is the answer to M78f arriving early rather than a
# broken build target.
FW_ENVS := alloyflux alloyflux_wired alloycoil alloycoil_wired

.PHONY: firmware-all
firmware-all:
	@for e in $(FW_ENVS); do \
	  echo ""; echo "--- firmware: $$e ---"; \
	  $(PIO) run -e $$e || exit 1; \
	done

help:
	@echo ""
	@echo "Alloy Platform - make targets"
	@echo ""
	@echo "  Modules: $(MODULES)"
	@echo "    MODULE=<module>   picks the firmware image and the params"
	@echo "                      tables                  (current: $(MODULE))"
	@echo "    WIRELESS=0|1      Pico 2W image with BLE MIDI. DEFAULT is 1 on"
	@echo "                      both modules; WIRELESS=0 gives the _wired env."
	@echo "                      Currently building: $(FW_ENV)"
	@echo ""
	@echo "  Firmware (RP2350, one image per module)"
	@echo "    firmware          build firmware.uf2                  (default)"
	@echo "    firmware-all      build all four envs (both modules, both boards)"
	@echo "    upload            build and flash over USB"
	@echo "    upload-monitor    flash, then open the serial monitor"
	@echo "    monitor           serial monitor only"
	@echo "    test              every host suite, then the PlatformIO ones"
	@echo "    firmware-clean    clean the PlatformIO build"
	@echo "      e.g.  make upload MODULE=alloycoil"
	@echo "      e.g.  make upload WIRELESS=0   (Alloy Flux on a plain Pico 2)"
	@echo ""
	@echo "  VCV Rack plugin (ONE plugin, all modules in it)"
	@echo "    vcv               build vcv-plugin/plugin.dll"
	@echo "    vcv-install       build and install into Rack's user plugin dir"
	@echo "    vcv-dist          package the .vcvplugin for the VCV library"
	@echo "    vcv-clean         clean the plugin build"
	@echo "    print-plugins-dir print Rack's user plugin directory"
	@echo ""
	@echo "  Alloy Controller (Svelte + Vite, one build drives every module)"
	@echo "    web               production build into $(WEB)/dist"
	@echo "    web-dev           vite dev server on localhost:5173"
	@echo "    web-host          same over HTTPS, on the LAN, for phone testing"
	@echo "                      (self-signed cert - accept it once on the phone)"
	@echo "    web-check         svelte-check + tsc type validation"
	@echo "    web-deps          npm install"
	@echo "    web-clean         remove dist/ and node_modules/"
	@echo "      (no MODULE — the controller detects what is on the port)"
	@echo ""
	@echo "  Parameters"
	@echo "    params            regenerate tables from modules/\$$(MODULE)/params.json"
	@echo "    params-check      CI gate: fail if the committed tables are stale"
	@echo ""
	@echo "  Tests (host builds, no board needed — 'make test' runs them all)"
	@echo "    flux-cloud      Alloy Flux: CLOUD oscillator pool + levels"
	@echo "    flux-filter     Alloy Flux: SVF + OTA ladder response"
	@echo "    coil-params     Alloy Coil: IOBridge knobs match params.json"
	@echo "    coil-host       Alloy Coil: run the engine, print its footprint"
	@echo ""
	@echo "  Measurements (print numbers to weigh; they cannot fail)"
	@echo "    flux-levels     Alloy Flux: output level of every voice mode"
	@echo "    coil-ab         measure echo aliasing + quantiser noise"
	@echo "    coil-ab-sweep   the same, across the whole switch matrix"
	@echo ""
	@echo "  Across the repo"
	@echo "    everything        every firmware image + vcv + the web build"
	@echo "    format            clang-format every tracked C/C++ file"
	@echo "    format-check      verify formatting without writing (CI gate)"
	@echo "    clean             all three clean targets"
	@echo ""

# ── Parameters ───────────────────────────────────────────────────────────────
# A module declares its parameters once, in modules/<name>/params.json. This
# regenerates every table derived from it. The output is committed, so an
# ordinary build never needs Python — only editing params.json does.
.PHONY: params params-check

params:
	$(PYTHON) tools/gen_params.py modules/$(MODULE)

# CI gate: regenerates, then fails if that produced a diff — i.e. the committed
# tables do not match params.json. On a clean checkout (CI) that is exactly the
# staleness check. In a dirty working tree it will also fire on generated
# changes you have made but not yet committed, which is the same instruction:
# commit them alongside the params.json edit that caused them.
#
# The paths come from the generator itself (--list-outputs) rather than being
# listed here: this gate used to name web-configurator/src/lib/paramMap.ts,
# which is the hand-written shim and not something `params` ever writes, so a
# stale paramMapAlloyCoil.ts sailed straight through it.
#
# `git status`, not `git diff`: a generated file that is new — as
# param_globals.generated.h was — is untracked rather than modified, and a
# plain diff reports nothing at all for it.
#
# One of the outputs, MANUAL.md, is only PARTLY generated: `make params`
# rewrites the CC-map table between its markers and leaves the surrounding
# prose alone. On a clean checkout that distinction does not matter, but in a
# dirty tree this gate will also fire on hand edits to the manual's prose. The
# fix is the same either way — commit them.
params-check: params
	@outs=$$($(PYTHON) tools/gen_params.py modules/$(MODULE) --list-outputs); \
	 if [ -n "$$(git status --porcelain -- $$outs)" ]; then \
	   git status --short -- $$outs; \
	   git diff -- $$outs; \
	   echo "Generated parameter tables differ from what is committed."; \
	   echo "If you just edited params.json, commit the regenerated files too."; \
	   exit 1; \
	 fi

# ── Firmware ─────────────────────────────────────────────────────────────────
# One PlatformIO environment per module: the RP2350 image. See platformio.ini.
.PHONY: firmware upload upload-monitor monitor firmware-clean

firmware:
	$(PIO) run -e $(FW_ENV)

upload:
	$(PIO) run -e $(FW_ENV) -t upload

upload-monitor:
	$(PIO) run -e $(FW_ENV) -t upload -t monitor

monitor:
	$(PIO) device monitor

firmware-clean:
	$(PIO) run -e $(FW_ENV) -t clean

# ── Tests ────────────────────────────────────────────────────────────────────
# Every pass/fail suite in the repo, and one target that runs them all.
#
# They are host builds, not firmware: each compiles the DSP it covers against
# nothing but the standard library, runs it, and exits non-zero if an assertion
# fails. No board, no flashing, seconds to run — which is the only reason they
# get run at all.
#
# What lives here is what *asserts*. The measurement tools that print numbers
# for a human to weigh — coil-ab and coil-ab-sweep — are deliberately not in
# this section and not in `test`: they always exit 0, because there is nothing
# for them to fail.
#
#   make test           every suite below, then the PlatformIO ones
#   make flux-cloud     just that suite (they all still run individually)
#
# ⚠ A host build cannot see a cross-core race. control() and audio() run on
# separate cores on hardware but sequentially here, so anything that depends on
# the two interleaving has to be tested through its *structure* instead — see
# the buffer-ownership check in cloud_pool.cpp for what that looks like.
.PHONY: test flux-cloud flux-filter coil-host coil-host-clean coil-params

# Suites, in ascending order of runtime so a failure surfaces fast.
HOST_TESTS := coil-params flux-filter flux-cloud coil-host

# `pio test` fails outright ("Nothing to build. Please put your test suites to
# the 'test' folder") when test/ holds only its README, which is the case
# today. Ask the filesystem rather than hardcoding it, so this starts working
# the moment a suite is added.
TEST_SUITES := $(wildcard test/test_*)

test: $(HOST_TESTS)
	@echo ""
	@echo "host suites passed: $(HOST_TESTS)"
ifeq ($(TEST_SUITES),)
	@echo "no PlatformIO suites under test/ - add test/test_<name>/ to enable them."
else
	$(PIO) test -e $(MODULE)
endif

# --- Alloy Flux: filter response --------------------------------------------
# The OTA ladder shipped with the (1-G) stage factor missing from its ZDF
# feedback predictor, which tied resonance to cutoff: at res 0.8 the peak ran
# -0.5 dB at 100 Hz and +14.4 dB at 4 kHz, past self-oscillation over the top
# half of the cutoff range. Every single setting still sounded like a filter,
# so only a property held *across* settings catches it. That is what this
# asserts: the response shape must not move with cutoff, or with input level
# while the tanh is still linear.
#
# Header-only -- no engine to link, so it is the cheapest suite here and runs
# first. Worth running on any edit to OTALadder.h or SVFFilter.h.
FLUX_FILTER_BIN := $(BUILD_TMP)/flux_filter$(EXE)

flux-filter:
	@mkdir -p $(BUILD_TMP)
	@$(HOST_CXX) -std=c++14 -O2 -Wall -Wextra -Wno-unused-parameter \
	  -Imodules/alloyflux/include -Iplatform/include $(HOST_EXTRA) \
	  modules/alloyflux/test/ladder_response.cpp \
	  -o $(FLUX_FILTER_BIN)
	@$(FLUX_FILTER_BIN)

# --- Alloy Flux: CLOUD oscillator pool ---------------------------------------
# M78 made CLOUD polyphonic by sharing one oscillator pool across held notes,
# and the pool's correctness is invisible in the audio: a stack left a saw
# short, or an oscillator handed to two notes at once, sounds like a slightly
# different supersaw rather than like a fault. So the plan is asserted on
# directly, alongside sample-step checks that the drone stopping and the stacks
# narrowing are fades rather than steps, and level checks that a chord never
# reaches the rail while the drone stays out of the saturator.
#
# Cheap enough to run on every edit to the CLOUD case in SynthEngine.cpp.
FLUX_CLOUD_BIN := $(BUILD_TMP)/flux_cloud$(EXE)

flux-cloud:
	@mkdir -p $(BUILD_TMP)
	@$(HOST_CXX) -std=c++14 -O2 -Wall -Wextra -Wno-unused-parameter \
	  -Imodules/alloyflux/include -Iplatform/include $(HOST_EXTRA) \
	  modules/alloyflux/test/cloud_pool.cpp modules/alloyflux/src/SynthEngine.cpp \
	  -o $(FLUX_CLOUD_BIN)
	@$(FLUX_CLOUD_BIN)

# --- Alloy Coil: knob/CC agreement -------------------------------------------
# params.json is the source of truth, but io/IOBridge.h is a hand-written mirror
# of it — what a knob and a CV actually do — and nothing was checking the two
# agreed. M63i changed two curves and every hand-computed number derived from
# the old ones had to be found by eye; one was missed, and the VCV module booted
# with its echo at 1.14 s instead of 0.5 s.
#
# Walks every knob across its travel and compares IOBridge against
# ParamDescriptor::fromPos(). Cheap enough to run on every params.json edit.
COIL_PARAMS_BIN := $(BUILD_TMP)/coil_params$(EXE)

coil-params:
	@mkdir -p $(BUILD_TMP)
	@$(HOST_CXX) -std=c++14 -O2 -Wall -Wextra -Wno-unused-parameter \
	  -Iplatform/include -Imodules/alloycoil/include -Ivendor/daisysp $(HOST_EXTRA) \
	  modules/alloycoil/test/params_check.cpp -o $(COIL_PARAMS_BIN)
	@$(COIL_PARAMS_BIN)

# --- Alloy Coil: engine host build -------------------------------------------
# Compiles the vendored Audrey II engine (M63e) against nothing but the vendored
# DaisySP subset and the standard library — no Daisy headers, no SDRAM
# allocator, no heap. Then runs it for 10 s at maximum feedback and echo
# feedback above unity, checking for NaN, silence and DC drift, and prints the
# static footprint of each big member.
#
# That footprint is the point: it is what decides whether the engine can fit a
# 520 KB RP2350 at all, and it costs nothing to learn here rather than on a
# flashed board. It is a test as well as a report — non-finite output, silence
# or DC drift all exit non-zero — which is why it belongs in `test`.
COIL_BIN := $(BUILD_TMP)/coil_host$(EXE)

# The three engine sources, named rather than globbed. modules/alloycoil/src/ also
# holds the firmware integration (main, hooks, commands, config, param_map),
# which needs Arduino and the platform headers and has no business in a host
# build — this harness exercises the DSP, not the module.
COIL_SRCS := modules/alloycoil/test/host_build.cpp \
               modules/alloycoil/src/FeedbackSynthEngine.cpp \
               modules/alloycoil/src/KarplusString.cpp \
               modules/alloycoil/src/BiquadFilters.cpp \
               vendor/daisysp/dcblock.cpp \
               vendor/daisysp/tone.cpp \
               vendor/daisysp/crossfade.cpp \
               vendor/daisysp/overdrive.cpp \
               vendor/daisysp/reverbsc.cpp \
               vendor/daisysp/limiter.cpp

# HOST_EXTRA passes build switches through, which is how the size/quality
# trade-offs get compared rather than argued about:
#   make coil-host HOST_EXTRA="-DCOIL_ECHO_MAX_S=3"
#   make coil-host HOST_EXTRA="-DCOIL_ECHO_DECIMATION=1 -DCOIL_ECHO_Q15=0"
coil-host:
	@mkdir -p $(BUILD_TMP)
	$(HOST_CXX) -std=c++14 -O2 -Wall -Wextra -Wno-unused-parameter \
	  -Ivendor/daisysp -Imodules/alloycoil/include $(HOST_EXTRA) \
	  $(COIL_SRCS) -o $(COIL_BIN)
	@$(COIL_BIN)

coil-host-clean:
	rm -f $(COIL_BIN)

# ── Alloy Flux mode levels ───────────────────────────────────────────────────
# Renders every voice mode on the same patch and prints RMS, peak and dB
# relative to PAIR. A measurement, not a test — there is no correct answer for
# it to assert, only numbers to weigh.
#
# It exists because "mode X sounds quieter" is otherwise unanswerable by ear.
# The distinction it makes is the useful one: CLOUD peaks exactly where every
# other mode peaks and is 7 dB down in *RMS*, which is crest factor rather than
# gain — seven near-coincident voices spend most of the cycle near zero. That
# is also why raising its level mostly feeds the saturator instead of adding
# loudness. See kCloudStackLevel.
.PHONY: flux-levels

FLUX_LEVELS_BIN := $(BUILD_TMP)/flux_levels$(EXE)

flux-levels:
	@mkdir -p $(BUILD_TMP)
	@$(HOST_CXX) -std=c++14 -O2 -w \
	  -Imodules/alloyflux/include -Iplatform/include $(HOST_EXTRA) \
	  modules/alloyflux/test/mode_levels.cpp modules/alloyflux/src/SynthEngine.cpp \
	  -o $(FLUX_LEVELS_BIN)
	@$(FLUX_LEVELS_BIN)

# ── Alloy Coil echo A/B ──────────────────────────────────────────────────────────
# The two DSP risks M63f has been carrying since it landed, measured instead of
# argued: does the anti-alias filter catch the /4 fold-down, and does int16
# quantisation noise regenerate when echo feedback goes past unity.
#
# Exercises EchoDelay directly — no string, no reverb, no feedback loop — so
# nothing else can colour the answer. Builds are the comparison: these are
# compile-time switches by design, so `coil-ab-sweep` rebuilds across the
# matrix and prints each configuration in turn.
.PHONY: coil-ab coil-ab-sweep coil-ab-clean

COIL_AB_BIN  := $(BUILD_TMP)/coil_ab$(EXE)
COIL_AB_SRCS := modules/alloycoil/test/echo_ab.cpp \
                  modules/alloycoil/src/BiquadFilters.cpp

# Same HOST_EXTRA convention as coil-host:
#   make coil-ab HOST_EXTRA="-DCOIL_ECHO_ANTIALIAS=0"
#   make coil-ab HOST_EXTRA="-DCOIL_ECHO_Q15=0"
coil-ab:
	@mkdir -p $(BUILD_TMP)
	@$(HOST_CXX) -std=c++14 -O2 -Wall -Wextra -Wno-unused-parameter \
	  -Ivendor/daisysp -Imodules/alloycoil/include $(HOST_EXTRA) \
	  $(COIL_AB_SRCS) -o $(COIL_AB_BIN)
	@$(COIL_AB_BIN)

# The whole matrix, in the order that makes the argument:
#   1. /1 float          the reference — no decimation, no quantiser
#   2. /4, AA off        what fold-down looks like unmitigated
#   3. /4, AA on         what the filter buys  (= the shipping alias behaviour)
#   4. /4 int16, shaping off   the quantiser without its error feedback
#   5. shipping default        everything on
coil-ab-sweep:
	@$(MAKE) --no-print-directory coil-ab \
	  HOST_EXTRA="-DCOIL_ECHO_DECIMATION=1 -DCOIL_ECHO_Q15=0"
	@$(MAKE) --no-print-directory coil-ab \
	  HOST_EXTRA="-DCOIL_ECHO_Q15=0 -DCOIL_ECHO_ANTIALIAS=0"
	@$(MAKE) --no-print-directory coil-ab \
	  HOST_EXTRA="-DCOIL_ECHO_Q15=0"
	@$(MAKE) --no-print-directory coil-ab \
	  HOST_EXTRA="-DCOIL_ECHO_NOISE_SHAPE=0"
	@$(MAKE) --no-print-directory coil-ab

coil-ab-clean:
	rm -f $(COIL_AB_BIN)

# ── Panel LED shape ──────────────────────────────────────────────────────────
# The LED apertures are drawn on the panel's F.Mask layer (the panel is a PCB,
# and the light comes through the bare substrate from behind). Rack has to fill
# the same outline to match, so it is generated from the artwork rather than
# transcribed — a 1200-character bezier is not something to retype, and an
# eyeballed approximation would drift the moment the panel is edited.
#
# All seven apertures are the same shape, so one path serves the lot; LED1 is
# simply the one it is taken from. Committed, so a normal build needs no Python.
.PHONY: led-shape

led-shape:
	$(PYTHON) tools/svg_path_to_nvg.py \
	  $(PANEL_SRCDIR)/AlloyCoil_src.svg LED1 \
	  platform/vcv/PanelLedShape.generated.h AlloyPanelLed \
	  $(PANEL_SRCDIR)/AlloyFlux_src.svg

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
# ⚠ Never widen the selection to `select-all:all`. The docs make `:all` ("every
# object including groups") sound safer, but `object-to-path` then recurses into
# every group in the document and on a 270 KB panel it does not finish — killed
# after ten minutes. The selection is narrower still now — see PANEL_ACTIONS,
# which asks for text and nothing else, because text is the only thing here that
# needs Inkscape at all.
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
PANEL_HIDE_LAYERS ?= components Drill

# The action list, overridable so a variant can be tried without editing this
# file: make panels PANEL_ACTIONS="..."
#
# Text only. Inkscape is here for one thing nothing else can do — turning glyphs
# into outlines needs the font metrics — and every other conversion it was being
# asked for was either unnecessary or ruinous:
#
#   rect/circle/ellipse/line/polyline/polygon  nanosvg draws these natively.
#       Converting them to paths was work with no effect on what Rack renders.
#   <use>  nanosvg cannot draw it, so it does have to go — but `object-to-path`
#       unlinks one clone per document rebuild, and a tiled-clone texture of
#       1927 logos blew straight through the five-minute wall clock. It is a
#       deep copy per clone, so prep_panel.py does it in well under a second.
#
# `select-by-element` needs Inkscape 1.1+. On an older build, fall back with
#   make panels PANEL_ACTIONS="select-all; object-to-path; export-plain-svg; export-filename:$@; export-do"
PANEL_ACTIONS ?= select-by-element:text; object-to-path; export-plain-svg; export-filename:$@; export-do

# ⚠ Give the build its own Inkscape application ID. This is what makes the
# panel step safe to run while you have Inkscape open, and it is not optional.
#
# Inkscape is a GApplication with a fixed ID, `org.inkscape.Inkscape`. The first
# process to claim that ID becomes the primary; every later `inkscape` command
# becomes a *remote* instance that forwards its arguments to the primary and
# then waits for it. So without a tag, `make panels` does not run Inkscape — it
# asks whatever Inkscape already exists to do the work, and blocks. If that is
# your editor, the actions run in your editing session; if it is a wedged batch
# process from an earlier build, nothing happens at all and make waits forever.
#
# It gets worse: `--batch-process` is documented as "Close GUI after executing
# all actions". Forwarded into your open editor, that is an instruction to run
# select-all + object-to-path on whatever you have open and then close it.
#
# `--app-id-tag=TAG` gives this invocation the ID `org.inkscape.Inkscape.TAG`,
# so it always starts its own private instance, never attaches to the GUI, and
# cannot be held up by an orphan of the untagged one.
PANEL_APP_TAG ?= alloybuild

# ⚠ Hard wall-clock limit on Inkscape, because every other guard here stops a
# *known* hang and this one stops the unknown next one.
#
# Inkscape on Windows is single-instance: a second invocation hands its work to
# the first and waits. So one wedged batch process does not just fail its own
# build, it wedges every build after it — observed as `make vcv-install`
# printing "panel: ..." and sitting there while an orphan from twenty minutes
# earlier span at 8% CPU holding the lock. Nothing times out, because make is
# waiting on a process that is technically alive.
#
# 180 s is generous: a 270 KB panel converts in under ten seconds. Exceeding it
# means something is wrong, and a failed build that says so beats a hung one.
#
# Must be MSYS2's timeout, by absolute path. `timeout` on PATH under Windows
# resolves to C:/Windows/system32/timeout.exe, which is an unrelated tool that
# waits for a keypress and rejects these arguments outright.
PANEL_TIMEOUT_S ?= 180

# Resolved with $(wildcard), not a shell loop. A `case` statement cannot live
# inside $(shell ...) — its `)` closes the make function call, and the result is
# a syntax error from sh plus an empty variable. And $(wildcard)'s whitespace
# splitting, which rules it out for the Inkscape probe above ("C:/Program
# Files/..."), is harmless here because none of these paths contain a space.
#
# PATH is deliberately not consulted: `timeout` there resolves to
# C:/Windows/system32/timeout.exe, an unrelated tool that waits for a keypress.
PANEL_TIMEOUT ?= $(firstword $(wildcard \
      $(MSYS)/usr/bin/timeout.exe \
      C:/msys64/usr/bin/timeout.exe \
      /usr/bin/timeout \
      /opt/homebrew/bin/gtimeout))

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
PANEL_MASTER ?= $(PANEL_SRCDIR)/AlloyCoil_src.svg

panel-coords:
	@$(PYTHON) tools/panel_coords.py $(PANEL_MASTER) $(PANEL_HIDE_LAYERS)

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
	@$(if $(PANEL_TIMEOUT),"$(PANEL_TIMEOUT)" -k 5 $(PANEL_TIMEOUT_S),) \
	  "$(INKSCAPE)" --app-id-tag=$(PANEL_APP_TAG) \
	  --batch-process $(PANEL_TMP)/$(notdir $<) \
	  --actions="$(PANEL_ACTIONS)" \
	  >/dev/null 2>&1 </dev/null \
	  || { echo "  ERROR: Inkscape failed or exceeded $(PANEL_TIMEOUT_S)s."; \
	       echo "  The build runs Inkscape under its own application ID"; \
	       echo "  (org.inkscape.Inkscape.$(PANEL_APP_TAG)), so your open editor is"; \
	       echo "  not the cause. Look for a stray process with that tag:"; \
	       echo "      Get-Process inkscape | Stop-Process -Force"; \
	       exit 1; }
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

# ── Alloy Controller ─────────────────────────────────────────────────────────
# Chrome or Edge only at runtime — Web Serial and Web MIDI are not implemented
# in Firefox or Safari.
#
# The build targets bootstrap node_modules on first run so a fresh clone works
# with a single `make web`.
.PHONY: web web-dev web-host web-check web-deps web-clean

$(WEB)/node_modules:
	cd $(WEB) && $(NPM) install

web-deps:
	cd $(WEB) && $(NPM) install

# One build serves every module and takes no MODULE: the page detects which one
# is on the MIDI port from its SysEx signature and loads that parameter map,
# remembering the last one it saw across sessions. There used to be a
# VITE_MODULE that picked what a browser which had never connected to anything
# opened on; it is gone, and that case now falls back to AlloyFlux.
# See web-configurator/src/lib/activeModule.ts.
web: $(WEB)/node_modules
	cd $(WEB) && $(NPM) run build

web-dev: $(WEB)/node_modules
	cd $(WEB) && $(NPM) run dev

# Exposes the dev server on the LAN so a phone can reach it, over HTTPS.
#
# The HTTPS is not optional decoration. Web Bluetooth, Web MIDI and Web Serial
# are secure-context APIs, and over plain HTTP on a LAN IP the browser does not
# refuse them — it does not *define* them, so every feature-detect on the page
# reports "not supported" on an Android Chrome that supports them perfectly
# well. vite.config.ts turns on basic-ssl when it sees --host for exactly this,
# and leaves `make web-dev` alone because localhost is already secure.
#
# The certificate is self-signed, so the phone shows an interstitial once.
web-host: $(WEB)/node_modules
	@echo ""
	@echo "NOTE: self-signed certificate - accept the browser warning once on the"
	@echo "      phone. https:// is required: Web Bluetooth and Web MIDI do not"
	@echo "      exist on a plain-HTTP origin, and the page will say 'Needs HTTPS'."
	@echo ""
	cd $(WEB) && $(NPM) run dev -- --host

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
#
# ":(exclude)~/" is not paranoia. A tool that expands ~/... itself leaves a
# literal "~" directory in the repo root, and an in-place format of a stray
# toolchain checkout is not something to discover afterwards. .gitignore covers
# it too; this makes the sweep safe even in a tree whose ignores are stale.
FORMAT_FILES = git ls-files --cached --others --exclude-standard \
    "*.h" "*.hpp" "*.c" "*.cc" "*.cpp" \
    ":(exclude)vendor/" ":(exclude)modules/alloycoil/" ":(exclude)~/"

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
