# A game as games use nativeshvc: its own project, nativeshvc added as a
# subdirectory, the nshvc target linked.
cmake_minimum_required(VERSION 3.16)
project(consumer LANGUAGES C)
add_subdirectory(${NATIVESHVC} nativeshvc)
add_executable(consumer game.c)
target_link_libraries(consumer PRIVATE nshvc)
