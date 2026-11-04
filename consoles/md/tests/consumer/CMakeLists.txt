cmake_minimum_required(VERSION 3.16)
project(nmd_consumer LANGUAGES C)

if(NOT DEFINED NATIVEKIT_ROOT)
  message(FATAL_ERROR "pass -DNATIVEKIT_ROOT=/path/to/nativekit")
endif()

set(NK_CONSOLES "md" CACHE STRING "" FORCE)
set(NK_HOST "null" CACHE STRING "" FORCE)
set(NK_RENDER "soft" CACHE STRING "" FORCE)
set(NK_TESTS OFF CACHE BOOL "" FORCE)
set(NK_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${NATIVEKIT_ROOT}" nativekit)

add_executable(md_consumer main.c)
target_link_libraries(md_consumer PRIVATE nmd)
