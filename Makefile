# xmp-furnace — native XMPlay input plugin (Furnace / DefleMask / FamiTracker)
#
#   make          # 32-bit xmp-furnace.dll + host tests
#   make dll      # dist/xmp-furnace.dll
#   make pack     # /workspace/xmp-furnace-1.0.zip
#   make test     # magic-byte probe (host)
#   make test-render  # Furnace host render of local samples
#   make clean
#
# If `make` is a wrapper, invoke GNU make explicitly:  /usr/bin/make -C thisdir

ROOT     := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
DIST     := $(ROOT)/dist
SRC      := $(ROOT)/src
INC      := $(ROOT)/include/xmplay
TP       := $(ROOT)/third_party
FURNACE_SRC := $(TP)/furnace
FURNACE_LINUX := $(TP)/build-furnace-linux
FURNACE_I686 := $(TP)/build-furnace-i686
FURNACE_REV := e14a0a3e2da06a5c7c63d4910be7f3759303f6f5
FURNACE_GIT := https://github.com/tildearrow/furnace.git

I686_HOST := i686-w64-mingw32
I686_CC   := $(I686_HOST)-gcc
I686_CXX  := $(I686_HOST)-g++

JOBS ?= $(shell nproc 2>/dev/null || echo 2)
GNUMAKE := /usr/bin/make

# Full (nearly full) headless Furnace: all chip systems + .fur/.dmf/.ftm loaders.
# No XMP_DMF_STRIP. No libopenmpt.
FURNACE_CMAKE_FLAGS = \
	-DCMAKE_BUILD_TYPE=Release \
	-DBUILD_GUI=OFF -DUSE_SDL2=OFF -DUSE_RTMIDI=OFF \
	-DWITH_PORTAUDIO=OFF -DUSE_SNDFILE=OFF -DWITH_JACK=OFF \
	-DUSE_BACKWARD=OFF -DWITH_LOCALE=OFF -DWITH_JSON=OFF \
	-DWARNINGS_ARE_ERRORS=OFF -DWITH_OGG=OFF -DWITH_MPEG=OFF \
	-DWITH_ASIO=OFF -DWITH_DEMOS=OFF -DWITH_INSTRUMENTS=OFF \
	-DWITH_WAVETABLES=OFF -DBUILD_ENGINE_LIB=ON

.PHONY: all dll test test-render pack clean furnace-linux furnace-i686

all: test test-render dll

dll: $(DIST)/xmp-furnace.dll

test: $(DIST)/test_probe
	$(DIST)/test_probe

test-render: $(DIST)/test_furnace_render
	$(DIST)/test_furnace_render

$(FURNACE_SRC)/.xmp-furnace-patched:
	@if [ ! -e $(FURNACE_SRC)/.git ]; then \
	  git clone --recurse-submodules $(FURNACE_GIT) $(FURNACE_SRC); \
	fi
	cd $(FURNACE_SRC) && git fetch --depth 1 origin $(FURNACE_REV) && git checkout --force $(FURNACE_REV)
	cd $(FURNACE_SRC) && git apply --check $(TP)/patches/furnace-engine-lib.patch && git apply $(TP)/patches/furnace-engine-lib.patch
	touch $@

$(FURNACE_LINUX)/libfurnace_engine.a: $(FURNACE_SRC)/.xmp-furnace-patched
	mkdir -p $(FURNACE_LINUX)
	cmake -S $(FURNACE_SRC) -B $(FURNACE_LINUX) \
	  $(FURNACE_CMAKE_FLAGS) -DSYSTEM_ZLIB=OFF \
	  -DCMAKE_C_FLAGS="-O2 -fPIC" -DCMAKE_CXX_FLAGS="-O2 -fPIC"
	cmake --build $(FURNACE_LINUX) --target furnace_engine opus -j$(JOBS)
	@test -f $(FURNACE_LINUX)/libfurnace_engine.a

$(FURNACE_I686)/libfurnace_engine.a: $(FURNACE_SRC)/.xmp-furnace-patched
	mkdir -p $(FURNACE_I686)
	cmake -S $(FURNACE_SRC) -B $(FURNACE_I686) \
	  $(FURNACE_CMAKE_FLAGS) -DSYSTEM_ZLIB=OFF \
	  -DCMAKE_SYSTEM_NAME=Windows \
	  -DCMAKE_SYSTEM_PROCESSOR=i686 \
	  -DCMAKE_C_COMPILER=$(I686_CC) \
	  -DCMAKE_CXX_COMPILER=$(I686_CXX) \
	  -DCMAKE_RC_COMPILER=$(I686_HOST)-windres \
	  -DCMAKE_FIND_ROOT_PATH=/usr/i686-w64-mingw32 \
	  -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
	  -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
	  -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
	  -DCMAKE_C_FLAGS="-O2" -DCMAKE_CXX_FLAGS="-O2"
	cmake --build $(FURNACE_I686) --target furnace_engine opus -j$(JOBS)
	@test -f $(FURNACE_I686)/libfurnace_engine.a

