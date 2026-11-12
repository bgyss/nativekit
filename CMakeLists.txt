# The monorepo: nativekit (kit/), the console-independent parts, and each
# native console on it (consoles/). The SNES (consoles/snes) is nativeshvc:
#
#   cmake -B build -DNK_HOST=macos -DNK_RENDER=metal
#   cmake --build build
#
# builds the static library nshvc a game links with its C. Options as
# consoles/snes/CMakeLists.txt describes them.
cmake_minimum_required(VERSION 3.16)
project(nativekit VERSION 1.1.0 LANGUAGES C)

if(APPLE)
  enable_language(OBJC)
endif()
if(NOT MSVC)
  enable_language(ASM)
endif()

# Generic options; the original names remain accepted for existing games.
foreach(_o HOST RENDER TESTS EXAMPLES)
  if(DEFINED _NK_LAST_${_o} AND DEFINED NSHVC_${_o}
     AND NOT "${NSHVC_${_o}}" STREQUAL "${_NK_LAST_${_o}}"
     AND "${NK_${_o}}" STREQUAL "${_NK_LAST_${_o}}")
    set(NK_${_o} "${NSHVC_${_o}}" CACHE STRING "" FORCE)
  elseif(DEFINED NSHVC_${_o} AND NOT DEFINED NK_${_o})
    set(NK_${_o} "${NSHVC_${_o}}" CACHE STRING "" FORCE)
  endif()
endforeach()
set(NK_HOST "null" CACHE STRING "the host: null, macos, windows, ios, none")
if(APPLE AND NOT NK_HOST STREQUAL "null")
  set(_render_default metal)
elseif(WIN32)
  set(_render_default gl)
else()
  set(_render_default soft)
endif()
set(NK_RENDER "${_render_default}" CACHE STRING "the renderer: soft, metal, gl, vulkan")
option(NK_TESTS "build the tests" OFF)
option(NK_EXAMPLES "build the examples" OFF)
set(NK_CONSOLES "snes;md;nes" CACHE STRING "the consoles to build")
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)
foreach(_o HOST RENDER TESTS EXAMPLES)
  set(NSHVC_${_o} "${NK_${_o}}" CACHE STRING "legacy alias for NK_${_o}" FORCE)
  set(NSHVC_${_o} "${NK_${_o}}")
  set(_NK_LAST_${_o} "${NK_${_o}}" CACHE INTERNAL "last synchronized option" FORCE)
endforeach()
if(NOT WIN32 AND NOT NK_HOST STREQUAL "none")
  find_package(Threads REQUIRED)
endif()
add_subdirectory(kit)
foreach(_c ${NK_CONSOLES})
  if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/consoles/${_c}/CMakeLists.txt)
    message(FATAL_ERROR "nativekit: no console ${_c} (consoles/${_c})")
  endif()
  add_subdirectory(consoles/${_c})
endforeach()
