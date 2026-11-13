cmake_minimum_required(VERSION 3.10)
project(blockalloc VERSION 0.1.1 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(GNUInstallDirs)

# Shared library
add_library(blockalloc SHARED
    src/blockalloc.c
)

set_target_properties(blockalloc PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)

target_include_directories(blockalloc
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(blockalloc PRIVATE ENABLE_LOG)
endif()

# Install / packaging rules live in cmake/packagex.cmake
include(cmake/packagex.cmake)

# Tests
enable_testing()
add_subdirectory(tutorial)