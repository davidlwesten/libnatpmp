add_library(natpmp_warnings INTERFACE)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  target_compile_options(natpmp_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    -Wsign-conversion
    -Wcast-align
    -Wunused
    -Wnull-dereference
    -Wdouble-promotion
    -Wformat=2)
elseif(CMAKE_C_COMPILER_ID MATCHES "MSVC")
  target_compile_options(natpmp_warnings INTERFACE
    /W4
    /permissive-)
else()
  message(
    WARNING
    "No known warning flags set for compiler: ${CMAKE_C_COMPILER_ID}")
endif()

