add_executable(console_check unit/console_check.c)
target_link_libraries(console_check PRIVATE nmd)
set_target_properties(console_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)

add_executable(bus_check unit/bus_check.c)
target_include_directories(bus_check PRIVATE ../src/console)
target_link_libraries(bus_check PRIVATE nmd)
set_target_properties(bus_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)

add_executable(md_testcart rom/testcart.c)
target_include_directories(md_testcart PRIVATE ../src/console)
target_link_libraries(md_testcart PRIVATE nmd)
set_target_properties(md_testcart PROPERTIES OUTPUT_NAME md_testcart
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)

add_executable(interrupt_check unit/interrupt_check.c)
target_include_directories(interrupt_check PRIVATE ../src/console)
target_link_libraries(interrupt_check PRIVATE nmd)
set_target_properties(interrupt_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)

add_executable(vdp_check unit/vdp_check.c)
target_include_directories(vdp_check PRIVATE ../src/console)
target_link_libraries(vdp_check PRIVATE nmd)
set_target_properties(vdp_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
