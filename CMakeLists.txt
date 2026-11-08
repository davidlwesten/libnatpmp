cmake_minimum_required(VERSION 3.23)

project(natpmp VERSION 0.1.0 LANGUAGES C)

# The example is only built by default when libnatpmp isn't pulled in by another
# project (e.g. through FetchContent)
option(NATPMP_BUILD_EXAMPLE "Build the example client" ${PROJECT_IS_TOP_LEVEL})
option(NATPMP_BUILD_FUZZER
       "Build the libFuzzer harness for the wire format (clang)" OFF)
# Off by default, so a newer compiler's warnings don't break the build. CI turns
# it on.
option(NATPMP_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

include(cmake/warnings.cmake)

set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_COMPILE_WARNING_AS_ERROR ${NATPMP_WARNINGS_AS_ERRORS})

# BUILD_TESTING comes from CTest. A project pulling in libnatpmp never builds
# its tests, even if it builds its own. lib/ needs to know, since it builds a
# copy of the library for the tests.
if(PROJECT_IS_TOP_LEVEL)
  include(CTest)
endif()
if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTING)
  set(NATPMP_BUILD_TESTS ON)
  include(cmake/sanitizers.cmake)
else()
  set(NATPMP_BUILD_TESTS OFF)
endif()

add_subdirectory(lib)

if(NATPMP_BUILD_EXAMPLE)
  add_subdirectory(example)
endif()

if(NATPMP_BUILD_TESTS)
  add_subdirectory(test)
endif()
