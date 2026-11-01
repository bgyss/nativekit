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
  src/front/snes_input.c
  src/front/snes_console.c
  src/front/snes_present.c)

target_include_directories(nshvc
  PUBLIC include
  PRIVATE include/nshvc src/console src/ppu src/dsp src/front src/render ${NK_INCLUDE})
if(NOT MSVC)
  target_compile_options(nshvc PRIVATE -Wall -fno-strict-aliasing -ffp-contract=off)
endif()

# the kit's coroutines, resampler, time, files, host, settings, battery save
# and headless script, the players' devices and the application
# (kit/CMakeLists.txt)
target_sources(nshvc PRIVATE $<TARGET_OBJECTS:nk_coro> $<TARGET_OBJECTS:nk_resample> $<TARGET_OBJECTS:nk_dispatch> $<TARGET_OBJECTS:nk_host>
  $<TARGET_OBJECTS:nk_front> $<TARGET_OBJECTS:nk_input> $<TARGET_OBJECTS:nk_app>)
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
  target_sources(nshvc PRIVATE src/render/soft/snes_soft.c $<TARGET_OBJECTS:nk_render>)
elseif(NSHVC_RENDER STREQUAL "metal")
  # the kit's Metal backend (and its frameworks), the compositor and Mode 7 the SNES's own
  # (nshvc_main sets its open hook, snes_metal_open)
  target_sources(nshvc PRIVATE src/render/metal/snes_metal.m $<TARGET_OBJECTS:nk_render>)
  set_source_files_properties(src/render/metal/snes_metal.m PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_link_libraries(nshvc PUBLIC $<TARGET_PROPERTY:nk_render,INTERFACE_LINK_LIBRARIES>)
  target_compile_definitions(nshvc PRIVATE NSHVC_RENDER_METAL=1)
elseif(NSHVC_RENDER STREQUAL "gl")
  # the kit's GL backend (and its libraries), the compositor and Mode 7 the
  # SNES's own (nshvc_main sets its open hook, snes_gl_open)
  target_sources(nshvc PRIVATE src/render/gl/snes_gl.c $<TARGET_OBJECTS:nk_render>)
  target_include_directories(nshvc PRIVATE $<TARGET_PROPERTY:nk_render,INTERFACE_INCLUDE_DIRECTORIES>)
  target_link_libraries(nshvc PUBLIC $<TARGET_PROPERTY:nk_render,INTERFACE_LINK_LIBRARIES>)
  target_compile_definitions(nshvc PRIVATE NSHVC_RENDER_GL=1)
elseif(NSHVC_RENDER STREQUAL "vulkan")
  # the kit's Vulkan backend, the compositor and Mode 7 the SNES's own
  # (nshvc_main sets its open hook, snes_vulkan_open)
  find_package(Vulkan REQUIRED)
  target_sources(nshvc PRIVATE src/render/vulkan/snes_vulkan.c $<TARGET_OBJECTS:nk_render>)
  target_link_libraries(nshvc PUBLIC Vulkan::Vulkan)
  target_compile_definitions(nshvc PRIVATE NSHVC_RENDER_VULKAN=1)
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
