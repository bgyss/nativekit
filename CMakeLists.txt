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
add_subdirectory(consoles/snes)
