# tutorial-c 同时用两层：kvspace 的存储 ABI + kvlang 的 wire 构造器。
#
# 路径约定与 `kvlang/xvalue/CMakeLists.txt` 同一套，不另造：
#   KVLANG_KVSPACE_INCLUDE_DIRECTORY  kvspace 仓的 include/
#   KVLANG_KVSPACE_LIB_DIRECTORY      持有 libkvspace 的目录
#   KVLANG_XVALUE_LIBRARY_PATH        libkvlang_xvalue.so
cmake_minimum_required(VERSION 3.16)
project(kvlang_tutorial_c LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

get_filename_component(KVLANG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
# kvspace 的两个路径**必填，没有默认值**：兄弟目录布局是开发者本地的偶然状态，
# 不是契约。它已经制造过三类事故——CMake 静默命中兄弟仓里非 pin 的版本、CI 把仓库
# clone 到相邻目录之后就再也不敢动目录结构、pod 重建后路径全失效而报错不指向真因。
# 默认值一旦存在，正确路径就变成「没被默认值遮住的那条」，而没有人会去验它。
set(KVLANG_KVSPACE_INCLUDE_DIRECTORY "" CACHE PATH
    "kvspace 仓的 include/（提供 <kvspace/kvspace.h> 与 <kvspace/kvspace_xvalue.h>）；必填")
set(KVLANG_KVSPACE_LIB_DIRECTORY "" CACHE PATH
    "持有 libkvspace 的目录（通常是 kvspace 仓的 build/）；必填")
set(KVLANG_XVALUE_LIBRARY_PATH "${KVLANG_ROOT}/lib/libkvlang_xvalue.so" CACHE FILEPATH
    "libkvlang_xvalue.so（本仓产物，默认 lib/）")

if(NOT KVLANG_KVSPACE_INCLUDE_DIRECTORY OR NOT KVLANG_KVSPACE_LIB_DIRECTORY)
    message(FATAL_ERROR
        "缺少 -DKVLANG_KVSPACE_INCLUDE_DIRECTORY=<kvspace 仓的 include/> 或 "
        "-DKVLANG_KVSPACE_LIB_DIRECTORY=<持有 libkvspace 的目录>。\n"
        "例：cmake -S tutorial-c -B build/tutorial-c \\\n"
        "       -DKVLANG_KVSPACE_INCLUDE_DIRECTORY=/path/to/kvspace/include \\\n"
        "       -DKVLANG_KVSPACE_LIB_DIRECTORY=/path/to/kvspace/build \\\n"
        "       -DKVLANG_XVALUE_LIBRARY_PATH=/path/to/kvlang/lib/libkvlang_xvalue.so")
endif()

foreach(path IN ITEMS
        "${KVLANG_KVSPACE_INCLUDE_DIRECTORY}/kvspace/kvspace.h"
        "${KVLANG_KVSPACE_INCLUDE_DIRECTORY}/kvspace/kvspace_xvalue.h"
        "${KVLANG_ROOT}/kvlangxvalue/kvlangxvalue.h"
        "${KVLANG_XVALUE_LIBRARY_PATH}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "缺少 ${path}；检查 -DKVLANG_KVSPACE_INCLUDE_DIRECTORY= / "
            "-DKVLANG_KVSPACE_LIB_DIRECTORY= / -DKVLANG_XVALUE_LIBRARY_PATH= 是否指对。")
    endif()
endforeach()
find_library(KVLANG_KVSPACE_LIBRARY_PATH NAMES kvspace
    PATHS "${KVLANG_KVSPACE_LIB_DIRECTORY}" NO_DEFAULT_PATH)
if(NOT KVLANG_KVSPACE_LIBRARY_PATH)
    message(FATAL_ERROR "在 ${KVLANG_KVSPACE_LIB_DIRECTORY} 找不到 libkvspace")
endif()

# 一个接口 target 承载「两层都要」这件事：include 路径与两个库。
add_library(tutorial_c_abi INTERFACE)
target_include_directories(tutorial_c_abi INTERFACE
    "${KVLANG_ROOT}/include" "${KVLANG_ROOT}/kvlangxvalue"
    "${KVLANG_KVSPACE_INCLUDE_DIRECTORY}")
target_link_libraries(tutorial_c_abi INTERFACE
    "${KVLANG_KVSPACE_LIBRARY_PATH}" "${KVLANG_XVALUE_LIBRARY_PATH}")

foreach(name IN ITEMS 01_basic.c 05_integrity.c 06_multiprocess.c)
    get_filename_component(stem "${name}" NAME_WE)
    add_executable(${stem} "${name}")
    target_link_libraries(${stem} PRIVATE tutorial_c_abi)
endforeach()

add_executable(02_cpp 02_cpp.cpp)
target_link_libraries(02_cpp PRIVATE tutorial_c_abi)
