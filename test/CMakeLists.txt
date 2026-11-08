# The library itself only needs 3.23. The tests need 3.28 to keep Unity's own
# install rules out of cmake --install (EXCLUDE_FROM_ALL).
cmake_minimum_required(VERSION 3.28)

include(FetchContent)

FetchContent_Declare(
  unity
  GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
  GIT_TAG v2.7.0
  EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(unity)

# A test executable built from sources in the calling directory, plus the runner
# they all share. It tests the sanitized copy of the library.
function(natpmp_add_test_executable target)
  add_executable(${target} ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/runner.c ${ARGN})

  target_link_libraries(${target}
    PRIVATE
      natpmp_sanitized
      unity::framework)

  target_include_directories(${target}
    PRIVATE
      ${CMAKE_CURRENT_FUNCTION_LIST_DIR})

  # one ctest test per RUN_TEST(...) in the test sources, so tests only need to
  # be registered in their file's *_tests() function
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ARGN})

  foreach(source IN LISTS ARGN)
    file(STRINGS ${source} lines REGEX "RUN_TEST\\([a-z0-9_]+\\);")
    foreach(line IN LISTS lines)
      string(REGEX REPLACE ".*RUN_TEST\\(([a-z0-9_]+)\\);.*" "\\1" test_case "${line}")
      add_test(NAME ${test_case} COMMAND ${target} ${test_case})
    endforeach()
  endforeach()
endfunction()

add_subdirectory(unit)

# Checks that every packet the generated code decodes encodes back to the same
# bytes, and that every message it builds decodes again. Run it with
# ./build/test/natpmp_fuzz -max_total_time=60
if(NATPMP_BUILD_FUZZER)
  if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "NATPMP_BUILD_FUZZER needs clang for libFuzzer")
  endif()

  add_executable(natpmp_fuzz natpmp_fuzz.c
    ${PROJECT_SOURCE_DIR}/lib/natpmp_gen.c)
  target_compile_features(natpmp_fuzz PRIVATE c_std_23)
  target_include_directories(natpmp_fuzz PRIVATE ${PROJECT_SOURCE_DIR}/lib)
  target_compile_options(natpmp_fuzz
    PRIVATE
      -g
      -fsanitize=fuzzer,address,undefined)
  target_link_options(natpmp_fuzz
    PRIVATE -fsanitize=fuzzer,address,undefined)
  # natpmp_fuzz.c is regenerated along with the library's sources
  add_dependencies(natpmp_fuzz natpmp)
endif()
