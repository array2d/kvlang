# tutorial-c 同时用两层：pathspace 的存储 ABI + pathlang 的 wire 构造器。
#
# 路径约定与 `pathlang/xvalue/CMakeLists.txt` 同一套，不另造：
#   PATHLANG_PATHSPACE_INCLUDE_DIRECTORY  pathspace 仓的 include/
#   PATHLANG_PATHSPACE_LIB_DIRECTORY      持有 libpathspace 的目录
#   PATHLANG_XVALUE_LIBRARY_PATH        libpathlang_xvalue.a
cmake_minimum_required(VERSION 3.16)
project(pathlang_tutorial_c LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

get_filename_component(PATHLANG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
# pathspace 的两个路径**必填，没有默认值**：兄弟目录布局是开发者本地的偶然状态，
# 不是契约。它已经制造过三类事故——CMake 静默命中兄弟仓里非 pin 的版本、CI 把仓库
# clone 到相邻目录之后就再也不敢动目录结构、pod 重建后路径全失效而报错不指向真因。
# 默认值一旦存在，正确路径就变成「没被默认值遮住的那条」，而没有人会去验它。
set(PATHLANG_PATHSPACE_INCLUDE_DIRECTORY "" CACHE PATH
    "pathspace 仓的 include/（提供 <pathspace/pathspace.h> 与 <pathspace/pathspace_xvalue.h>）；必填")
set(PATHLANG_PATHSPACE_LIB_DIRECTORY "" CACHE PATH
    "持有 libpathspace 的目录（通常是 pathspace 仓的 build/）；必填")
set(PATHLANG_XVALUE_LIBRARY_PATH "${PATHLANG_ROOT}/lib/libpathlang_xvalue.a" CACHE FILEPATH
    "libpathlang_xvalue.a（本仓产物，默认 lib/）")

if(NOT PATHLANG_PATHSPACE_INCLUDE_DIRECTORY OR NOT PATHLANG_PATHSPACE_LIB_DIRECTORY)
    message(FATAL_ERROR
        "缺少 -DPATHLANG_PATHSPACE_INCLUDE_DIRECTORY=<pathspace 仓的 include/> 或 "
        "-DPATHLANG_PATHSPACE_LIB_DIRECTORY=<持有 libpathspace 的目录>。\n"
        "例：cmake -S tutorial-c -B build/tutorial-c \\\n"
        "       -DPATHLANG_PATHSPACE_INCLUDE_DIRECTORY=/path/to/pathspace/include \\\n"
        "       -DPATHLANG_PATHSPACE_LIB_DIRECTORY=/path/to/pathspace/build \\\n"
        "       -DPATHLANG_XVALUE_LIBRARY_PATH=/path/to/pathlang/lib/libpathlang_xvalue.a")
endif()

foreach(path IN ITEMS
        "${PATHLANG_PATHSPACE_INCLUDE_DIRECTORY}/pathspace/pathspace.h"
        "${PATHLANG_PATHSPACE_INCLUDE_DIRECTORY}/pathspace/pathspace_xvalue.h"
        "${PATHLANG_ROOT}/include/pathlang/xvalue.h"
        "${PATHLANG_XVALUE_LIBRARY_PATH}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "缺少 ${path}；检查 -DPATHLANG_PATHSPACE_INCLUDE_DIRECTORY= / "
            "-DPATHLANG_PATHSPACE_LIB_DIRECTORY= / -DPATHLANG_XVALUE_LIBRARY_PATH= 是否指对。")
    endif()
endforeach()
find_library(PATHLANG_PATHSPACE_LIBRARY_PATH NAMES pathspace
    PATHS "${PATHLANG_PATHSPACE_LIB_DIRECTORY}" NO_DEFAULT_PATH)
if(NOT PATHLANG_PATHSPACE_LIBRARY_PATH)
    message(FATAL_ERROR "在 ${PATHLANG_PATHSPACE_LIB_DIRECTORY} 找不到 libpathspace")
endif()

# 一个接口 target 承载「两层都要」这件事：include 路径与两个库。
add_library(tutorial_c_abi INTERFACE)
target_include_directories(tutorial_c_abi INTERFACE
    "${PATHLANG_ROOT}/include" "${PATHLANG_PATHSPACE_INCLUDE_DIRECTORY}")
target_link_libraries(tutorial_c_abi INTERFACE
    "${PATHLANG_PATHSPACE_LIBRARY_PATH}" "${PATHLANG_XVALUE_LIBRARY_PATH}")

foreach(name IN ITEMS 01_basic.c 05_integrity.c 06_multiprocess.c)
    get_filename_component(stem "${name}" NAME_WE)
    add_executable(${stem} "${name}")
    target_link_libraries(${stem} PRIVATE tutorial_c_abi)
endforeach()

add_executable(02_cpp 02_cpp.cpp)
target_link_libraries(02_cpp PRIVATE tutorial_c_abi)
