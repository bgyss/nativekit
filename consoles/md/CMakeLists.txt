# nativemd: the base Sega Mega Drive / Genesis console.
if(NOT TARGET nk_app)
  message(FATAL_ERROR "nativemd: configure from the repository root")
endif()
project(nativemd VERSION 0.1.0 LANGUAGES C)
set(NMD_DIR ${CMAKE_CURRENT_SOURCE_DIR})

add_library(nmd STATIC src/console/bus.c src/console/dispatch.c src/z80/z80.c
  src/z80/z80bus.c
  src/sound/sn76489.c src/sound/ym2612.c
  src/vdp/vdp.c src/vdp/dma.c
  src/vdp/render.c
  src/front/md_console.c src/front/md_present.c)
target_include_directories(nmd PUBLIC include PRIVATE src/front src/console ${NK_INCLUDE})
if(NOT MSVC)
  target_compile_options(nmd PRIVATE -Wall -fno-strict-aliasing -ffp-contract=off)
endif()

# A console library folds in the kit's common loop, host, input and renderer.
target_sources(nmd PRIVATE
  $<TARGET_OBJECTS:nk_coro>
  $<TARGET_OBJECTS:nk_resample>
  $<TARGET_OBJECTS:nk_dispatch>
  ${NK_HOST_OBJECTS}
  $<TARGET_OBJECTS:nk_front>
  $<TARGET_OBJECTS:nk_input>
  $<TARGET_OBJECTS:nk_app>
  $<TARGET_OBJECTS:nk_render>)

if(WIN32)
  target_link_libraries(nmd PUBLIC winmm ole32 uuid xinput9_1_0 shell32 gdi32 user32)
elseif(APPLE)
  if(NK_HOST STREQUAL "macos")
    target_link_libraries(nmd PUBLIC "-framework Cocoa" "-framework QuartzCore"
      "-framework AudioToolbox" "-framework CoreAudio" "-framework GameController")
  elseif(NK_HOST STREQUAL "ios")
    target_link_libraries(nmd PUBLIC "-framework UIKit" "-framework CoreGraphics"
      "-framework QuartzCore" "-framework AudioToolbox" "-framework AVFoundation"
      "-framework GameController")
  endif()
  if(NK_RENDER STREQUAL "metal" OR NK_RENDER STREQUAL "gl" OR NK_RENDER STREQUAL "vulkan")
    target_link_libraries(nmd PUBLIC $<TARGET_PROPERTY:nk_render,INTERFACE_LINK_LIBRARIES>)
  endif()
  target_link_libraries(nmd PUBLIC m)
elseif(NOT WIN32)
  if(NK_RENDER STREQUAL "gl" OR NK_RENDER STREQUAL "vulkan")
    target_link_libraries(nmd PUBLIC $<TARGET_PROPERTY:nk_render,INTERFACE_LINK_LIBRARIES>)
  endif()
  target_link_libraries(nmd PUBLIC m)
endif()

if(NK_TESTS)
  add_subdirectory(tests)
endif()
