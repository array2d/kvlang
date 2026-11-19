cmake_minimum_required(VERSION 3.16)
project(kvlang_xvalue LANGUAGES C)

set(KVLANG_XVALUE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_library(kvlang_xvalue SHARED
    "${CMAKE_CURRENT_SOURCE_DIR}/src/kvlangxvalue.c")
set_target_properties(kvlang_xvalue PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(KVLANG_KVSPACE_INCLUDE_DIRECTORY "" CACHE PATH
    "kvspace 仓的 include/（提供 <kvspace/kvspace_xvalue.h>）；必填")
if(NOT KVLANG_KVSPACE_INCLUDE_DIRECTORY)
    message(FATAL_ERROR
        "缺少 -DKVLANG_KVSPACE_INCLUDE_DIRECTORY=<kvspace 仓的 include/>。\n"
        "例：cmake -S kvlangxvalue -B build/kvlangxvalue \\\n"
        "       -DKVLANG_KVSPACE_INCLUDE_DIRECTORY=/path/to/kvspace/include \\\n"
        "       -DKVLANG_KVSPACE_LIB_DIRECTORY=/path/to/kvspace/build")
endif()
if(NOT EXISTS "${KVLANG_KVSPACE_INCLUDE_DIRECTORY}/kvspace/kvspace_xvalue.h")
    message(FATAL_ERROR
        "${KVLANG_KVSPACE_INCLUDE_DIRECTORY}/kvspace/kvspace_xvalue.h 不存在；"
        "KVLANG_KVSPACE_INCLUDE_DIRECTORY 要指向 kvspace 仓的 include/。")
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
    "libkvspace 所在目录（通常是 kvspace 仓的 build/）；必填")
if(NOT KVLANG_KVSPACE_LIB_DIRECTORY)
    message(FATAL_ERROR
        "缺少 -DKVLANG_KVSPACE_LIB_DIRECTORY=<libkvspace 所在目录>。\n"
        "例：cmake -S kvlangxvalue -B build/kvlangxvalue \\\n"
        "       -DKVLANG_KVSPACE_INCLUDE_DIRECTORY=/path/to/kvspace/include \\\n"
        "       -DKVLANG_KVSPACE_LIB_DIRECTORY=/path/to/kvspace/build")
endif()
find_library(KVLANG_KVSPACE_LIBRARY_PATH NAMES kvspace
    PATHS "${KVLANG_KVSPACE_LIB_DIRECTORY}" NO_DEFAULT_PATH)
if(NOT KVLANG_KVSPACE_LIBRARY_PATH)
    message(FATAL_ERROR
        "在 ${KVLANG_KVSPACE_LIB_DIRECTORY} 里找不到 libkvspace。\n"
        "先构建 kvspace 仓（cmake -S /path/to/kvspace -B /path/to/kvspace/build &&"
        " cmake --build /path/to/kvspace/build），再指向它的 build/ 目录。")
endif()
target_link_libraries(kvlang_xvalue PUBLIC "${KVLANG_KVSPACE_LIBRARY_PATH}")

add_subdirectory(kvi)

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

    add_executable(test_header_only
        "${KVLANG_XVALUE_ROOT}/tests/test_header_only.c")
    target_include_directories(test_header_only PRIVATE
        "${KVLANG_XVALUE_ROOT}/include"
        "${KVLANG_KVSPACE_INCLUDE_DIRECTORY}")
    target_link_libraries(test_header_only PRIVATE
        "${KVLANG_KVSPACE_LIBRARY_PATH}")
    add_test(NAME header_only COMMAND test_header_only)

    add_subdirectory(test)
endif()

install(TARGETS kvlang_xvalue LIBRARY DESTINATION lib)
install(DIRECTORY "${KVLANG_XVALUE_ROOT}/include/" DESTINATION include)
