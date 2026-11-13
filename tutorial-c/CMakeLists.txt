# tutorial-c uses both layers: pathspace's storage ABI + pathlang's wire constructors.
#
# Reuse the same variable naming convention as `pathlang/xvalue/CMakeLists.txt`:
#   PATHLANG_PATHSPACE_INCLUDE_DIRECTORY  pathspace repo include/
#   PATHLANG_PATHSPACE_LIB_DIRECTORY      directory containing libpathspace
#   PATHLANG_XVALUE_LIBRARY_PATH          libpathlang_xvalue.a
cmake_minimum_required(VERSION 3.16)
project(pathlang_tutorial_c LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

get_filename_component(PATHLANG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
# Both pathspace paths are **required with no defaults**: a sibling-directory layout
# is an incidental local setup, not a contract. Relying on defaults previously caused
# three failure modes -- CMake silently picking up an unpinned sibling checkout, CI
# freezing directory layout after cloning side-by-side, and pod rebuilds breaking all
# paths with unhelpful diagnostics. Once a default exists, the real path is only tested
# when the default fails to mask it.
set(PATHLANG_PATHSPACE_INCLUDE_DIRECTORY "" CACHE PATH
    "pathspace include/ directory (providing <pathspace/pathspace.h> and <pathspace/pathspace_xvalue.h>); required")
set(PATHLANG_PATHSPACE_LIB_DIRECTORY "" CACHE PATH
    "directory containing libpathspace (typically pathspace build/); required")
set(PATHLANG_XVALUE_LIBRARY_PATH "${PATHLANG_ROOT}/lib/libpathlang_xvalue.a" CACHE FILEPATH
    "libpathlang_xvalue.a (repo artifact, defaults to lib/)")

if(NOT PATHLANG_PATHSPACE_INCLUDE_DIRECTORY OR NOT PATHLANG_PATHSPACE_LIB_DIRECTORY)
    message(FATAL_ERROR
        "Missing -DPATHLANG_PATHSPACE_INCLUDE_DIRECTORY=<pathspace include/> or "
        "-DPATHLANG_PATHSPACE_LIB_DIRECTORY=<directory containing libpathspace>.\n"
        "Example: cmake -S tutorial-c -B build/tutorial-c \\\n"
        "           -DPATHLANG_PATHSPACE_INCLUDE_DIRECTORY=/path/to/pathspace/include \\\n"
        "           -DPATHLANG_PATHSPACE_LIB_DIRECTORY=/path/to/pathspace/build \\\n"
        "           -DPATHLANG_XVALUE_LIBRARY_PATH=/path/to/pathlang/lib/libpathlang_xvalue.a")
endif()

foreach(path IN ITEMS
        "${PATHLANG_PATHSPACE_INCLUDE_DIRECTORY}/pathspace/pathspace.h"
        "${PATHLANG_PATHSPACE_INCLUDE_DIRECTORY}/pathspace/pathspace_xvalue.h"
        "${PATHLANG_ROOT}/include/pathlang/xvalue.h"
        "${PATHLANG_XVALUE_LIBRARY_PATH}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Missing ${path}; check -DPATHLANG_PATHSPACE_INCLUDE_DIRECTORY= / "
            "-DPATHLANG_PATHSPACE_LIB_DIRECTORY= / -DPATHLANG_XVALUE_LIBRARY_PATH=.")
    endif()
endforeach()
find_library(PATHLANG_PATHSPACE_LIBRARY_PATH NAMES pathspace
    PATHS "${PATHLANG_PATHSPACE_LIB_DIRECTORY}" NO_DEFAULT_PATH)
if(NOT PATHLANG_PATHSPACE_LIBRARY_PATH)
    message(FATAL_ERROR "libpathspace not found in ${PATHLANG_PATHSPACE_LIB_DIRECTORY}")
endif()

# Single interface target carrying both layers (include paths and libraries).
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
