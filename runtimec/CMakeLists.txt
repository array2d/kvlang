
cmake_minimum_required(VERSION 3.20)
project(kvlang_runtimec VERSION 0.2.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

set(RUNTIMEC_INCLUDE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include")

set(KVSPACE_INCLUDE_DIRECTORY "" CACHE PATH
    "kvspace 仓的 include/（提供 <kvspace/kvspace_xvalue.h>）；必填")
if(NOT KVSPACE_INCLUDE_DIRECTORY)
    message(FATAL_ERROR
        "缺少 -DKVSPACE_INCLUDE_DIRECTORY=<kvspace 仓的 include/>。")
endif()

set(KVLANG_STALE_INCLUDE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../include/kvspace")
if(EXISTS "${KVLANG_STALE_INCLUDE_DIRECTORY}")
    message(FATAL_ERROR "kvlang/include/kvspace/ must not exist (stale copy of the "
        "kvspace headers).")
endif()
if(NOT EXISTS "${KVSPACE_INCLUDE_DIRECTORY}/kvspace/kvspace_xvalue.h")
    message(FATAL_ERROR
        "KVSPACE_INCLUDE_DIRECTORY=${KVSPACE_INCLUDE_DIRECTORY} 里没有 "
        "kvspace/kvspace_xvalue.h。")
endif()
get_filename_component(KVLANG_INCLUDE_DIRECTORY
    "${CMAKE_CURRENT_SOURCE_DIR}/../include" ABSOLUTE)

set(KVLANG_XVALUE_INCLUDE_DIRECTORY "${KVLANG_INCLUDE_DIRECTORY}")
if(NOT EXISTS "${KVLANG_XVALUE_INCLUDE_DIRECTORY}/kvlangxvalue/kvlangxvalue.h")
    message(FATAL_ERROR "runtimec requires kvlang/kvlangxvalue/kvlangxvalue.h "
        "(looked in ${KVLANG_XVALUE_INCLUDE_DIRECTORY}).")
endif()

set(RUNTIMEC_KVSPACE_LIBRARY_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../lib"
    CACHE PATH "directory holding libkvspace")
find_library(RUNTIMEC_KVSPACE_LIBRARY_PATH NAMES kvspace
    PATHS "${RUNTIMEC_KVSPACE_LIBRARY_DIRECTORY}" NO_DEFAULT_PATH)
if(NOT RUNTIMEC_KVSPACE_LIBRARY_PATH)
    message(FATAL_ERROR "cannot find libkvspace (searched "
        "${RUNTIMEC_KVSPACE_LIBRARY_DIRECTORY}).")
endif()

find_library(KVLANG_XVALUE_LIBRARY_PATH NAMES kvlang_xvalue
    PATHS "${RUNTIMEC_KVSPACE_LIBRARY_DIRECTORY}" NO_DEFAULT_PATH
    DOC "libkvlang_xvalue.so (built from kvlang/kvlangxvalue)")
if(NOT KVLANG_XVALUE_LIBRARY_PATH)
    message(FATAL_ERROR "cannot find libkvlang_xvalue.so (searched "
        "${RUNTIMEC_KVSPACE_LIBRARY_DIRECTORY}).")
endif()

function(runtimec_configure_target target_name)
    target_compile_features(${target_name} PRIVATE c_std_11)
    target_include_directories(${target_name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}" "${RUNTIMEC_INCLUDE_DIRECTORY}"
        "${KVSPACE_INCLUDE_DIRECTORY}" "${KVLANG_INCLUDE_DIRECTORY}"
        "${KVLANG_XVALUE_INCLUDE_DIRECTORY}")
    set_target_properties(${target_name} PROPERTIES
        C_VISIBILITY_PRESET hidden POSITION_INDEPENDENT_CODE ON)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(${target_name} PRIVATE
            -Wall -Wextra -Wpedantic -Werror
            -Wstrict-prototypes -Wmissing-prototypes)
    elseif(MSVC)
        target_compile_options(${target_name} PRIVATE /W4)
    endif()
endfunction()

function(runtimec_add_component target_name component_path)
    file(GLOB_RECURSE sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/${component_path}/*.c")
    if(NOT sources)
        message(FATAL_ERROR "runtimec component ${target_name} has no source files")
    endif()
    add_library(${target_name} STATIC ${sources})
    runtimec_configure_target(${target_name})
endfunction()

runtimec_add_component(runtimec_binir binir)
runtimec_add_component(runtimec_binirgrouprun binirgrouprun)
runtimec_add_component(runtimec_vthread vthread)
runtimec_add_component(runtimec_runtime runtime)

target_link_libraries(runtimec_binir PRIVATE)
target_link_libraries(runtimec_binirgrouprun PRIVATE runtimec_binir)
target_link_libraries(runtimec_vthread PRIVATE "${RUNTIMEC_KVSPACE_LIBRARY_PATH}")
target_link_libraries(runtimec_runtime PRIVATE runtimec_vthread
    "${RUNTIMEC_KVSPACE_LIBRARY_PATH}" "${KVLANG_XVALUE_LIBRARY_PATH}")
foreach(alias_contract "runtimec_binir" "runtimec_binirgrouprun" "runtimec_vthread" "runtimec_runtime")
    add_library(kvlang::${alias_contract} ALIAS ${alias_contract})
endforeach()

enable_testing()
add_subdirectory(test)
