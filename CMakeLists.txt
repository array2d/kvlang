cmake_minimum_required(VERSION 3.20)
project(kvlang VERSION 0.1.0 LANGUAGES CXX)

# ── C++ standard ─────────────────────────────────────
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Compiler warnings ────────────────────────────────
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
elseif(MSVC)
    add_compile_options(/W4 /WX)
endif()

# ── kvlang library (C++ runtime) ─────────────────────
# C++ only implements the runtime: read from kvspace → kvcpu execute.
# Go handles the toolchain (parse/lower/layout).

file(GLOB_RECURSE KV_SOURCES
    "device/*.cpp"
    "keytree/*.cpp"
    "kvcpu/*.cpp"
    "logx/*.cpp"
    "op/*.cpp"
    "op/builtin/*.cpp"
    "op/dispatch/*.cpp"
    "vthread/*.cpp"
    "vtype/*.cpp"
)

add_library(kvlang_lib ${KV_SOURCES})

# All module directories are include paths (headers use #include "module/file.h")
target_include_directories(kvlang_lib PUBLIC ${CMAKE_SOURCE_DIR})

# ── CLI executable ───────────────────────────────────
add_executable(kvlang cmd/kvlang/main.cpp)
target_link_libraries(kvlang PRIVATE kvlang_lib)

# ── Tests (optional) ─────────────────────────────────
option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    # add_subdirectory(tests)
endif()
