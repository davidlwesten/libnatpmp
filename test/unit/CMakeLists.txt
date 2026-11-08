natpmp_add_test_executable(unit_tests
  main.c
  packet.c)

target_include_directories(unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/lib)
