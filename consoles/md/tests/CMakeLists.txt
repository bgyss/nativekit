add_executable(console_check unit/console_check.c)
target_link_libraries(console_check PRIVATE nmd)
set_target_properties(console_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)

add_executable(bus_check unit/bus_check.c)
target_include_directories(bus_check PRIVATE ../src/console)
target_link_libraries(bus_check PRIVATE nmd)
set_target_properties(bus_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)

add_executable(md_testcart rom/testcart.c)
target_include_directories(md_testcart PRIVATE ../src/console ../src/vdp)
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

add_executable(dma_check unit/dma_check.c)
target_include_directories(dma_check PRIVATE ../src/console)
target_link_libraries(dma_check PRIVATE nmd)
set_target_properties(dma_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)

add_executable(render_check unit/render_check.c)
target_include_directories(render_check PRIVATE
  ../src/console ../src/front ../src/vdp ${NK_INCLUDE})
target_link_libraries(render_check PRIVATE nmd)
set_target_properties(render_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)

add_executable(z80_check unit/z80_check.c)
target_include_directories(z80_check PRIVATE ../src/console)
target_link_libraries(z80_check PRIVATE nmd)
set_target_properties(z80_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
