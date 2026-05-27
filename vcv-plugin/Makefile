RACK_DIR ?= /c/Users/carlosedp/Rack-SDK

# Expose the shared DSP headers and sources under common/include and common/src
EXTRA_FLAGS += -I../common/include -I../common/src

# VCV plugin sources (compile.mk resolves these to build/src/*.o)
SOURCES += src/plugin.cpp
SOURCES += src/AlloyFlux.cpp

# SynthEngine lives outside the plugin dir; add the object directly so
# compile.mk's patsubst doesn't resolve it to vcv-plugin/src/*.o
OBJECTS += build/SynthEngine.cpp.o
DISTRIBUTABLES += res

include $(RACK_DIR)/plugin.mk

# Rack SDK injects a Clang-only warning flag that GCC (MSYS2 g++) does not recognize.
CXXFLAGS := $(filter-out -Wno-vla-extension,$(CXXFLAGS))

# Out-of-tree compile rule — uses CXXFLAGS fully expanded by compile.mk
build/SynthEngine.cpp.o: ../common/src/SynthEngine.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c -o $@ $<
