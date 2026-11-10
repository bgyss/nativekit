# nativenes: the NES / Famicom console library, nnes.
#
# A hardware foundation that does not yet use the kit: it builds from the
# repository root with the other consoles (NK_CONSOLES names nes), or alone,
# for a game that needs nothing else:
#
#   cmake -S consoles/nes -B build/nes -DNNES_EXAMPLES=ON
#
# NNES_TESTS and NNES_EXAMPLES follow NK_TESTS and NK_EXAMPLES from the root.
cmake_minimum_required(VERSION 3.16)
project(nativenes VERSION 0.1.0 LANGUAGES C)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  option(NNES_TESTS "Build NES contract tests" ON)
  option(NNES_EXAMPLES "Build synthetic native cartridge example" OFF)
  set(_nnes_out ${CMAKE_BINARY_DIR})
else()
  if(NOT DEFINED NNES_TESTS)
    set(NNES_TESTS ${NK_TESTS})
  endif()
  if(NOT DEFINED NNES_EXAMPLES)
    set(NNES_EXAMPLES ${NK_EXAMPLES})
  endif()
  set(_nnes_out ${CMAKE_BINARY_DIR}/nes)
endif()

add_library(nnes STATIC src/nes.c src/machine.c src/ppu.c src/apu.c)
target_include_directories(nnes PUBLIC include)
target_compile_features(nnes PUBLIC c_std_11)
set_target_properties(nnes PROPERTIES C_EXTENSIONS OFF)
if(MSVC)
  target_compile_options(nnes PRIVATE /W4)
else()
  target_compile_options(nnes PRIVATE -Wall -Wextra -Wpedantic)
endif()

set(_nnes_checks "")
function(nnes_program name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE nnes)
  set_target_properties(${name} PROPERTIES C_EXTENSIONS OFF RUNTIME_OUTPUT_DIRECTORY ${_nnes_out})
endfunction()

if(NNES_TESTS)
  enable_testing()
  foreach(_t contract machine_contract control_contract bus_operation_contract
             ppu_contract jsr_contract apu_status_contract)
    nnes_program(nnes_${_t} tests/${_t}.c)
    add_test(NAME nnes_${_t} COMMAND nnes_${_t})
    list(APPEND _nnes_checks COMMAND nnes_${_t})
  endforeach()
endif()
if(NNES_EXAMPLES)
  nnes_program(nnes_native_cart examples/native_cart.c)
  if(NNES_TESTS)
    add_test(NAME nnes_native_cart COMMAND nnes_native_cart)
    list(APPEND _nnes_checks COMMAND nnes_native_cart)
  endif()
endif()

# every nnes check at once: cmake --build <dir> --target nes-check
if(NNES_TESTS)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_custom_target(nes-check
    ${_nnes_checks}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/profile_check.py
    VERBATIM)
endif()
