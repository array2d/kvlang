cmake_minimum_required(VERSION 3.20)
project(kvlang_runtimec2 C CXX)

# 主干目录 src/{vmkv,vmbin,jit}/ 的私有头以 "<component>/x.hpp" 引用。
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 不进入默认构建：显式 `cmake --build <dir> --target runtimec2`。
# JIT 平台发射器：按 arch 只编译对应文件（emit_arch 单一定义）。
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|arm")
    set(JIT_ARCH_SRC src/jit/jit_arm.cpp)
else()
    set(JIT_ARCH_SRC src/jit/jit_x64.cpp)
endif()

add_library(kvlang_runtimec2 SHARED EXCLUDE_FROM_ALL
    src/abi.cpp
    src/kvspace.cpp
    src/valueio.cpp
    src/rwirext.cpp
    src/rwir_gen.cpp
    src/rwir_lookup.cpp
    src/codeseg.cpp
    src/vmbin/binir_codec.cpp
    src/jit/jit.cpp
    src/jit/jit_abi_adapter.cpp
    ${JIT_ARCH_SRC}
    src/vmbin/profile.cpp
    src/value.cpp
    src/vmkv.cpp
    src/vmkv_rwirruntimeimpl.cpp
    src/binir_dump.cpp
    src/runtime.cpp)
add_custom_target(runtimec2 ALL DEPENDS kvlang_runtimec2)

# 性能探针开关：默认关（热路径零开销）。
option(RUNTIMEC2_VMBIN_PROFILE "Build runtimec2 with per-op rdtsc profiling" OFF)
option(RC2_PROF "Compatibility alias for RUNTIMEC2_VMBIN_PROFILE" OFF)
if(RC2_PROF)
    set(RUNTIMEC2_VMBIN_PROFILE ON)
    message(STATUS "RC2_PROF maps to RUNTIMEC2_VMBIN_PROFILE")
endif()
if(RUNTIMEC2_VMBIN_PROFILE)
    target_compile_definitions(kvlang_runtimec2 PRIVATE RUNTIMEC2_VMBIN_PROFILE)
endif()

target_include_directories(kvlang_runtimec2 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../runtime/include   # ABI 头只读引用
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)          # deps 头（deps.sh 落 repo）

target_compile_options(kvlang_runtimec2 PRIVATE
    -O3 -fno-exceptions -fno-rtti -fvisibility=hidden -fno-threadsafe-statics
    -Wall -Wextra)

