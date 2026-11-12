cmake_minimum_required(VERSION 3.10)
project(blockmalloc VERSION 0.1.1 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(GNUInstallDirs)

# 共享库
add_library(blockmalloc SHARED
    src/blockmalloc.c
)

set_target_properties(blockmalloc PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)

target_include_directories(blockmalloc
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(blockmalloc PRIVATE ENABLE_LOG)
endif()

# 将安装 / 打包 逻辑移至 cmake/packagex.cmake
include(cmake/packagex.cmake)

# 测试
enable_testing()
add_subdirectory(tutorial)