# ASan and UBSan for the tests and the copy of the library they test, so memory
# errors and undefined behavior in the library's own code fail the tests too.
# Any undefined behavior aborts instead of just printing a warning.
add_library(natpmp_sanitizers INTERFACE)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  target_compile_options(natpmp_sanitizers INTERFACE
    -fsanitize=address,undefined
    -fno-sanitize-recover=undefined
    -fno-omit-frame-pointer)
  target_link_options(natpmp_sanitizers INTERFACE
    -fsanitize=address,undefined)
elseif(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
  target_compile_options(natpmp_sanitizers INTERFACE /fsanitize=address /Zi)
else()
  message(WARNING
    "No sanitizers configured for compiler: ${CMAKE_C_COMPILER_ID}")
endif()
