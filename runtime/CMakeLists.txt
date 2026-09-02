cmake_minimum_required(VERSION 3.10)
project(kvlang-runtime C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ── 输出统一到 bin/ ──────────────────────────────────────────────
set(BIN_DIR "${CMAKE_SOURCE_DIR}/../bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${BIN_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${BIN_DIR}")

file(GLOB KVLANG_RUNTIME_SRC CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.c")
add_library(kvlang_runtime SHARED ${KVLANG_RUNTIME_SRC})
target_include_directories(kvlang_runtime PUBLIC include src)
target_compile_definitions(kvlang_runtime PRIVATE _GNU_SOURCE)

# 链接 kvspace dispatch 前端（已安装 /usr/lib），运行期按 DSN 选后端，编译期不再选后端。
target_link_libraries(kvlang_runtime PRIVATE kvspace m pthread)

# --export-dynamic 供 kvlang（term 扩展）找符号；--disable-new-dtags 使 rpath 转 DT_RPATH（传递）。
set_target_properties(kvlang_runtime PROPERTIES LINK_FLAGS "-Wl,--export-dynamic -Wl,--disable-new-dtags")
