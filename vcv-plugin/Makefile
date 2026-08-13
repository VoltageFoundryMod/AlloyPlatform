RACK_DIR ?= ../../Rack-SDK

# Platform headers plus the active module's headers and VCV glue.
EXTRA_FLAGS += -I../platform/include
EXTRA_FLAGS += -I../modules/alloyflux/include
EXTRA_FLAGS += -I../modules/alloyflux/vcv
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

# Add files to the ZIP package when running `make dist`
# The compiled plugin and "plugin.json" are automatically added.
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += $(wildcard presets)

include $(RACK_DIR)/plugin.mk

# Rack SDK injects a Clang-only warning flag that GCC (MSYS2 g++) does not recognize.
CXXFLAGS := $(filter-out -Wno-vla-extension,$(CXXFLAGS))

# Out-of-tree compile rules — use CXXFLAGS fully expanded by compile.mk
build/SynthEngine.cpp.o: ../modules/alloyflux/src/SynthEngine.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build/AlloyFlux.cpp.o: ../modules/alloyflux/vcv/AlloyFlux.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Where Rack loads user plugins from. plugin.mk works this out per platform
# (LOCALAPPDATA on Windows, XDG on Linux, Application Support on macOS) and
# appends the arch; the root Makefile asks for it rather than guessing.
.PHONY: print-plugins-dir
print-plugins-dir:
	@echo "$(PLUGINS_DIR)"
