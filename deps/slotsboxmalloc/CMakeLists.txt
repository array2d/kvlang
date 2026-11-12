cmake_minimum_required(VERSION 3.10)
project(slotsboxmalloc VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(GNUInstallDirs)

# shared library (for FFI) + header-only (with SLOTSBOXMALLOC_IMPLEMENTATION)
add_library(slotsboxmalloc SHARED
    src/slotsboxmalloc.c
)
target_include_directories(slotsboxmalloc
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_definitions(slotsboxmalloc PRIVATE
    $<$<CONFIG:Debug>:ENABLE_LOG>
)

# 依赖 blockmalloc 走 header-only（src/slotsboxmalloc.c 里 #define *_IMPLEMENTATION），
# 故不链接它的 .so；头的搜索路径由系统 include 提供。

enable_testing()
add_subdirectory(tutorial)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/packagex.cmake")
