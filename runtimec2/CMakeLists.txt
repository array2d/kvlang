cmake_minimum_required(VERSION 3.20)
project(kvlang_runtimec2 C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 不进入默认构建：显式 `cmake --build <dir> --target runtimec2`。
add_library(kvlang_runtimec2 SHARED EXCLUDE_FROM_ALL
    src/abi.cpp
    src/kvspace.cpp
    src/vm.cpp
    src/model.cpp
    src/valueio.cpp
    src/rwirext.cpp
    src/ops_gen.cpp
    src/ops_lookup.cpp
    src/codeseg.cpp
    src/value.cpp
    src/vm.cpp
    src/vmkv.cpp
    src/vmbin.cpp
    src/runtime.cpp)
add_custom_target(runtimec2 ALL DEPENDS kvlang_runtimec2)

# 性能探针开关：默认关（热路径零开销）；开时产 profiling 二进制（RC2_PROF 输出）。
option(RC2_PROF "Build runtimec2 with per-op rdtsc profiling" OFF)
if(RC2_PROF)
    target_compile_definitions(kvlang_runtimec2 PRIVATE RC2_PROF)
endif()

target_include_directories(kvlang_runtimec2 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../runtime/include   # ABI 头只读引用
    /usr/include)

target_compile_options(kvlang_runtimec2 PRIVATE
    -O3 -fno-exceptions -fno-rtti -fvisibility=hidden -fno-threadsafe-statics
    -Wall -Wextra)

if(APPLE)
    target_link_libraries(kvlang_runtimec2 PRIVATE kvspace)
    set_target_properties(kvlang_runtimec2 PROPERTIES
        OUTPUT_NAME "kvlang_runtimec2"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        INSTALL_RPATH "/usr/local/lib;/usr/local/lib/kvspace")
else()
    target_link_libraries(kvlang_runtimec2 PRIVATE kvspace)
    set_target_properties(kvlang_runtimec2 PROPERTIES
        OUTPUT_NAME "kvlang_runtimec2"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        LINK_FLAGS "-Wl,--disable-new-dtags -Wl,-rpath,/usr/lib -Wl,-rpath,/usr/lib/kvspace")
endif()

# install：装到 /usr/lib/libkvlang_runtimec2.so（与 C 版同目录）。
install(TARGETS kvlang_runtimec2
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

add_executable(m1_smoke tests/m1_smoke.c)
target_include_directories(m1_smoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../runtime/include /usr/include)
target_link_libraries(m1_smoke PRIVATE kvlang_runtimec2 kvspace)

enable_testing()
add_executable(ops_table tests/ops_table.cpp src/ops_gen.cpp)
target_include_directories(ops_table PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_test(NAME c2_ops_table COMMAND $<TARGET_FILE:ops_table>)
add_test(NAME c2_impl_db_check
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/impl_db_check.sh)
add_test(NAME c2_ops_gen_check
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/ops_gen_check.sh)
add_test(NAME c2_m1_smoke
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/m1_smoke.sh
                 $<TARGET_FILE:m1_smoke>)
add_test(NAME c2_symbol_parity
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/symbols.sh
                 $<TARGET_FILE:kvlang_runtimec2>)
