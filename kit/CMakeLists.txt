# nativekit: the console-independent parts of a native console (kit/docs/kit.md).
# OBJECT libraries a console folds into its own library, so a game links the
# console's library alone. NK_HOST (the host: null, macos, windows, ios,
# none), NK_RENDER (soft, metal, gl, vulkan) and NK_TESTS are set by the top-level CMake from the console's own options.
set(NK_DIR ${CMAKE_CURRENT_SOURCE_DIR})
file(READ ${NK_DIR}/VERSION NK_VERSION)
string(STRIP "${NK_VERSION}" NK_VERSION)
include(${NK_DIR}/tests/kit_names.cmake)
set(NK_INCLUDE ${NK_DIR}/include ${NK_DIR}/include/nk)
set(NK_INCLUDE ${NK_INCLUDE} PARENT_SCOPE)

if(NOT MSVC)
  set(NK_C_FLAGS -Wall -fno-strict-aliasing -ffp-contract=off)
endif()

# the coroutines and their switch (the architecture the build targets)
add_library(nk_coro OBJECT ${NK_DIR}/src/coro/coro.c)
if(NOT WIN32)
  set(_arch "${CMAKE_SYSTEM_PROCESSOR}")
  if(CMAKE_OSX_ARCHITECTURES)
    set(_arch "${CMAKE_OSX_ARCHITECTURES}")
  endif()
  if(_arch MATCHES "arm64|aarch64|ARM64")
    target_sources(nk_coro PRIVATE ${NK_DIR}/src/coro/coro_arm64.S)
  elseif(_arch MATCHES "x86_64|AMD64|amd64")
    target_sources(nk_coro PRIVATE ${NK_DIR}/src/coro/coro_x86_64.S)
  else()
    message(FATAL_ERROR "nativekit: no coroutine switch for ${_arch} (one architecture a build)")
  endif()
endif()

add_library(nk_resample OBJECT ${NK_DIR}/src/dsp/resample.c)

# the routine table's lookup
add_library(nk_dispatch OBJECT ${NK_DIR}/src/dispatch/routines.c)

# the front: the console's settings, the battery save, the headless script
add_library(nk_front OBJECT ${NK_DIR}/src/front/env.c ${NK_DIR}/src/front/save.c ${NK_DIR}/src/front/script.c)

# the players' native devices (it reads the host's game controllers)
add_library(nk_input OBJECT ${NK_DIR}/src/front/input.c)

# the application: main's loop, the window, the pace, the sound out
add_library(nk_app OBJECT ${NK_DIR}/src/front/app.c)

# the presentation backend (NK_RENDER; the Vulkan one still lives in the
# console's folder until it moves here). On Metal and GL its frameworks or
# libraries are nk_render's link libraries, for the console's library to
# link; on GL its folder (gl_api.h, for rr_gl_ext.h) is an include directory
# of nk_render's, for the console's own GL code.
if(NOT NK_RENDER MATCHES "^(soft|metal|gl|vulkan)$")
  message(FATAL_ERROR "nativekit: no renderer ${NK_RENDER}")
endif()
if(NK_RENDER STREQUAL "soft")
  add_library(nk_render OBJECT)
  target_sources(nk_render PRIVATE ${NK_DIR}/src/render/soft/rr_soft.c)
  set(NK_RENDER_TARGETS nk_render)
elseif(NK_RENDER STREQUAL "metal")
  add_library(nk_render OBJECT)
  target_sources(nk_render PRIVATE ${NK_DIR}/src/render/metal/rr_metal.m)
  set_source_files_properties(${NK_DIR}/src/render/metal/rr_metal.m PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_link_libraries(nk_render PUBLIC "-framework Metal" "-framework QuartzCore" "-framework Foundation")
  set(NK_RENDER_TARGETS nk_render)
elseif(NK_RENDER STREQUAL "gl")
  add_library(nk_render OBJECT)
  target_sources(nk_render PRIVATE ${NK_DIR}/src/render/gl/rr_gl.c ${NK_DIR}/src/render/gl/gl_ctx.c)
  target_include_directories(nk_render PUBLIC ${NK_DIR}/src/render/gl)
  if(WIN32)
    target_link_libraries(nk_render PUBLIC opengl32 gdi32)
  elseif(APPLE)
    target_link_libraries(nk_render PUBLIC "-framework OpenGL")
  else()
    target_link_libraries(nk_render PUBLIC GL)
  endif()
  set(NK_RENDER_TARGETS nk_render)
endif()

# time, files and the host
add_library(nk_host OBJECT)
if(WIN32)
  target_sources(nk_host PRIVATE ${NK_DIR}/src/host/win32/host_win32.c)
elseif(NOT NK_HOST STREQUAL "none")
  target_sources(nk_host PRIVATE ${NK_DIR}/src/host/posix/host_posix.c)
endif()
if(NK_HOST STREQUAL "null")
  target_sources(nk_host PRIVATE ${NK_DIR}/src/host/null/host_null.c)
elseif(NK_HOST STREQUAL "macos")
  target_sources(nk_host PRIVATE ${NK_DIR}/src/host/macos/host_macos.m)
  set_source_files_properties(${NK_DIR}/src/host/macos/host_macos.m PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
elseif(NK_HOST STREQUAL "ios")
  target_sources(nk_host PRIVATE ${NK_DIR}/src/host/ios/host_ios.m)
  set_source_files_properties(${NK_DIR}/src/host/ios/host_ios.m PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
elseif(NK_HOST STREQUAL "windows")
  target_sources(nk_host PRIVATE ${NK_DIR}/src/host/windows/host_windows.c)
elseif(NOT NK_HOST STREQUAL "none")
  message(FATAL_ERROR "nativekit: no host ${NK_HOST}")
endif()

foreach(t nk_coro nk_resample nk_dispatch nk_host nk_front nk_input nk_app ${NK_RENDER_TARGETS})
  target_include_directories(${t} PRIVATE ${NK_INCLUDE})
  if(NK_C_FLAGS)
    target_compile_options(${t} PRIVATE ${NK_C_FLAGS})
  endif()
endforeach()

if(NK_TESTS)
  add_subdirectory(tests)
endif()
