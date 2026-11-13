# 组件：runtimec 顶层构建
# 作用：C11-only（不链 libstdc++）；按组件生成独立 target，并从 interface/exports.txt
#       生成三平台导出脚本。产物是 libpathlang_runtime，禁止用于 benchmark/tutorial/发布。
# 联系：依赖只沿本文件声明的单向图连接；JIT 是 Compiler 内部实现。
cmake_minimum_required(VERSION 3.20)
project(pathlang_runtimec_scaffold VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

option(RUNTIMEC_ENABLE_JIT "Enable BinRWIR JIT compilation capability" ON)
# 临时：输出算子（print/cerr）的就地实现，正式实现归 pathseek 扩展 runtime
# （issue #622 决策 D）。默认 OFF，**.so 产物必须关闭**。
option(RUNTIMEC_TEMP_OUTPUT "TEMPORARY: in-process 输出算子 for tutorial tests" OFF)
if(RUNTIMEC_TEMP_OUTPUT)
    message(STATUS "runtimec: TEMPORARY 输出算子 enabled (must be OFF for .so)")
endif()

set(RUNTIMEC_INCLUDE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include")
# XValue 的**字节布局**头在 pathspace；**语言层**头在本仓（TASK-082 从 pathspace 搬来：
# langtype、构造器、坐标比较器都是语言概念，存储层不该认识）。
#
# **必填，没有默认值**：兄弟目录布局是开发者本地的偶然状态，不是契约。它已经制造
# 过三类事故——CMake 静默命中兄弟仓里非 pin 的版本、CI 把仓库 clone 到相邻目录之后
# 就再也不敢动目录结构、pod 重建后路径全失效而报错不指向真因。默认值一旦存在，
# 正确路径就变成「没被默认值遮住的那条」，而没有人会去验它。
set(PATHSPACE_INCLUDE_DIRECTORY "" CACHE PATH
    "pathspace 仓的 include/（提供 <pathspace/pathspace_xvalue.h>）；必填")
if(NOT PATHSPACE_INCLUDE_DIRECTORY)
    message(FATAL_ERROR
        "缺少 -DPATHSPACE_INCLUDE_DIRECTORY=<pathspace 仓的 include/>。\n"
        "例：cmake -S runtimec -B build/runtimec \\\n"
        "       -DPATHSPACE_INCLUDE_DIRECTORY=/path/to/pathspace/include")
endif()
# pathspace 头只有一个定义点：`pathspace` 仓的 include/。pathlang 内的过期副本一旦重现，
# 「真头 16 字节、副本 8 字节」的局面就无法验证，故在此拦住。
set(PATHLANG_STALE_INCLUDE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../include/pathspace")
if(EXISTS "${PATHLANG_STALE_INCLUDE_DIRECTORY}")
    message(FATAL_ERROR "pathlang/include/pathspace/ must not exist (stale copy of the "
        "pathspace headers). Delete it; the single definition is "
        "${PATHSPACE_INCLUDE_DIRECTORY}/pathspace.")
endif()
if(NOT EXISTS "${PATHSPACE_INCLUDE_DIRECTORY}/pathspace/pathspace_xvalue.h")
    message(FATAL_ERROR
        "PATHSPACE_INCLUDE_DIRECTORY=${PATHSPACE_INCLUDE_DIRECTORY} 里没有 "
        "pathspace/pathspace_xvalue.h；它要指向 pathspace 仓的 include/。")
endif()
get_filename_component(PATHLANG_INCLUDE_DIRECTORY
    "${CMAKE_CURRENT_SOURCE_DIR}/../include" ABSOLUTE)
if(NOT EXISTS "${PATHLANG_INCLUDE_DIRECTORY}/pathlang/xvalue.h")
    message(FATAL_ERROR "runtimec requires pathlang/include/pathlang/xvalue.h "
        "(looked in ${PATHLANG_INCLUDE_DIRECTORY}/pathlang).")
endif()

# 语言层静态库：pathlang/xvalue 构建产出。构造器（pathlangXvalueHeadNew*）与坐标比较器
# 都在这里，不再是 libpathspace 的符号。
# 不要在这之前 `set(... "" CACHE FILEPATH)`：那会把缓存条目建成空串，
# find_library 见变量已定义就**直接跳过搜索**，于是默认路径永远找不到、
# 只有显式 -D 才能配置成功。让 find_library 自己建缓存条目：-D 仍然优先。
find_library(PATHLANG_XVALUE_LIBRARY_PATH NAMES pathlang_xvalue
    PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../lib" NO_DEFAULT_PATH
    DOC "libpathlang_xvalue.a (built from pathlang/xvalue)")
if(NOT PATHLANG_XVALUE_LIBRARY_PATH)
    message(FATAL_ERROR "cannot find libpathlang_xvalue.a (searched pathlang/lib). "
        "Fix: cmake -S pathlang/xvalue -B <dir> && cmake --build <dir>, then copy the "
        ".a into pathlang/lib or pass -DPATHLANG_XVALUE_LIBRARY_PATH=<path>.")
endif()

set(RUNTIMEC_GENERATED_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${RUNTIMEC_GENERATED_DIRECTORY}")

# 真库路径：存储访问直接链接它，不是测试替身。
set(RUNTIMEC_PATHSPACE_LIBRARY_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../lib"
    CACHE PATH "directory holding libpathspace")
find_library(RUNTIMEC_PATHSPACE_LIBRARY_PATH NAMES pathspace
    PATHS "${RUNTIMEC_PATHSPACE_LIBRARY_DIRECTORY}" NO_DEFAULT_PATH)
if(NOT RUNTIMEC_PATHSPACE_LIBRARY_PATH)
    message(FATAL_ERROR "cannot find libpathspace (searched "
        "${RUNTIMEC_PATHSPACE_LIBRARY_DIRECTORY}). Fix: build the pathspace include dir there "
        "(pathlang and pathspace must be siblings), or configure with "
        "-DRUNTIMEC_PATHSPACE_LIBRARY_DIRECTORY=<dir holding libpathspace>.")
endif()

# 三平台导出脚本：符号表只有 interface/exports.txt 一个来源。
set(RUNTIMEC_EXPORT_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/interface/exports.txt")
file(STRINGS "${RUNTIMEC_EXPORT_MANIFEST}" RUNTIMEC_PUBLIC_SYMBOLS)
set(elf "{\n  global:\n")
set(macho "")
set(windows "LIBRARY pathlang_runtime\nEXPORTS\n")
foreach(symbol IN LISTS RUNTIMEC_PUBLIC_SYMBOLS)
    string(APPEND elf "    ${symbol};\n")
    string(APPEND macho "_${symbol}\n")
    string(APPEND windows "    ${symbol}\n")
endforeach()
string(APPEND elf "  local: *;\n};\n")
file(WRITE "${RUNTIMEC_GENERATED_DIRECTORY}/exports.map" "${elf}")
file(WRITE "${RUNTIMEC_GENERATED_DIRECTORY}/exports.macos" "${macho}")
file(WRITE "${RUNTIMEC_GENERATED_DIRECTORY}/exports.def" "${windows}")

function(runtimec_configure_target target_name)
    target_compile_features(${target_name} PRIVATE c_std_11)
    target_include_directories(${target_name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}" "${RUNTIMEC_INCLUDE_DIRECTORY}"
        "${PATHSPACE_INCLUDE_DIRECTORY}" "${PATHLANG_INCLUDE_DIRECTORY}")
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

# 六行显式调用，**不用 foreach**：`check_layers.py` 按「组件声明顺序」校验单向
# 依赖图（`EXPECTED_COMPONENT_ORDER`），它读的是这六行的字面文本。改成循环会让
# 检查器读不到组件列表（实测报 `found ()`），那等于把一条门禁关掉。
runtimec_add_component(runtimec_contract contract)
runtimec_add_component(runtimec_binrwir binrwir)
runtimec_add_component(runtimec_vmkv vmkv)
runtimec_add_component(runtimec_binrwir_compiler binrwir_compiler)
runtimec_add_component(runtimec_vmbin vmbin)
runtimec_add_component(runtimec_engine engine)

# `vmkv/rwir_manifest.c` 用 `#include` 引入 `specification/*.def`，而 CMake 只追踪
# 源文件表里的 `.c/.h`。不显式登记，生成器重写后 `.o` 仍算新鲜，别名表/能力表停在
# 旧内容，新算子静默落成 FOREIGN（运行期才以「能力不存在」出现）。
file(GLOB rwir_specification_defs CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/specification/*.def")
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/vmkv/rwir_manifest.c"
    PROPERTIES OBJECT_DEPENDS "${rwir_specification_defs}")

# contract 与 vmbin 的 handler 都直接调 pathspace C ABI（用户裁决：不对 ABI 二次封装，
# 句柄就是 `void *`），故必须链真库。**只链前端 `libpathspace`**——pathspace-c /
# pathspace-durable 由 DSN scheme 运行期选择，编译期不出现。`m`：scalar 的 sqrt/exp/log。
target_include_directories(runtimec_contract PUBLIC
    "$<BUILD_INTERFACE:${RUNTIMEC_INCLUDE_DIRECTORY}>"
    "$<BUILD_INTERFACE:${PATHSPACE_INCLUDE_DIRECTORY}>")
# contract 与 vmbin 的 handler 都直接调 pathspace C ABI（不对 ABI 二次封装，句柄就是
# `void *`），**只链前端 `libpathspace`**——pathspace-c / pathspace-durable 由 DSN scheme
# 运行期选择，编译期不出现。`m`：scalar 的 sqrt/exp/log。
# contract 造值要用语言层构造器（pathlangXvalueHeadNew*），故一并链 pathlang_xvalue.a。
# 方向仍是「语言层 ← 运行时」，pathspace 不参与。
target_link_libraries(runtimec_contract PRIVATE
    "${RUNTIMEC_PATHSPACE_LIBRARY_PATH}" "${PATHLANG_XVALUE_LIBRARY_PATH}")
target_link_libraries(runtimec_binrwir PRIVATE runtimec_contract)
target_link_libraries(runtimec_vmkv PRIVATE runtimec_contract runtimec_binrwir)
target_link_libraries(runtimec_binrwir_compiler
    PRIVATE runtimec_contract runtimec_binrwir)
target_link_libraries(runtimec_vmbin PRIVATE runtimec_contract runtimec_binrwir
    "${RUNTIMEC_PATHSPACE_LIBRARY_PATH}" "${PATHLANG_XVALUE_LIBRARY_PATH}" m)
# engine 是组合根：connect pathspace 拿句柄，再借给 contract/pathspace_access。
target_link_libraries(runtimec_engine PRIVATE
    runtimec_contract runtimec_binrwir runtimec_vmkv
    runtimec_binrwir_compiler runtimec_vmbin "${RUNTIMEC_PATHSPACE_LIBRARY_PATH}")
foreach(target_name runtimec_binrwir_compiler runtimec_engine)
    target_compile_definitions(${target_name}
        PRIVATE RUNTIMEC_ENABLE_JIT=$<BOOL:${RUNTIMEC_ENABLE_JIT}>)
endforeach()

# 公共二进制接口只编译一次，静态组件与 scaffold 动态库共享。
# `extension_interface.c` 必须在这个 OBJECT 集合里：它唯一的调用者在 `.so` 外部，
# 靠 static 库按需拉取会**整个对象被丢弃**——症状是头里有声明、库里没符号。
add_library(runtimec_interface_objects OBJECT
    interface/public_interface.c engine/extension_interface.c)
runtimec_configure_target(runtimec_interface_objects)
target_compile_definitions(runtimec_interface_objects
    PRIVATE PATHLANG_RUNTIMEC_BUILDING_LIBRARY=1)

add_library(runtimec_interface STATIC $<TARGET_OBJECTS:runtimec_interface_objects>)
runtimec_configure_target(runtimec_interface)
target_link_libraries(runtimec_interface PUBLIC runtimec_contract PRIVATE runtimec_engine)

# 驱动不再复制三份源文件、不再靠 `RUNTIMEC_TEMP_OUTPUT` 宏：输出算子改由
# `tools/driver_output.c` 走扩展表注册——宏是「编进/编出」，扩展表是「注册/不注册」，
# 后者运行期可见可测。`.so` 与驱动的差别只剩「驱动多链一个源文件」。
add_executable(kv_runner cli/kv_runner.c tools/driver_output.c)
runtimec_configure_target(kv_runner)
target_link_libraries(kv_runner PRIVATE runtimec_contract runtimec_binrwir
    runtimec_vmbin runtimec_vmkv runtimec_binrwir_compiler runtimec_engine
    "${RUNTIMEC_PATHSPACE_LIBRARY_PATH}" "${CMAKE_DL_LIBS}" m)
set_target_properties(kv_runner PROPERTIES
    BUILD_RPATH "${RUNTIMEC_PATHSPACE_LIBRARY_DIRECTORY}")

# `pathlangrun`：benchmark 的两阶段驱动（`create <src>` 建 vthread、`run <vid>` 推进）。
# 与 `kv_runner` 同一个输出算子实现（`tools/driver_output.c`）、同一套 include/link；
# layout 经 dlopen 取，不链。
add_executable(pathlangrun cli/pathlangrun.c tools/driver_output.c)
runtimec_configure_target(pathlangrun)
target_link_libraries(pathlangrun PRIVATE runtimec_contract runtimec_binrwir
    runtimec_vmbin runtimec_vmkv runtimec_binrwir_compiler runtimec_engine
    "${RUNTIMEC_PATHSPACE_LIBRARY_PATH}" "${CMAKE_DL_LIBS}" m)
set_target_properties(pathlangrun PROPERTIES
    BUILD_RPATH "${RUNTIMEC_PATHSPACE_LIBRARY_DIRECTORY}")

add_library(pathlang_runtimec_scaffold SHARED
    $<TARGET_OBJECTS:runtimec_interface_objects>)
target_include_directories(pathlang_runtimec_scaffold
    PUBLIC "$<BUILD_INTERFACE:${RUNTIMEC_INCLUDE_DIRECTORY}>")
target_link_libraries(pathlang_runtimec_scaffold
    PRIVATE runtimec_interface runtimec_engine)
# **产物名是 `libpathlang_runtime.so`**（用户裁决 2026-11-10，TASK-059）。
# 内部 CMake target 名保留 `pathlang_runtimec_scaffold` 只为减少改动面——target 名
# 不是用户可见的名字，`OUTPUT_NAME` 才是。
set_target_properties(pathlang_runtimec_scaffold PROPERTIES
    C_VISIBILITY_PRESET hidden OUTPUT_NAME pathlang_runtime
    POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    target_link_options(pathlang_runtimec_scaffold PRIVATE
        "LINKER:-exported_symbols_list,${RUNTIMEC_GENERATED_DIRECTORY}/exports.macos")
    set_property(TARGET pathlang_runtimec_scaffold APPEND PROPERTY
        LINK_DEPENDS "${RUNTIMEC_GENERATED_DIRECTORY}/exports.macos")
elseif(WIN32)
    target_sources(pathlang_runtimec_scaffold PRIVATE
        "${RUNTIMEC_GENERATED_DIRECTORY}/exports.def")
elseif(UNIX)
    target_link_options(pathlang_runtimec_scaffold PRIVATE
        "LINKER:--version-script=${RUNTIMEC_GENERATED_DIRECTORY}/exports.map")
    set_property(TARGET pathlang_runtimec_scaffold APPEND PROPERTY
        LINK_DEPENDS "${RUNTIMEC_GENERATED_DIRECTORY}/exports.map")
endif()

foreach(alias_contract "runtimec_contract" "runtimec_binrwir" "runtimec_vmkv"
                       "runtimec_binrwir_compiler" "runtimec_vmbin"
                       "runtimec_engine" "runtimec_interface")
    add_library(pathlang::${alias_contract} ALIAS ${alias_contract})
endforeach()
add_library(pathlang::runtimec_scaffold ALIAS pathlang_runtimec_scaffold)

# **测试文件与它的注册是一组**：只留 .c 没有 add_test 时，`ctest` 报
# `No tests were found!!!` 且**退出码 0**，CI 因此恒绿而一个测试都没跑过
# （TASK-086 的假绿）。`enable_testing()` 必须在这里调——子目录里的 add_test
# 只有在祖先目录开启后才被 ctest 看到。
enable_testing()
add_subdirectory(tests)
