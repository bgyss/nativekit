# The monorepo: nativekit (kit/), the console-independent parts, and each
# native console on it (consoles/). The SNES (consoles/snes) is nativeshvc:
#
#   cmake -B build -DNSHVC_HOST=macos -DNSHVC_RENDER=metal
#   cmake --build build
#
# builds the static library nshvc a game links with its C. Options as
# consoles/snes/CMakeLists.txt describes them.
cmake_minimum_required(VERSION 3.16)
project(nativekit VERSION 0.1.0 LANGUAGES C)

if(APPLE)
  enable_language(OBJC)
endif()
if(NOT MSVC)
  enable_language(ASM)
endif()

set(NSHVC_HOST "null" CACHE STRING "the host: null, macos, windows, ios, none")
if(APPLE AND NOT NSHVC_HOST STREQUAL "null")
  set(_render_default metal)
elseif(WIN32)
  set(_render_default gl)
else()
  set(_render_default soft)
endif()
set(NSHVC_RENDER "${_render_default}" CACHE STRING "the renderer: soft, metal, gl, vulkan")
option(NSHVC_TESTS "build the tests" OFF)
option(NSHVC_EXAMPLES "build the examples" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)

# the kit reads the options as NK_HOST, NK_RENDER and NK_TESTS; they keep nativeshvc's names for its games
set(NK_HOST ${NSHVC_HOST})
set(NK_RENDER ${NSHVC_RENDER})
set(NK_TESTS ${NSHVC_TESTS})
add_subdirectory(kit)
add_subdirectory(consoles/snes)
