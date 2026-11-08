add_executable(natpmpc main.c)

target_compile_features(natpmpc PRIVATE c_std_23)
target_compile_definitions(natpmpc PRIVATE _POSIX_C_SOURCE=200112L)

target_link_libraries(natpmpc
  PRIVATE
    natpmp
    natpmp_warnings)
