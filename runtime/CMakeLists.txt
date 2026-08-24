cmake_minimum_required(VERSION 3.10)
project(kvlang-runtime C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ── 依赖 .so 由 ci/deps.sh 下载到 libso/（不再引用相邻仓库）────────
set(LIBSO_DIR "${CMAKE_SOURCE_DIR}/../libso")
set(KVSPACE_C_DIR "${LIBSO_DIR}/lib")
set(KVSPACE_DURABLE_DIR "${LIBSO_DIR}/lib")
set(BLOCKMALLOC_DIR "${LIBSO_DIR}/lib")
set(SLOTSBOXMALLOC_DIR "${LIBSO_DIR}/lib")

# ── kvspace 后端（kvspace-c 或 kvspace_durable，均导出同一 C ABI）────
set(KVSPACE_LIB "kvspace-c" CACHE STRING "kvspace 后端库：kvspace-c / kvspace_durable")

# ── 输出统一到 bin/ ──────────────────────────────────────────────
set(BIN_DIR "${CMAKE_SOURCE_DIR}/../bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${BIN_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${BIN_DIR}")

add_library(kvlang_runtime SHARED
    src/strbuf.c src/xvalue.c src/kv.c src/keytree.c src/rwir.c
    src/vthread.c src/logx.c src/builtin.c src/kvcpu.c src/runtime.c src/rwirext.c
    src/kindexpr.c)
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
# --export-dynamic 供 kvlang（term 扩展）找符号；--disable-new-dtags 使 rpath 转 DT_RPATH（传递），
# 让 runtime → kvspace-c → blockmalloc/slotsboxmalloc 的传递依赖能被 runtime 的 rpath 找到。
set_target_properties(kvlang_runtime PROPERTIES LINK_FLAGS "-Wl,--export-dynamic -Wl,--disable-new-dtags")
