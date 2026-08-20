RACK_DIR ?= ../../Rack-SDK

# Platform headers plus every module's headers and the shared VCV glue.
#
# Unlike the firmware, the plugin hosts all modules in one binary — Rack loads
# one .dll and offers both — so every module's include root is on the path at
# once. That works only because the modules share no header names; if two ever
# do, the first -I wins silently, which is the same trap `plugin.hpp` and
# `config_store.h` have already sprung. Module headers are named for their
# module (alloy_config.h, coil_config.h) precisely to keep that from
# happening. The exceptions are params.h and io/PanelMap.h, which DO collide —
# hence the per-object include order below rather than a shared list.
EXTRA_FLAGS += -I../platform/include
EXTRA_FLAGS += -I../platform/vcv
# -iquote, not -I: our plugin.hpp collides with Rack SDK's own include/plugin.hpp.
# A quoted #include searches -iquote directories before any -I directory, so this
# resolves "plugin.hpp" to ours while <plugin.hpp> still reaches Rack's. With a
# plain -I the SDK path wins (it is prepended by plugin.mk) and module sources
# compiled out-of-tree silently pick up the wrong header — which fails as
# "expected class-name before '{'" on every struct deriving from Module.
EXTRA_FLAGS += -iquote src

# DELAY_MAX_MS is no longer set here: 500 ms is the default in DelayEngine.h
# for every target as of M63g, so there is nothing left to keep in sync with
# platformio.ini.

# Plugin scaffold lives here; everything else belongs to a module and is added
# as an explicit object below (compile.mk's patsubst would otherwise resolve
# out-of-tree sources to vcv-plugin/src/*.o).
SOURCES += src/plugin.cpp

OBJECTS += build/AlloyFlux.cpp.o
OBJECTS += build/SynthEngine.cpp.o

OBJECTS += build/AlloyCoil.cpp.o
OBJECTS += build/alloycoil/FeedbackSynthEngine.cpp.o
OBJECTS += build/alloycoil/KarplusString.cpp.o
OBJECTS += build/alloycoil/BiquadFilters.cpp.o
OBJECTS += build/daisysp/dcblock.cpp.o
OBJECTS += build/daisysp/tone.cpp.o
OBJECTS += build/daisysp/crossfade.cpp.o
OBJECTS += build/daisysp/overdrive.cpp.o
OBJECTS += build/daisysp/reverbsc.cpp.o
OBJECTS += build/daisysp/limiter.cpp.o

# Add files to the ZIP package when running `make dist`
# The compiled plugin and "plugin.json" are automatically added.
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += $(wildcard presets)

include $(RACK_DIR)/plugin.mk

# Rack SDK injects a Clang-only warning flag that GCC (MSYS2 g++) does not recognize.
CXXFLAGS := $(filter-out -Wno-vla-extension,$(CXXFLAGS))

# Out-of-tree compile rules — use CXXFLAGS fully expanded by compile.mk.
#
# Each module's include root is added per-object rather than globally: both
# modules ship a `params.h` and an `io/PanelMap.h`, so a shared -I list would
# hand one module the other's globals with no diagnostic beyond a link error
# much later.
ALLOYFLUX_INC := -I../modules/alloyflux/include

# ReverbSc sizes its delay pool from the highest sample rate the build must
# support, and the plugin — unlike the firmware, which is pinned to 48 kHz —
# re-inits at whatever Rack is set to. Rack offers up to 192 kHz, and at
# anything above ~48.1 kHz a 48 kHz-sized pool cannot hold all eight delay
# lines. It costs 385 KiB of the plugin's own memory, which is free on a
# desktop and would be most of an RP2350.
COIL_INC    := -I../modules/alloycoil/include -I../vendor/daisysp \
               -DDSY_REVERBSC_MAX_SRATE=192000

build/SynthEngine.cpp.o: ../modules/alloyflux/src/SynthEngine.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(ALLOYFLUX_INC) -c -o $@ $<

build/AlloyFlux.cpp.o: ../modules/alloyflux/vcv/AlloyFlux.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(ALLOYFLUX_INC) -c -o $@ $<

build/AlloyCoil.cpp.o: ../modules/alloycoil/vcv/AlloyCoil.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(COIL_INC) -c -o $@ $<

build/alloycoil/%.cpp.o: ../modules/alloycoil/src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(COIL_INC) -c -o $@ $<

build/daisysp/%.cpp.o: ../vendor/daisysp/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(COIL_INC) -c -o $@ $<

# Header dependency tracking for the objects above.
#
# compile.mk derives its own list from SOURCES:
#     DEPENDENCIES := $(patsubst %, build/%.d, $(SOURCES))
# and everything here is in OBJECTS instead — deliberately, because compile.mk's
# patsubst would resolve out-of-tree sources to vcv-plugin/src/*.o. The .d files
# were therefore being written by -MMD and then ignored, so editing a shared
# header rebuilt nothing and `make vcv` cheerfully relinked stale objects. That
# is worse than a compile error: the plugin builds, installs, and runs the code
# you thought you had changed.
-include $(OBJECTS:.o=.d)

# Where Rack loads user plugins from. plugin.mk works this out per platform
# (LOCALAPPDATA on Windows, XDG on Linux, Application Support on macOS) and
# appends the arch; the root Makefile asks for it rather than guessing.
.PHONY: print-plugins-dir
print-plugins-dir:
	@echo "$(PLUGINS_DIR)"
