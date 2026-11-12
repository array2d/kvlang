# 组件：runtimec 顶层构建
# 作用：C11-only；按组件生成独立 target。当前五个组件：
#   kvlangxvalue / binrwir / binrwirgrouprun / vthread / runtime
#       另有一个导出的 ABI 库 kvlang_xvalue_store（源在 kvlangxvalue/ 里）与 cli/ 子目录。
cmake_minimum_required(VERSION 3.20)
project(kvlang_runtimec VERSION 0.2.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

set(RUNTIMEC_INCLUDE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include")
# XValue 的字节布局头在 kvspace；语言层头在本仓。
# **必填，没有默认值**：兄弟目录布局是开发者本地的偶然状态，不是契约。
set(KVSPACE_INCLUDE_DIRECTORY "" CACHE PATH
    "kvspace 仓的 include/（提供 <kvspace/kvspace_xvalue.h>）；必填")
if(NOT KVSPACE_INCLUDE_DIRECTORY)
    message(FATAL_ERROR
        "缺少 -DKVSPACE_INCLUDE_DIRECTORY=<kvspace 仓的 include/>。")
endif()
# kvspace 头只有一个定义点：`kvspace` 仓的 include/。kvlang 内的过期副本一旦重现，
# 「真头 16 字节、副本 8 字节」的局面就无法验证，故在此拦住。
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
# 语言层的唯一公共头：kvlang/kvlangxvalue/kvlangxvalue.h（一个头、一个 C）。
get_filename_component(KVLANG_XVALUE_INCLUDE_DIRECTORY
    "${CMAKE_CURRENT_SOURCE_DIR}/../kvlangxvalue" ABSOLUTE)
if(NOT EXISTS "${KVLANG_XVALUE_INCLUDE_DIRECTORY}/kvlangxvalue.h")
    message(FATAL_ERROR "runtimec requires kvlang/kvlangxvalue/kvlangxvalue.h "
        "(looked in ${KVLANG_XVALUE_INCLUDE_DIRECTORY}).")
endif()

# 语言层静态库：kvlang/xvalue 构建产出。
find_library(KVLANG_XVALUE_LIBRARY_PATH NAMES kvlang_xvalue
    PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../lib" NO_DEFAULT_PATH
    DOC "libkvlang_xvalue.so (built from kvlang/kvlangxvalue)")
if(NOT KVLANG_XVALUE_LIBRARY_PATH)
    message(FATAL_ERROR "cannot find libkvlang_xvalue.so (searched kvlang/lib).")
endif()

set(RUNTIMEC_KVSPACE_LIBRARY_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../lib"
    CACHE PATH "directory holding libkvspace")
find_library(RUNTIMEC_KVSPACE_LIBRARY_PATH NAMES kvspace
    PATHS "${RUNTIMEC_KVSPACE_LIBRARY_DIRECTORY}" NO_DEFAULT_PATH)
if(NOT RUNTIMEC_KVSPACE_LIBRARY_PATH)
    message(FATAL_ERROR "cannot find libkvspace (searched "
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

# 每个组件必须拥有真实源文件：缺失组件在配置期立即失败，禁止用空翻译单元
# 把缺失能力伪装成成功构建。
function(runtimec_add_component target_name component_path)
    file(GLOB_RECURSE sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/${component_path}/*.c")
    if(NOT sources)
        message(FATAL_ERROR "runtimec component ${target_name} has no source files")
    endif()
    add_library(${target_name} STATIC ${sources})
    runtimec_configure_target(${target_name})
endfunction()

# 五个组件**逐个显式声明，不用 foreach**：`check_layers.py` 按这里的声明顺序校验
# 单向依赖图，循环会让它读不到组件列表。
#
# `kvlangxvalue/xvalue_store.c` 属于 kvlangxvalue 组件（同一目录、同一层职责：
# 把 langtype 与字节对上），不单独立组件——单列会多一层只为一件文件存在的 target。
runtimec_add_component(runtimec_kvlangxvalue kvlangxvalue)
runtimec_add_component(runtimec_binrwir binrwir)
runtimec_add_component(runtimec_binrwirgrouprun binrwirgrouprun)
runtimec_add_component(runtimec_vthread vthread)
runtimec_add_component(runtimec_runtime runtime)

target_include_directories(runtimec_kvlangxvalue PUBLIC
    "$<BUILD_INTERFACE:${RUNTIMEC_INCLUDE_DIRECTORY}>"
    "$<BUILD_INTERFACE:${KVSPACE_INCLUDE_DIRECTORY}>")
target_link_libraries(runtimec_kvlangxvalue PRIVATE
    "${RUNTIMEC_KVSPACE_LIBRARY_PATH}" "${KVLANG_XVALUE_LIBRARY_PATH}")
target_link_libraries(runtimec_binrwir PRIVATE)
target_link_libraries(runtimec_binrwirgrouprun PRIVATE runtimec_binrwir)
target_link_libraries(runtimec_vthread PRIVATE runtimec_kvlangxvalue)
target_link_libraries(runtimec_runtime PRIVATE runtimec_vthread
    "${RUNTIMEC_KVSPACE_LIBRARY_PATH}")
# 导出的 ABI 库：把 xvalue_store 的公开符号给其他语言用。
#
# **只链接既有静态组件，不重复列源文件。** 从前这里写了一遍
# `xvalue_store/xvalue_store.c`——同一个源被编两次：一次进 STATIC（组件）、一次进
# SHARED。两份目标码各自持有静态变量与内联展开，符号表与真正被测试的那份实现是
# 两份东西，「测的是 A、发的是 B」。
#
# 静态组件带 POSITION_INDEPENDENT_CODE（见 runtimec_add_component → 全局
# CMAKE_POSITION_INDEPENDENT_CODE），可以直接链进 .so。
#
# 导出面由 `C_VISIBILITY_PRESET hidden` + 头里的 KVLANG_XVALUE_STORE_EXPORT 决定：
# 只有标了导出宏的 5 个 `kvlang_xvalue_store_*` 出现在动态符号表里。
# `add_library(SHARED)` 不接受空源列表，所以用 `$<TARGET_OBJECTS:...>` 取**静态
# 组件自己的目标码**——仍是同一份编译产物，不是第二遍编译。
add_library(kvlang_xvalue_store SHARED
    $<TARGET_OBJECTS:runtimec_kvlangxvalue>)
target_link_libraries(kvlang_xvalue_store PRIVATE runtimec_kvlangxvalue
    "${RUNTIMEC_KVSPACE_LIBRARY_PATH}")
target_include_directories(kvlang_xvalue_store PUBLIC
    "$<BUILD_INTERFACE:${RUNTIMEC_INCLUDE_DIRECTORY}>"
    "$<BUILD_INTERFACE:${KVSPACE_INCLUDE_DIRECTORY}>")
target_compile_definitions(kvlang_xvalue_store PRIVATE
    KVLANG_RUNTIMEC_BUILDING_LIBRARY)
set_target_properties(kvlang_xvalue_store PROPERTIES
    C_VISIBILITY_PRESET hidden OUTPUT_NAME kvlang_xvalue_store)

foreach(alias_contract "runtimec_kvlangxvalue" "runtimec_binrwir" "runtimec_binrwirgrouprun" "runtimec_vthread" "runtimec_runtime")
    add_library(kvlang::${alias_contract} ALIAS ${alias_contract})
endforeach()

enable_testing()
add_subdirectory(test)
# CLI 的 target 在它自己的 CMakeLists 里。**必须在 runtimec_xvalue_store 组件注册
# 之后**——它链接那个组件。
add_subdirectory(cli)
