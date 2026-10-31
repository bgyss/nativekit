# nativeshvc: a native Super Nintendo for decompiled games.
#
#   cmake -B build -DNSHVC_HOST=macos -DNSHVC_RENDER=metal
#   cmake --build build
#
# Builds the static library nshvc (the console, a host and a renderer) that a
# game links with its C. Options:
#   NSHVC_HOST    null (headless; the default) | macos | windows | ios | none
#                 (none: the app supplies host.h's functions itself)
#   NSHVC_RENDER  soft (the default for null) | metal (macos, ios) | gl
#                 (windows) | vulkan
#   NSHVC_TESTS   build the tests (tests/)
#   NSHVC_EXAMPLES build the examples (examples/)
if(NOT PROJECT_NAME)
  cmake_minimum_required(VERSION 3.16)
endif()
project(nativeshvc VERSION 2.1.0 LANGUAGES C)
set(NSHVC_DIR ${CMAKE_CURRENT_SOURCE_DIR})

add_library(nshvc STATIC
  src/console/bus.c
  src/console/dma.c
  src/console/ports.c
  src/console/apu.c
  src/console/dispatch.c
  src/ppu/ppu.c
  src/dsp/mixer.c
  src/dsp/mixer_regs.c
  src/front/console.c
  src/front/input.c
  src/front/script.c)

target_include_directories(nshvc
  PUBLIC include
  PRIVATE include/nshvc src/console src/ppu src/dsp src/front src/render ${NK_INCLUDE})
if(NOT MSVC)
  target_compile_options(nshvc PRIVATE -Wall -fno-strict-aliasing -ffp-contract=off)
endif()

# the kit's coroutines, resampler, time, files and host (kit/CMakeLists.txt)
target_sources(nshvc PRIVATE $<TARGET_OBJECTS:nk_coro> $<TARGET_OBJECTS:nk_resample> $<TARGET_OBJECTS:nk_host>)
if(WIN32)
  target_link_libraries(nshvc PUBLIC winmm)
endif()

# the host
if(NSHVC_HOST STREQUAL "null")
  # nothing to link
elseif(NSHVC_HOST STREQUAL "macos")
  target_link_libraries(nshvc PUBLIC "-framework Cocoa" "-framework QuartzCore" "-framework AudioToolbox"
    "-framework CoreAudio" "-framework GameController")
elseif(NSHVC_HOST STREQUAL "ios")
  target_sources(nshvc PRIVATE src/front/ios_main.c)
  target_compile_definitions(nshvc PUBLIC NSHVC_HOST_IOS=1)
  target_link_libraries(nshvc PUBLIC "-framework UIKit" "-framework CoreGraphics" "-framework QuartzCore"
    "-framework AudioToolbox" "-framework AVFoundation" "-framework GameController")
elseif(NSHVC_HOST STREQUAL "windows")
  target_link_libraries(nshvc PUBLIC ole32 uuid xinput9_1_0 shell32 gdi32 user32)
elseif(NOT NSHVC_HOST STREQUAL "none")
  message(FATAL_ERROR "nativeshvc: no host ${NSHVC_HOST}")
endif()

# the compositor's source (src/render/compose_body.h) as a string, for the
# backends that compile it at run time
set(_cp_src ${CMAKE_CURRENT_BINARY_DIR}/gen/rr_compose_src.h)
add_custom_command(OUTPUT ${_cp_src}
  COMMAND ${CMAKE_COMMAND} -DIN=${NSHVC_DIR}/src/render/compose_body.h -DOUT=${_cp_src}
    -DNAME=k_compose_body -P ${NSHVC_DIR}/src/render/embed.cmake
  DEPENDS src/render/compose_body.h src/render/embed.cmake)
target_sources(nshvc PRIVATE ${_cp_src})
target_include_directories(nshvc PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gen)

# the renderer
if(NSHVC_RENDER STREQUAL "soft")
  target_sources(nshvc PRIVATE src/render/soft/rr_soft.c)
elseif(NSHVC_RENDER STREQUAL "metal")
  target_sources(nshvc PRIVATE src/render/metal/rr_metal.m)
  set_source_files_properties(src/render/metal/rr_metal.m PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_link_libraries(nshvc PUBLIC "-framework Metal" "-framework QuartzCore" "-framework Foundation")
elseif(NSHVC_RENDER STREQUAL "gl")
  target_sources(nshvc PRIVATE src/render/gl/rr_gl.c src/render/gl/gl_ctx.c)
  if(WIN32)
    target_link_libraries(nshvc PUBLIC opengl32 gdi32)
  elseif(APPLE)
    target_link_libraries(nshvc PUBLIC "-framework OpenGL")
  else()
    target_link_libraries(nshvc PUBLIC GL)
  endif()
elseif(NSHVC_RENDER STREQUAL "vulkan")
  find_package(Vulkan REQUIRED)
  target_sources(nshvc PRIVATE src/render/vulkan/rr_vulkan.c)
  target_link_libraries(nshvc PUBLIC Vulkan::Vulkan)
else()
  message(FATAL_ERROR "nativeshvc: no renderer ${NSHVC_RENDER}")
endif()

if(NOT WIN32)
  target_link_libraries(nshvc PUBLIC m)
endif()

if(NSHVC_TESTS)
  add_subdirectory(tests)
endif()
if(NSHVC_EXAMPLES)
  add_subdirectory(examples)
endif()