# 链接 kvspace dispatch 前端：产物只放 repo 内 lib/（后端 lib/kvspace/），不装 /usr。
# 链接期用 repo 内绝对路径，运行期 rpath 用相对自身的 $ORIGIN（库本身就在 lib/）。
set(LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../lib")
set(BACKEND_DIR "${LIB_DIR}/kvspace")
if(APPLE)
    target_link_directories(kvlang_runtimec2 PRIVATE "${LIB_DIR}" "${BACKEND_DIR}")
    target_link_libraries(kvlang_runtimec2 PRIVATE kvspace)
    set_target_properties(kvlang_runtimec2 PROPERTIES
        OUTPUT_NAME "kvlang_runtimec2"
        LIBRARY_OUTPUT_DIRECTORY "${LIB_DIR}"
        INSTALL_RPATH "@loader_path;@loader_path/kvspace"
        BUILD_WITH_INSTALL_RPATH ON)
else()
    target_link_directories(kvlang_runtimec2 PRIVATE "${LIB_DIR}" "${BACKEND_DIR}")
    target_link_libraries(kvlang_runtimec2 PRIVATE kvspace)
    set_target_properties(kvlang_runtimec2 PROPERTIES
        OUTPUT_NAME "kvlang_runtimec2"
        LIBRARY_OUTPUT_DIRECTORY "${LIB_DIR}"
        INSTALL_RPATH "\$ORIGIN;\$ORIGIN/kvspace"
        BUILD_WITH_INSTALL_RPATH ON
        LINK_FLAGS "-Wl,--disable-new-dtags")
endif()

add_executable(m1_smoke tests/m1_smoke.c)
target_include_directories(m1_smoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../runtime/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_directories(m1_smoke PRIVATE "${LIB_DIR}" "${BACKEND_DIR}")
target_link_libraries(m1_smoke PRIVATE kvlang_runtimec2 kvspace)

enable_testing()
add_executable(rwir_table tests/rwir_table.cpp src/rwir_gen.cpp)
target_include_directories(rwir_table PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# myrwircap 表导出：layout 产出 rwir 差集断言用。
add_executable(myrwircap_dump tests/myrwircap_dump.cpp src/rwir_gen.cpp)
target_include_directories(myrwircap_dump PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(segment_shape_dump
    tools/segment_shape_dump.cpp
    src/codeseg.cpp
    src/vmbin/binir_codec.cpp
    src/binir_dump.cpp
    src/vmbin/profile.cpp
    src/value.cpp
    src/valueio.cpp
    src/rwir_gen.cpp
    src/rwir_lookup.cpp)
target_include_directories(segment_shape_dump PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../runtime/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_compile_options(segment_shape_dump PRIVATE -O2 -Wall -Wextra)
target_link_directories(segment_shape_dump PRIVATE "${LIB_DIR}" "${BACKEND_DIR}")
target_link_libraries(segment_shape_dump PRIVATE kvspace)

# binir 序列打印（§2·8）：只编 binir_dump.cpp，不链接 runtime/kvspace。
add_executable(binir_dump_test
    tests/binir_dump_test.cpp src/binir_dump.cpp src/rwir_gen.cpp)
target_include_directories(binir_dump_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(binir_dump_test PRIVATE -O2 -Wall -Wextra)
add_test(NAME c2_binir_dump COMMAND binir_dump_test)

# E 层单测（#583 L3）：只依赖头文件，不链接 kvspace/runtime。
add_executable(register_desc_type_test tests/register_desc_type_test.cpp)
target_include_directories(register_desc_type_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(register_desc_type_test PRIVATE -O2 -Wall -Wextra)
add_test(NAME c2_register_desc_type COMMAND register_desc_type_test)

# binir 编解码往返（#583 L2）：不链 kvspace。
add_executable(binir_codec_test
    tests/binir_codec_test.cpp
    src/vmbin/binir_codec.cpp
    src/rwir_gen.cpp
    src/rwir_lookup.cpp)
target_include_directories(binir_codec_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(binir_codec_test PRIVATE -O2 -Wall -Wextra)
add_test(NAME c2_binir_codec COMMAND binir_codec_test
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/binir/real-shapes.txt)

add_test(NAME c2_rwir_table COMMAND $<TARGET_FILE:rwir_table>)
add_test(NAME c2_impl_db_check
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/impl_db_check.sh)
add_test(NAME c2_layout_ops_subset
         COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/layout_ops_diff.py
                 --layout ${CMAKE_CURRENT_SOURCE_DIR}/../bin/kvlanglayout
                 --byteseek ${CMAKE_CURRENT_SOURCE_DIR}/../bin/byteseek
                 --myrwircap-dump $<TARGET_FILE:myrwircap_dump>
                 --source-root ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_tests_properties(c2_layout_ops_subset PROPERTIES TIMEOUT 180)
add_test(NAME c2_unknown_rwir_rejected
         COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/unknown_rwir_test.py
                 --layout ${CMAKE_CURRENT_SOURCE_DIR}/../bin/kvlanglayout
                 --m1 $<TARGET_FILE:m1_smoke>
                 --source-root ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_tests_properties(c2_unknown_rwir_rejected PROPERTIES TIMEOUT 120)
add_test(NAME c2_m1_smoke
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/m1_smoke.sh
                 $<TARGET_FILE:m1_smoke>)
add_test(NAME c2_kvspace_dynamic_ops
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/kvspace_dynamic_ops.sh
                 $<TARGET_FILE:m1_smoke>
                 ${CMAKE_CURRENT_SOURCE_DIR}/../bin/byteseek)
set_tests_properties(c2_kvspace_dynamic_ops PROPERTIES TIMEOUT 120)
add_test(NAME c2_symbol_parity
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/symbols.sh
                 $<TARGET_FILE:kvlang_runtimec2>)
add_test(NAME c2_segment_shape_dump
         COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/segment_shape_dump_test.py
                 --exporter $<TARGET_FILE:segment_shape_dump>
                 --byteseek ${CMAKE_CURRENT_SOURCE_DIR}/../bin/byteseek
                 --probe ${CMAKE_CURRENT_SOURCE_DIR}/tools/binir_format_probe.py
                 --source-root ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_tests_properties(c2_segment_shape_dump PROPERTIES TIMEOUT 120)


# JIT verify 单测：jit.cpp 的 vmkv 依赖在测试内以最小桩替代。
add_executable(jit_verify_test
    tests/jit_verify_test.cpp
    src/jit/jit.cpp
    src/jit/jit_abi_adapter.cpp
    src/jit/jit_x64.cpp
    src/rwir_gen.cpp)
target_include_directories(jit_verify_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../runtime/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_compile_options(jit_verify_test PRIVATE -O2 -Wall -Wextra)
add_test(NAME c2_jit_verify COMMAND jit_verify_test)

# x86_64 发射器 ABI/canary 单测（只编 jit_x64.cpp，不链接 runtime）
add_executable(jit_x64_abi_test
    tests/jit_x64_abi_test.cpp
    src/jit/jit_x64.cpp)
target_include_directories(jit_x64_abi_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../runtime/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_compile_options(jit_x64_abi_test PRIVATE -O2 -Wall -Wextra)
add_test(NAME c2_jit_x64_abi COMMAND jit_x64_abi_test)

# 端到端 fail-closed：3 个 fixture，JIT 与 RC2_NOJIT 对拍 stdout/退出码。
add_test(NAME c2_jit_failclosed
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/jit_failclosed.sh
                 ${CMAKE_CURRENT_SOURCE_DIR}/../bin/byteseek
                 ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_tests_properties(c2_jit_failclosed PROPERTIES TIMEOUT 120)
