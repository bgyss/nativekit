# the kit's own checks, each against a stub console where it needs one
add_executable(kit_render_check render_check.c ${NK_DIR}/src/render/rr_common.c)
target_include_directories(kit_render_check PRIVATE ${NK_INCLUDE} ${NK_PRIVATE_INCLUDE})
set_target_properties(kit_render_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/kit/tests)
add_executable(kit_fatal_check fatal_check.c $<TARGET_OBJECTS:nk_coro>)
add_executable(kit_coro_check coro_check.c $<TARGET_OBJECTS:nk_coro>)
foreach(t kit_fatal_check kit_coro_check)
  target_include_directories(${t} PRIVATE ${NK_INCLUDE} ${NK_PRIVATE_INCLUDE})
  set_target_properties(${t} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/kit/tests)
endforeach()
add_executable(kit_routines_check routines_check.c $<TARGET_OBJECTS:nk_dispatch>)
target_include_directories(kit_routines_check PRIVATE ${NK_INCLUDE} ${NK_PRIVATE_INCLUDE})
set_target_properties(kit_routines_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/kit/tests)
add_executable(kit_env_check env_check.c $<TARGET_OBJECTS:nk_front>)
add_executable(kit_save_check save_check.c $<TARGET_OBJECTS:nk_front>)
foreach(t kit_env_check kit_save_check)
  target_include_directories(${t} PRIVATE ${NK_INCLUDE} ${NK_PRIVATE_INCLUDE})
  set_target_properties(${t} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/kit/tests)
endforeach()
# (it gives its own host_pad_read, so it links no host)
add_executable(kit_input_check input_check.c $<TARGET_OBJECTS:nk_input>)
target_include_directories(kit_input_check PRIVATE ${NK_INCLUDE} ${NK_PRIVATE_INCLUDE})
set_target_properties(kit_input_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/kit/tests)
add_executable(kit_stub_console stub_console.c $<TARGET_OBJECTS:nk_front>)
target_include_directories(kit_stub_console PRIVATE ${NK_INCLUDE} ${NK_PRIVATE_INCLUDE})
set_target_properties(kit_stub_console PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/kit/tests)

# every kit check, and the name check, at once: cmake --build <dir> --target kit-check
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_custom_target(kit-check
  COMMAND ${CMAKE_COMMAND} -DNK_DIR=${NK_DIR} -P ${NK_DIR}/tests/kit_names.cmake
  COMMAND ${CMAKE_COMMAND} -E env --unset=RR_SIZE $<TARGET_FILE:kit_render_check>
  COMMAND ${CMAKE_COMMAND} -E env RR_SIZE=1100x800 $<TARGET_FILE:kit_render_check>
  COMMAND ${CMAKE_COMMAND} -E env RR_SIZE=1100x800 $<TARGET_FILE:kit_render_check> --override-only
  COMMAND ${CMAKE_COMMAND} -E env --unset=RR_SIZE ${CMAKE_COMMAND} "-DCMD=$<TARGET_FILE:kit_render_check>$<SEMICOLON>--unset" -P ${NK_DIR}/tests/expect_fatal.cmake
  COMMAND $<TARGET_FILE:kit_coro_check>
  COMMAND ${CMAKE_COMMAND} -DCMD=$<TARGET_FILE:kit_fatal_check> -P ${NK_DIR}/tests/expect_fatal.cmake
  COMMAND $<TARGET_FILE:kit_routines_check>
  COMMAND $<TARGET_FILE:kit_env_check>
  COMMAND $<TARGET_FILE:kit_save_check>
  COMMAND ${CMAKE_COMMAND} "-DCMD=$<TARGET_FILE:kit_save_check>$<SEMICOLON>--past" -P ${NK_DIR}/tests/expect_fatal.cmake
  COMMAND $<TARGET_FILE:kit_input_check>
  COMMAND ${Python3_EXECUTABLE} ${NK_DIR}/tests/states_check.py $<TARGET_FILE:kit_stub_console>
  COMMAND ${Python3_EXECUTABLE} ${NK_DIR}/tests/profile_lib_check.py
  DEPENDS kit_coro_check kit_fatal_check kit_routines_check kit_env_check kit_save_check kit_input_check
          kit_stub_console kit_render_check
  VERBATIM)
