# the kit's own checks, each against a stub console where it needs one
add_executable(kit_fatal_check fatal_check.c $<TARGET_OBJECTS:nk_coro>)
add_executable(kit_coro_check coro_check.c $<TARGET_OBJECTS:nk_coro>)
foreach(t kit_fatal_check kit_coro_check)
  target_include_directories(${t} PRIVATE ${NK_INCLUDE})
  set_target_properties(${t} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/kit/tests)
endforeach()
