add_executable(console_check unit/console_check.c)
target_link_libraries(console_check PRIVATE nmd)
set_target_properties(console_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
