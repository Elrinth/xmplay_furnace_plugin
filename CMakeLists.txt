cmake_minimum_required(VERSION 3.16)
project(xmp-furnace VERSION 1.0.0 LANGUAGES C CXX)

# Native XMPlay input plugin (Furnace headless only — no libopenmpt)
# Cross-compile:
#   cmake -B build-i686 -DCMAKE_TOOLCHAIN_FILE=cmake/i686-w64-mingw32.cmake
#   cmake --build build-i686
# Prefer the Makefile (`/usr/bin/make dll`) which builds the engine too.

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_C_STANDARD 99)

add_library(dmf_probe STATIC src/dmf_probe.c)
target_include_directories(dmf_probe PUBLIC ${CMAKE_SOURCE_DIR}/src)

set(XMP_FURNACE_PLUGIN_SOURCES src/xmp-furnace.cpp src/furnace_player.cpp src/xmp-furnace.def)

add_library(xmp-furnace SHARED ${XMP_FURNACE_PLUGIN_SOURCES})
set_target_properties(xmp-furnace PROPERTIES
  PREFIX ""
  OUTPUT_NAME "xmp-furnace"
  SUFFIX ".dll")
target_include_directories(xmp-furnace PRIVATE
  ${CMAKE_SOURCE_DIR}/include/xmplay
  ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(xmp-furnace PRIVATE dmf_probe)

if(MINGW)
  target_link_options(xmp-furnace PRIVATE -static -static-libgcc -static-libstdc++)
  if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    target_link_options(xmp-furnace PRIVATE -Wl,--kill-at -Wl,--add-stdcall-alias)
  endif()
endif()

if(NOT CMAKE_CROSSCOMPILING)
  add_executable(test_probe tests/test_probe.c)
  target_link_libraries(test_probe PRIVATE dmf_probe z)
  enable_testing()
  add_test(NAME probe COMMAND test_probe)
endif()
