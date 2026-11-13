cmake_minimum_required(VERSION 3.10)
project(slotboxalloc VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(GNUInstallDirs)

# shared library (for FFI) + header-only (with SLOTBOXALLOC_IMPLEMENTATION)
add_library(slotboxalloc SHARED
    src/slotboxalloc.c
)
target_include_directories(slotboxalloc
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_definitions(slotboxalloc PRIVATE
    $<$<CONFIG:Debug>:ENABLE_LOG>
)

# 依赖 blockalloc 走 header-only（src/slotboxalloc.c 里 #define *_IMPLEMENTATION），
# 故不链接它的 .so；头的搜索路径由系统 include 提供。

enable_testing()
add_subdirectory(tutorial)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/packagex.cmake")
