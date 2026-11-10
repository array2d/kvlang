cmake_minimum_required(VERSION 3.16)
project(kvlang_xvalue LANGUAGES C)

# kvlang 语言层的 XValue 库：把 langtype（tag 1）、构造器、坐标比较器从 kvspace
# 搬出来，kvspace 只留字节层与存储 ABI。
#
# 源码与头在仓根：../src/*.c、../include/kvlang/*.h。本目录只是构建入口——kvlang
# 仓不是 CMake 工程（各组件各自有工程），不在这里放第二个根 CMakeLists。
#
# **静态库**：语言层不是给外部 dlopen 的运行时组件，随 layout/runtimec/byteseek
# 编进各自产物。PIC 打开，以便被 .so 链（runtimec 的 libkvlang_runtime）。
set(KVLANG_XVALUE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_library(kvlang_xvalue STATIC
    "${KVLANG_XVALUE_ROOT}/src/xvalue.c"
    "${KVLANG_XVALUE_ROOT}/src/codec.c"
    "${KVLANG_XVALUE_ROOT}/src/coord.c"
    "${KVLANG_XVALUE_ROOT}/src/const.c")
set_target_properties(kvlang_xvalue PROPERTIES POSITION_INDEPENDENT_CODE ON)

# kvspace 的字节层头（kvspace_xvalue.h）只有一个定义点。**必须是 PUBLIC**：我们的
# 公共头 <kvlang/xvalue.h> include 它，消费者编自己的 TU 时也要找得到。
#
# 路径**必填，没有默认值**：兄弟目录布局是开发者本地的偶然状态，不是契约。
# 它已经制造过三类事故——CMake 静默命中兄弟仓里非 pin 的版本、CI 把仓库 clone 到
# 相邻目录之后就再也不敢动目录结构、pod 重建后路径全失效而报错不指向真因。
# 默认值一旦存在，正确路径就变成「没被默认值遮住的那条」，而没有人会去验它。
set(KVLANG_KVSPACE_INCLUDE_DIRECTORY "" CACHE PATH
    "kvspace 仓的 include/（提供 <kvspace/kvspace_xvalue.h>）；必填")
if(NOT KVLANG_KVSPACE_INCLUDE_DIRECTORY)
    message(FATAL_ERROR
        "缺少 -DKVLANG_KVSPACE_INCLUDE_DIRECTORY=<kvspace 仓的 include/>。\n"
        "例：cmake -S xvalue -B build/kvlang-xvalue \\\n"
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

# 门禁：隐式函数声明一律当错误——C 的老默认（隐式返回 int）会把返回指针的
# 构造器调用截断，崩在别处而不是在编译期点名。
target_compile_options(kvlang_xvalue PRIVATE -Werror=implicit-function-declaration)

# 语言层建在字节层之上：`kvlangXvalueHeadLangtype*` 内联调 `kvspaceXvalueGetAttr`。
# 这个依赖是**单向**的（kvlang → kvspace），kvspace 从不反向依赖 kvlang。
# 静态库不把 libkvspace 吞进来，只把它作为 INTERFACE 依赖传给最终链接者——
# 否则 runtimec 里会出现两份字节层实现。
set(KVLANG_KVSPACE_LIB_DIRECTORY "" CACHE PATH
    "libkvspace 所在目录（通常是 kvspace 仓的 build/）；必填")
if(NOT KVLANG_KVSPACE_LIB_DIRECTORY)
    message(FATAL_ERROR
        "缺少 -DKVLANG_KVSPACE_LIB_DIRECTORY=<libkvspace 所在目录>。\n"
        "例：cmake -S xvalue -B build/kvlang-xvalue \\\n"
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

include(CTest)
if(BUILD_TESTING)
    foreach(name xvalue codec coord)
        add_executable(test_${name} "${KVLANG_XVALUE_ROOT}/tests/test_${name}.c")
        target_link_libraries(test_${name} PRIVATE kvlang_xvalue)
        add_test(NAME ${name} COMMAND test_${name})
    endforeach()

    # wire 金标准：打印同一批操作的字节，供 kvspace-c / kvspace-durable 逐字节比对
    # （本仓 tests/compare_wire_golden.sh）。它不做断言，只产出可比对的文本
    # ——验的是「各方对同一份 head 定义的理解一致」，不是本仓自证。
    add_executable(xvalue_wire_golden
        "${KVLANG_XVALUE_ROOT}/tests/xvalue_wire_golden.c")
    target_link_libraries(xvalue_wire_golden PRIVATE kvlang_xvalue)
endif()

install(TARGETS kvlang_xvalue ARCHIVE DESTINATION lib)
install(DIRECTORY "${KVLANG_XVALUE_ROOT}/include/" DESTINATION include)
