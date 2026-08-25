cmake_minimum_required(VERSION 3.10)
project(kvlang-runtime C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ── 输出统一到 bin/ ──────────────────────────────────────────────
set(BIN_DIR "${CMAKE_SOURCE_DIR}/../bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${BIN_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${BIN_DIR}")

add_library(kvlang_runtime SHARED
    src/strbuf.c src/xvalue.c src/kv.c src/keytree.c src/rwir.c
    src/vthread.c src/logx.c src/builtin.c src/kvcpu.c src/runtime.c src/rwirext.c
    src/kindexpr.c src/dispatch.c)
target_include_directories(kvlang_runtime PUBLIC include src)
target_compile_definitions(kvlang_runtime PRIVATE _GNU_SOURCE)

# 链接 kvspace dispatch 前端（已安装 /usr/lib），运行期按 DSN 选后端，编译期不再选后端。
target_link_libraries(kvlang_runtime PRIVATE kvspace m pthread)

# --export-dynamic 供 kvlang（term 扩展）找符号；--disable-new-dtags 使 rpath 转 DT_RPATH（传递）。
set_target_properties(kvlang_runtime PROPERTIES LINK_FLAGS "-Wl,--export-dynamic -Wl,--disable-new-dtags")

function(kvlang_runtime_test name src)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE kvlang_runtime pthread)
    target_include_directories(${name} PRIVATE include src)
    set_target_properties(${name} PROPERTIES BUILD_RPATH "${BIN_DIR}")
endfunction()

kvlang_runtime_test(kvlang_delegate_test tests/delegate_test.c)
kvlang_runtime_test(kvlang_notify_test tests/notify_contract_test.c)
kvlang_runtime_test(kvlang_incr_test tests/incr_test.c)
kvlang_runtime_test(kvlang_expire_test tests/expire_test.c)
kvlang_runtime_test(kvlang_watchany_test tests/watchany_test.c)