furnace-linux: $(FURNACE_LINUX)/libfurnace_engine.a
furnace-i686: $(FURNACE_I686)/libfurnace_engine.a

PLUGIN_SRCS := $(SRC)/xmp-furnace.cpp $(SRC)/dmf_probe.c $(SRC)/furnace_player.cpp
PLUGIN_INCS := -I$(INC) -I$(SRC) \
	-I$(FURNACE_SRC)/src -I$(FURNACE_SRC)/src/engine -I$(FURNACE_SRC)/src/audio \
	-I$(FURNACE_SRC)/extern/fmt/include -I$(FURNACE_SRC)/extern/blip_buf \
	-I$(FURNACE_SRC)/extern/klattsch-cpp/include \
	-I$(FURNACE_SRC)/extern/fftw/api -I$(FURNACE_SRC)/extern/vgsound_emu-modified

# Extra static libs produced next to furnace_engine (fmt / zlib / opus / klattsch / fftw).
define furnace_extra_libs
$(sort $(wildcard \
	$(1)/extern/fmt/libfmt.a \
	$(1)/extern/zlib/libzlibstatic.a \
	$(1)/extern/zlib/libz.a \
	$(1)/extern/opus/libopus.a \
	$(1)/extern/klattsch-cpp/libklattsch.a \
	$(1)/libklattsch.a \
	$(1)/extern/fftw/libfftw3.a \
	$(1)/libfftw3.a))
endef

PLUGIN_CXXFLAGS = -shared -O2 -DNDEBUG -std=c++14 \
	  -static -static-libgcc -static-libstdc++ \
	  $(PLUGIN_INCS) \
	  -I$(FURNACE_I686)

PLUGIN_SYSLIBS = -Wl,--kill-at -Wl,--add-stdcall-alias \
	  -lz -lshlwapi -lwinmm -lws2_32 -lpthread -Wl,-s

$(DIST)/xmp-furnace.dll: $(PLUGIN_SRCS) $(SRC)/dmf_probe.h $(SRC)/furnace_player.h \
		$(FURNACE_I686)/libfurnace_engine.a
	mkdir -p $(DIST)
	$(I686_CXX) $(PLUGIN_CXXFLAGS) \
	  -o $@ $(PLUGIN_SRCS) $(SRC)/xmp-furnace.def \
	  $(FURNACE_I686)/libfurnace_engine.a \
	  $(call furnace_extra_libs,$(FURNACE_I686)) \
	  $(PLUGIN_SYSLIBS)
	$(I686_HOST)-objdump -p $@ | grep -E 'dll name|XMPIN_GetInterface' || true
	file $@

pack: dll
	rm -f /workspace/xmp-furnace-1.0.zip
	mkdir -p $(DIST)/pack
	cp -f $(DIST)/xmp-furnace.dll $(ROOT)/README.md $(DIST)/pack/
	cd $(DIST)/pack && zip -9 /workspace/xmp-furnace-1.0.zip xmp-furnace.dll README.md
	rm -rf $(DIST)/pack
	ls -l /workspace/xmp-furnace-1.0.zip

$(DIST)/test_probe: $(SRC)/dmf_probe.c $(SRC)/dmf_probe.h $(ROOT)/tests/test_probe.c
	mkdir -p $(DIST)
	$(CC) -O2 -Wall -Wextra -I$(SRC) -o $@ $(ROOT)/tests/test_probe.c $(SRC)/dmf_probe.c -lz

$(DIST)/test_furnace_render: $(SRC)/furnace_player.cpp $(SRC)/furnace_player.h \
		$(SRC)/dmf_probe.c $(ROOT)/tests/test_furnace_render.cpp \
		$(FURNACE_LINUX)/libfurnace_engine.a
	mkdir -p $(DIST)
	$(CXX) -O2 -std=c++14 -I$(SRC) $(PLUGIN_INCS) \
	  -o $@ $(ROOT)/tests/test_furnace_render.cpp $(SRC)/furnace_player.cpp $(SRC)/dmf_probe.c \
	  $(FURNACE_LINUX)/libfurnace_engine.a \
	  $(call furnace_extra_libs,$(FURNACE_LINUX)) \
	  -lz -lpthread -ldl -lm

clean:
	rm -rf $(DIST)/xmp-furnace.dll $(DIST)/test_probe $(DIST)/test_furnace_render $(DIST)/pack

distclean: clean
	rm -rf $(FURNACE_LINUX) $(FURNACE_I686)
