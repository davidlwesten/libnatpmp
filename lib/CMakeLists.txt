# The library's sources and settings, shared by the library itself and the copy
# the tests use
function(natpmp_add_library target)
  add_library(${target} ${ARGN})

  target_compile_features(${target} PUBLIC c_std_23)

  target_sources(${target}
    PRIVATE
      gateway.c
      packet.c
      retransmit.c
      session.c
      natpmp_gen.c)

  target_include_directories(${target} PUBLIC ${PROJECT_SOURCE_DIR}/include)

  target_link_libraries(${target} PRIVATE natpmp_warnings)
endfunction()

natpmp_add_library(natpmp)

# The tests check the library's own code with ASan and UBSan too, and the
# library that gets used is never sanitized
if(NATPMP_BUILD_TESTS)
  natpmp_add_library(natpmp_sanitized STATIC)
  target_link_libraries(natpmp_sanitized PUBLIC natpmp_sanitizers)
endif()

# The wire format is generated from natpmp.pkt by pktc
# (https://github.com/davidlwesten/pktc). The output is checked in, so pktc is
# only needed to change the schema. When it's installed, editing the schema
# regenerates the code as part of the build, along with pktc's libFuzzer harness
# for it (test/natpmp_fuzz.c).
find_program(PKTC_EXECUTABLE pktc)
if(PKTC_EXECUTABLE)
  # pktc writes everything into one directory, so generate into the build tree
  # and copy the harness to the tests
  set(PKTC_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pktc)
  add_custom_command(
    OUTPUT
      ${CMAKE_CURRENT_SOURCE_DIR}/natpmp_gen.c
      ${CMAKE_CURRENT_SOURCE_DIR}/natpmp_gen.h
      ${PROJECT_SOURCE_DIR}/test/natpmp_fuzz.c
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PKTC_OUTPUT}
    COMMAND ${PKTC_EXECUTABLE} --fuzz -o ${PKTC_OUTPUT}
            ${CMAKE_CURRENT_SOURCE_DIR}/natpmp.pkt
    COMMAND ${CMAKE_COMMAND} -E copy ${PKTC_OUTPUT}/natpmp_gen.c
            ${PKTC_OUTPUT}/natpmp_gen.h ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy ${PKTC_OUTPUT}/natpmp_fuzz.c
            ${PROJECT_SOURCE_DIR}/test
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/natpmp.pkt
    COMMENT "Generating the wire format from natpmp.pkt"
    VERBATIM)
endif()
