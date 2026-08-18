cmake_minimum_required(VERSION 3.10)
project(kvlang-runtime C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ── 依赖路径（相对 kvlang，不 hardcode 绝对路径）────────────────
get_filename_component(ARRAY2D "${CMAKE_SOURCE_DIR}/../.." ABSOLUTE)
set(KVSPACE_C_DIR "${ARRAY2D}/kvspace/build")
set(KVSPACE_DURABLE_DIR "${ARRAY2D}/kvspace-durable/target/release")
set(BLOCKMALLOC_DIR "${ARRAY2D}/blockmalloc/build")
set(SLOTSBOXMALLOC_DIR "${ARRAY2D}/slotsboxmalloc/build")

# ── kvspace 后端（kvspace-c 或 kvspace_durable，均导出同一 C ABI）────
set(KVSPACE_LIB "kvspace-c" CACHE STRING "kvspace 后端库：kvspace-c / kvspace_durable")

# ── 输出统一到 bin/ ──────────────────────────────────────────────
set(BIN_DIR "${CMAKE_SOURCE_DIR}/../bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${BIN_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${BIN_DIR}")

add_library(kvlang_runtime SHARED
    src/strbuf.c src/xvalue.c src/kv.c src/keytree.c src/rwir.c
    src/vthread.c src/logx.c src/builtin.c src/kvcpu.c src/runtime.c src/rwext.c
    src/type_expr.c)
target_include_directories(kvlang_runtime PUBLIC include src)
target_compile_definitions(kvlang_runtime PRIVATE _GNU_SOURCE)

if(KVSPACE_LIB STREQUAL "kvspace-c")
    target_link_directories(kvlang_runtime PRIVATE ${KVSPACE_C_DIR} ${BLOCKMALLOC_DIR} ${SLOTSBOXMALLOC_DIR})
    target_link_libraries(kvlang_runtime PRIVATE kvspace-c blockmalloc slotsboxmalloc)
    set_target_properties(kvlang_runtime PROPERTIES
        BUILD_RPATH "${KVSPACE_C_DIR};${BLOCKMALLOC_DIR};${SLOTSBOXMALLOC_DIR}")
else()
    target_link_directories(kvlang_runtime PRIVATE ${KVSPACE_DURABLE_DIR})
    target_link_libraries(kvlang_runtime PRIVATE kvspace_durable)
    set_target_properties(kvlang_runtime PROPERTIES BUILD_RPATH "${KVSPACE_DURABLE_DIR}")
endif()
target_link_libraries(kvlang_runtime PRIVATE m pthread)
# --export-dynamic 供 term 扩展找符号；--disable-new-dtags 使 rpath 转 DT_RPATH（传递），
# 让 run → runtime → kvspace-c → blockmalloc/slotsboxmalloc 的传递依赖能被 run/runtime 的 rpath 找到。
set_target_properties(kvlang_runtime PROPERTIES LINK_FLAGS "-Wl,--export-dynamic -Wl,--disable-new-dtags")

# ── 测试执行器 run：只链接 runtime（term 扩展是独立进程）──────────
add_executable(run test/main.c)
target_link_libraries(run PRIVATE kvlang_runtime)
set_target_properties(run PROPERTIES
    LINK_FLAGS "-Wl,--disable-new-dtags"
    BUILD_RPATH "${BIN_DIR};${KVSPACE_C_DIR};${BLOCKMALLOC_DIR};${SLOTSBOXMALLOC_DIR};${KVSPACE_DURABLE_DIR}")
