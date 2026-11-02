# 组件：runtimec 顶层构建
# 作用：C11-only 构建（不链接 libstdc++）；按 contract、host、binrwir、
#       vmkv、binrwir_compiler、vmbin、engine、interface 生成独立 target，
#       并从 interface/exports.txt
#       生成三平台导出脚本（README §9、§10）。当前产物名刻意带 _scaffold，禁止用于
#       benchmark/tutorial/发布。
# 联系：组件依赖只沿本文件声明的单向图连接；JIT 是 BinRWIR Compiler
#       内部实现，只有 engine route 汇合 compiler 与 vmbin。
cmake_minimum_required(VERSION 3.20)

project(kvlang_runtimec_scaffold VERSION 0.1.0 LANGUAGES C)

include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

option(RUNTIMEC_ENABLE_JIT "Enable BinRWIR JIT compilation capability" ON)

set(RUNTIMEC_INCLUDE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(RUNTIMEC_GENERATED_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${RUNTIMEC_GENERATED_DIRECTORY}")

set(RUNTIMEC_EXPORT_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/interface/exports.txt")
file(STRINGS "${RUNTIMEC_EXPORT_MANIFEST}" RUNTIMEC_PUBLIC_SYMBOLS)
set(RUNTIMEC_ELF_EXPORTS "{\n  global:\n")
set(RUNTIMEC_MACHO_EXPORTS "")
set(RUNTIMEC_WINDOWS_EXPORTS "LIBRARY kvlang_runtimec_scaffold\nEXPORTS\n")
foreach(public_symbol IN LISTS RUNTIMEC_PUBLIC_SYMBOLS)
    string(APPEND RUNTIMEC_ELF_EXPORTS "    ${public_symbol};\n")
    string(APPEND RUNTIMEC_MACHO_EXPORTS "_${public_symbol}\n")
    string(APPEND RUNTIMEC_WINDOWS_EXPORTS "    ${public_symbol}\n")
endforeach()
string(APPEND RUNTIMEC_ELF_EXPORTS "  local: *;\n};\n")
file(WRITE "${RUNTIMEC_GENERATED_DIRECTORY}/exports.map" "${RUNTIMEC_ELF_EXPORTS}")
file(WRITE "${RUNTIMEC_GENERATED_DIRECTORY}/exports.macos" "${RUNTIMEC_MACHO_EXPORTS}")
file(WRITE "${RUNTIMEC_GENERATED_DIRECTORY}/exports.def" "${RUNTIMEC_WINDOWS_EXPORTS}")

function(runtimec_configure_target target_name)
    target_compile_features(${target_name} PRIVATE c_std_11)
    target_include_directories(${target_name}
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${RUNTIMEC_INCLUDE_DIRECTORY}")
    set_target_properties(${target_name} PROPERTIES
        C_VISIBILITY_PRESET hidden
        POSITION_INDEPENDENT_CODE ON)

    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(${target_name} PRIVATE
            -Wall -Wextra -Wpedantic -Werror
            -Wstrict-prototypes -Wmissing-prototypes)
    elseif(MSVC)
        target_compile_options(${target_name} PRIVATE /W4)
    endif()
endfunction()

# 每个组件必须拥有真实源文件。缺失组件说明目录或构建关系错误，配置阶段
# 立即失败；禁止用空翻译单元把缺失能力伪装成成功构建。
function(runtimec_add_component target_name)
    set(component_sources)
    foreach(component_path IN LISTS ARGN)
        file(GLOB_RECURSE path_sources CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/${component_path}/*.c")
        list(APPEND component_sources ${path_sources})
    endforeach()

    if(NOT component_sources)
        message(FATAL_ERROR
            "runtimec component ${target_name} has no source files")
    endif()

    add_library(${target_name} STATIC ${component_sources})
    runtimec_configure_target(${target_name})
endfunction()

runtimec_add_component(runtimec_contract contract)
runtimec_add_component(runtimec_host host)
runtimec_add_component(runtimec_binrwir binrwir)
runtimec_add_component(runtimec_vmkv vmkv)
runtimec_add_component(runtimec_binrwir_compiler binrwir_compiler)
runtimec_add_component(runtimec_vmbin vmbin)
runtimec_add_component(runtimec_engine engine)

target_include_directories(runtimec_contract
    PUBLIC
        "$<BUILD_INTERFACE:${RUNTIMEC_INCLUDE_DIRECTORY}>")
target_link_libraries(runtimec_host
    PRIVATE runtimec_contract)
target_link_libraries(runtimec_binrwir
    PRIVATE runtimec_contract)
target_link_libraries(runtimec_vmkv
    PRIVATE runtimec_contract runtimec_binrwir)
target_link_libraries(runtimec_binrwir_compiler
    PRIVATE runtimec_contract runtimec_binrwir)
target_link_libraries(runtimec_vmbin
    PRIVATE runtimec_contract runtimec_binrwir)
target_link_libraries(runtimec_engine
    PRIVATE
        runtimec_contract
        runtimec_host
        runtimec_binrwir
        runtimec_vmkv
        runtimec_binrwir_compiler
        runtimec_vmbin)
target_compile_definitions(runtimec_binrwir_compiler
    PRIVATE RUNTIMEC_ENABLE_JIT=$<BOOL:${RUNTIMEC_ENABLE_JIT}>)
target_compile_definitions(runtimec_engine
    PRIVATE RUNTIMEC_ENABLE_JIT=$<BOOL:${RUNTIMEC_ENABLE_JIT}>)

# 编译公共二进制接口实现一次，在静态接口组件和 scaffold 动态库间共享。
add_library(runtimec_interface_objects OBJECT interface/public_interface.c)
runtimec_configure_target(runtimec_interface_objects)
target_compile_definitions(runtimec_interface_objects
    PRIVATE KVLANG_RUNTIMEC_BUILDING_LIBRARY=1)

add_library(runtimec_interface STATIC
    $<TARGET_OBJECTS:runtimec_interface_objects>)
runtimec_configure_target(runtimec_interface)
target_link_libraries(runtimec_interface
    PUBLIC runtimec_contract
    PRIVATE runtimec_engine)

add_library(kvlang_runtimec_scaffold SHARED
    $<TARGET_OBJECTS:runtimec_interface_objects>)
target_include_directories(kvlang_runtimec_scaffold
    PUBLIC "$<BUILD_INTERFACE:${RUNTIMEC_INCLUDE_DIRECTORY}>")
target_link_libraries(kvlang_runtimec_scaffold
    PRIVATE runtimec_interface runtimec_engine)
set_target_properties(kvlang_runtimec_scaffold PROPERTIES
    C_VISIBILITY_PRESET hidden
    OUTPUT_NAME kvlang_runtimec_scaffold
    POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    target_link_options(kvlang_runtimec_scaffold PRIVATE
        "LINKER:-exported_symbols_list,${RUNTIMEC_GENERATED_DIRECTORY}/exports.macos")
    set_property(TARGET kvlang_runtimec_scaffold APPEND PROPERTY
        LINK_DEPENDS "${RUNTIMEC_GENERATED_DIRECTORY}/exports.macos")
elseif(WIN32)
    target_sources(kvlang_runtimec_scaffold PRIVATE
        "${RUNTIMEC_GENERATED_DIRECTORY}/exports.def")
elseif(UNIX)
    target_link_options(kvlang_runtimec_scaffold PRIVATE
        "LINKER:--version-script=${RUNTIMEC_GENERATED_DIRECTORY}/exports.map")
    set_property(TARGET kvlang_runtimec_scaffold APPEND PROPERTY
        LINK_DEPENDS "${RUNTIMEC_GENERATED_DIRECTORY}/exports.map")
endif()

add_library(kvlang::runtimec_contract ALIAS runtimec_contract)
add_library(kvlang::runtimec_host ALIAS runtimec_host)
add_library(kvlang::runtimec_binrwir ALIAS runtimec_binrwir)
add_library(kvlang::runtimec_vmkv ALIAS runtimec_vmkv)
add_library(kvlang::runtimec_binrwir_compiler ALIAS runtimec_binrwir_compiler)
add_library(kvlang::runtimec_vmbin ALIAS runtimec_vmbin)
add_library(kvlang::runtimec_engine ALIAS runtimec_engine)
add_library(kvlang::runtimec_interface ALIAS runtimec_interface)
add_library(kvlang::runtimec_scaffold ALIAS kvlang_runtimec_scaffold)

if(BUILD_TESTING)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        message(FATAL_ERROR "BUILD_TESTING requires runtimec/tests/CMakeLists.txt")
    endif()
    add_subdirectory(tests)
endif()
