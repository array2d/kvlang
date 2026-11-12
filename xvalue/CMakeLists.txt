cmake_minimum_required(VERSION 3.16)
project(kvlang_xvalue LANGUAGES C)

set(KVLANG_XVALUE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_library(kvlang_xvalue STATIC
    "${KVLANG_XVALUE_ROOT}/src/xvalue.c"
    "${KVLANG_XVALUE_ROOT}/src/codec.c"
    "${KVLANG_XVALUE_ROOT}/src/coord.c"
    "${KVLANG_XVALUE_ROOT}/src/const.c")
set_target_properties(kvlang_xvalue PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(KVLANG_KVSPACE_INCLUDE_DIRECTORY "" CACHE PATH
    "kvspace include/ (provides <kvspace/kvspace_xvalue.h>); required")
if(NOT KVLANG_KVSPACE_INCLUDE_DIRECTORY)
    message(FATAL_ERROR
        "missing -DKVLANG_KVSPACE_INCLUDE_DIRECTORY=<kvspace include/>.\n"
        "e.g. cmake -S xvalue -B build/kvlang-xvalue \\\n"
        "       -DKVLANG_KVSPACE_INCLUDE_DIRECTORY=/path/to/kvspace/include \\\n"
        "       -DKVLANG_KVSPACE_LIB_DIRECTORY=/path/to/kvspace/build")
endif()
if(NOT EXISTS "${KVLANG_KVSPACE_INCLUDE_DIRECTORY}/kvspace/kvspace_xvalue.h")
    message(FATAL_ERROR
        "${KVLANG_KVSPACE_INCLUDE_DIRECTORY}/kvspace/kvspace_xvalue.h not found; "
        "KVLANG_KVSPACE_INCLUDE_DIRECTORY must point to the kvspace include/.")
endif()

target_include_directories(kvlang_xvalue
    PUBLIC
        $<BUILD_INTERFACE:${KVLANG_XVALUE_ROOT}/include>
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${KVLANG_KVSPACE_INCLUDE_DIRECTORY}>
)

set_target_properties(kvlang_xvalue PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON)

target_compile_options(kvlang_xvalue PRIVATE -Werror=implicit-function-declaration)

set(KVLANG_KVSPACE_LIB_DIRECTORY "" CACHE PATH
    "directory containing libkvspace (usually the kvspace build/); required")
if(NOT KVLANG_KVSPACE_LIB_DIRECTORY)
    message(FATAL_ERROR
        "missing -DKVLANG_KVSPACE_LIB_DIRECTORY=<dir with libkvspace>.\n"
        "e.g. cmake -S xvalue -B build/kvlang-xvalue \\\n"
        "       -DKVLANG_KVSPACE_INCLUDE_DIRECTORY=/path/to/kvspace/include \\\n"
        "       -DKVLANG_KVSPACE_LIB_DIRECTORY=/path/to/kvspace/build")
endif()
find_library(KVLANG_KVSPACE_LIBRARY_PATH NAMES kvspace
    PATHS "${KVLANG_KVSPACE_LIB_DIRECTORY}" NO_DEFAULT_PATH)
if(NOT KVLANG_KVSPACE_LIBRARY_PATH)
    message(FATAL_ERROR
        "libkvspace not found in ${KVLANG_KVSPACE_LIB_DIRECTORY}.\n"
        "Build kvspace first (cmake -S /path/to/kvspace -B /path/to/kvspace/build &&"
        " cmake --build /path/to/kvspace/build), then point at its build/ dir.")
endif()
target_link_libraries(kvlang_xvalue PUBLIC "${KVLANG_KVSPACE_LIBRARY_PATH}")

include(CTest)
if(BUILD_TESTING)
    foreach(name xvalue codec coord)
        add_executable(test_${name} "${KVLANG_XVALUE_ROOT}/tests/test_${name}.c")
        target_link_libraries(test_${name} PRIVATE kvlang_xvalue)
        add_test(NAME ${name} COMMAND test_${name})
    endforeach()

    add_executable(xvalue_wire_golden
        "${KVLANG_XVALUE_ROOT}/tests/xvalue_wire_golden.c")
    target_link_libraries(xvalue_wire_golden PRIVATE kvlang_xvalue)
endif()

install(TARGETS kvlang_xvalue ARCHIVE DESTINATION lib)
install(DIRECTORY "${KVLANG_XVALUE_ROOT}/include/" DESTINATION include)
