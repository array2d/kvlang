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

# Uses blockalloc header-only (#define *_IMPLEMENTATION in src/slotboxalloc.c);
# no shared library link needed, resolved via include paths.

enable_testing()
add_subdirectory(tutorial)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/packagex.cmake")
