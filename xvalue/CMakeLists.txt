cmake_minimum_required(VERSION 3.16)
project(pathlang_xvalue LANGUAGES C)

set(PATHLANG_XVALUE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_library(pathlang_xvalue STATIC
    "${PATHLANG_XVALUE_ROOT}/src/xvalue.c"
    "${PATHLANG_XVALUE_ROOT}/src/codec.c"
    "${PATHLANG_XVALUE_ROOT}/src/coord.c"
    "${PATHLANG_XVALUE_ROOT}/src/const.c")
set_target_properties(pathlang_xvalue PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(PATHLANG_PATHSPACE_INCLUDE_DIRECTORY "" CACHE PATH
    "pathspace include/ (provides <pathspace/pathspace_xvalue.h>); required")
if(NOT PATHLANG_PATHSPACE_INCLUDE_DIRECTORY)
    message(FATAL_ERROR
        "missing -DPATHLANG_PATHSPACE_INCLUDE_DIRECTORY=<pathspace include/>.\n"
        "e.g. cmake -S xvalue -B build/pathlang-xvalue \\\n"
        "       -DPATHLANG_PATHSPACE_INCLUDE_DIRECTORY=/path/to/pathspace/include \\\n"
        "       -DPATHLANG_PATHSPACE_LIB_DIRECTORY=/path/to/pathspace/build")
endif()
if(NOT EXISTS "${PATHLANG_PATHSPACE_INCLUDE_DIRECTORY}/pathspace/pathspace_xvalue.h")
    message(FATAL_ERROR
        "${PATHLANG_PATHSPACE_INCLUDE_DIRECTORY}/pathspace/pathspace_xvalue.h not found; "
        "PATHLANG_PATHSPACE_INCLUDE_DIRECTORY must point to the pathspace include/.")
endif()

target_include_directories(pathlang_xvalue
    PUBLIC
        $<BUILD_INTERFACE:${PATHLANG_XVALUE_ROOT}/include>
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${PATHLANG_PATHSPACE_INCLUDE_DIRECTORY}>
)

set_target_properties(pathlang_xvalue PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON)

target_compile_options(pathlang_xvalue PRIVATE -Werror=implicit-function-declaration)

set(PATHLANG_PATHSPACE_LIB_DIRECTORY "" CACHE PATH
    "directory containing libpathspace (usually the pathspace build/); required")
if(NOT PATHLANG_PATHSPACE_LIB_DIRECTORY)
    message(FATAL_ERROR
        "missing -DPATHLANG_PATHSPACE_LIB_DIRECTORY=<dir with libpathspace>.\n"
        "e.g. cmake -S xvalue -B build/pathlang-xvalue \\\n"
        "       -DPATHLANG_PATHSPACE_INCLUDE_DIRECTORY=/path/to/pathspace/include \\\n"
        "       -DPATHLANG_PATHSPACE_LIB_DIRECTORY=/path/to/pathspace/build")
endif()
find_library(PATHLANG_PATHSPACE_LIBRARY_PATH NAMES pathspace
    PATHS "${PATHLANG_PATHSPACE_LIB_DIRECTORY}" NO_DEFAULT_PATH)
if(NOT PATHLANG_PATHSPACE_LIBRARY_PATH)
    message(FATAL_ERROR
        "libpathspace not found in ${PATHLANG_PATHSPACE_LIB_DIRECTORY}.\n"
        "Build pathspace first (cmake -S /path/to/pathspace -B /path/to/pathspace/build &&"
        " cmake --build /path/to/pathspace/build), then point at its build/ dir.")
endif()
target_link_libraries(pathlang_xvalue PUBLIC "${PATHLANG_PATHSPACE_LIBRARY_PATH}")

include(CTest)
if(BUILD_TESTING)
    foreach(name xvalue codec coord)
        add_executable(test_${name} "${PATHLANG_XVALUE_ROOT}/tests/test_${name}.c")
        target_link_libraries(test_${name} PRIVATE pathlang_xvalue)
        add_test(NAME ${name} COMMAND test_${name})
    endforeach()

    add_executable(xvalue_wire_golden
        "${PATHLANG_XVALUE_ROOT}/tests/xvalue_wire_golden.c")
    target_link_libraries(xvalue_wire_golden PRIVATE pathlang_xvalue)
endif()

install(TARGETS pathlang_xvalue ARCHIVE DESTINATION lib)
install(DIRECTORY "${PATHLANG_XVALUE_ROOT}/include/" DESTINATION include)
