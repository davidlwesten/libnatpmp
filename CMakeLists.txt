cmake_minimum_required(VERSION 3.23)

project(natpmp VERSION 0.1.0 LANGUAGES C)

# Off by default, so a newer compiler's warnings don't break the build. CI turns
# it on.
option(NATPMP_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

include(cmake/warnings.cmake)

set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_COMPILE_WARNING_AS_ERROR ${NATPMP_WARNINGS_AS_ERRORS})

# BUILD_TESTING comes from CTest. A project pulling in libnatpmp never builds
# its tests, even if it builds its own
if(PROJECT_IS_TOP_LEVEL)
  include(CTest)
endif()
