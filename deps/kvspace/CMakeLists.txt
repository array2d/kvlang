cmake_minimum_required(VERSION 3.10)
project(kvspace VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

include(GNUInstallDirs)

# 默认 prefix=/usr；库装 <prefix>/lib/kvspace，与历史安装布局一致（已装的消费者不用改链接路径）。
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "" FORCE)
endif()

# kvspace 就是标准实现本身：没有 adapter/dispatch 层，也不 dlopen 任何后端。
# 单库直接包含 shm 引擎 + 字节层（head/attr）+ 存储常量。
# 语言层（构造器、langtype、坐标比较）不在本仓，见 <kvlang/xvalue.h>。
add_library(kvspace SHARED
    src/shm/kvspace.c
    src/xvalue_head.c
    src/const.c)
target_include_directories(kvspace PRIVATE src/shm)

# ── header-only ABI 依赖 ────────────────────────────────────────────
# src/shm/kvspace.c 直接 include 两个头（`#define *_IMPLEMENTATION` 后把实现体编进
# 本 TU）。从前 CMakeLists 一个字没提它们，全靠 `/usr/include` 隐式命中——于是换 pod
# 之后得到的是 `fatal error: blockmalloc/blockmalloc.h: No such file or directory`，
# 报错里看不出缺哪个仓、哪个版本、怎么修。
#
# **版本不写在这里**：pin 的唯一事实源是 `ci/deps.sh` 的两行。CMake 里再写一份就是
# 第二个真相，下次升级必漏一个。
#
# 候选按优先级：
#
#   1. <本文件>/../<name>/include   同树 deps/ 布局（论文 artifact 的自包含形态）
#   2. /usr/local/include           ci/deps.sh 的目标（macOS 也是这里）
#   3. /usr/include                 ci/deps.sh 的目标（Linux）
#
# 第 1 条只在 deps/ 目录树内成立：kvspace 位于 deps/kvspace，兄弟即 deps/blockmalloc。
# 独立检出 kvspace（无 deps/ 兄弟）时该路径不存在，find_path 自动落到 2、3，
# 与原始行为一致。
#
# 需要指定别的目录时用 `-DKVSPACE_<name>_INCLUDE_DIRECTORY=<dir>` 显式覆盖——
# 显式优先于全部候选，且命中结果会打印在配置日志里（见下方 message(STATUS)）。
#
# `NO_DEFAULT_PATH` 是必须的：不加的话 find_path 还会去编译器默认目录里扫，命中一个
# 同名但版本不符的目录时，症状是链接期或运行期才炸，而不是配置期点名。
foreach(header IN ITEMS blockmalloc/blockmalloc.h slotsboxmalloc/slotsboxobj.h)
    string(REGEX REPLACE "/[^/]+$" "" header_directory "${header}")
    set(pin_candidates
        "${CMAKE_CURRENT_SOURCE_DIR}/../${header_directory}/include"
        "/usr/local/include"
        "/usr/include")
    find_path(KVSPACE_${header_directory}_INCLUDE_DIRECTORY
        NAMES ${header}
        PATHS ${pin_candidates}
        NO_DEFAULT_PATH)
    if(NOT KVSPACE_${header_directory}_INCLUDE_DIRECTORY)
        # 候选逐行列出。写成 `${pin_candidates}` 会被 CMake 用 `;` 拼成一行
        # （`/usr/local/include;/usr/include`），一眼看不出是几个目录。
        set(candidate_lines "")
        set(candidate_index 1)
        foreach(candidate IN LISTS pin_candidates)
            string(APPEND candidate_lines "    ${candidate_index}. ${candidate}\n")
            math(EXPR candidate_index "${candidate_index} + 1")
        endforeach()
        message(FATAL_ERROR
            "找不到 <${header}>。\n"
            "  候选目录（均未命中，按优先级）：\n"
            "${candidate_lines}"
            "  修复（二选一）：\n"
            "    1. 跑 ./ci/deps.sh —— 版本 pin 在它里面（本文件不重复写版本号）；\n"
            "    2. 用 -DKVSPACE_${header_directory}_INCLUDE_DIRECTORY=<包含 ${header_directory}/ 的目录> 显式指定。")
    endif()
    message(STATUS "kvspace: <${header}> → ${KVSPACE_${header_directory}_INCLUDE_DIRECTORY}")
    target_include_directories(kvspace PRIVATE
        "${KVSPACE_${header_directory}_INCLUDE_DIRECTORY}")
endforeach()

# **kvspace 不依赖 kvlang**——这条是分层的方向，不是偏好：语言层依赖存储层，
# 反过来不行。测试造值也不借用语言层的构造器：它们用 `tests/shm/test_wire.h` 里
# 那几个本地拼 wire 的 inline（格式知识重复，由同一组测试矩阵约束，含与 kvlang
# 金标准的逐字节比对）。共享符号会把两个仓编译期绑死，那是更贵的耦合。

include(CTest)
if(BUILD_TESTING)
    # XValue 构造器与 codec 的测试随实现迁往 kvlang（`kvlang/xvalue/tests/`）。
    # kvspace 侧只保留字节层与存储契约的测试。

    # 存储契约 smoke：单库就是标准实现，直接链接即可，不再有「真/假后端」之分，
    # 也就不再需要独立构建目录与 dlopen 搜索路径。
    add_executable(test_storage_shm tests/test_storage_shm.c)
    target_include_directories(test_storage_shm PRIVATE include)
    target_link_libraries(test_storage_shm PRIVATE kvspace)
    add_test(NAME storage_shm_contract COMMAND test_storage_shm)
endif()

# shm 后端测试（原 kvspace-c/tests，已迁入 tests/shm/）。
# 统一 `shm_` 前缀：与前端测试有同名文件（xvalue_head、xvalue_wire_golden），
# 不前缀会在 ctest 里出现两个同名项，红的时候分不清是哪一个。
if(BUILD_TESTING)
    # 枚举 (ctest 名|源文件)：CMake 的 `;` 是列表分隔符，写进字符串会在
    # `foreach(IN LISTS)` 里被拆成两个元素（实测每个 pair 跑了两遍，第二次
    # 名字变成 "test_xxx" 就撞了前端 target）。故用 `|` 作内部分隔符。
    set(KVSPACE_SHM_TEST_PAIRS
        "art_scan|test_art_scan"
        "art_alignment|test_art_alignment"
        "anchor_hook|test_anchor_hook"
        "shm_split|test_shm_split"
        "storage_abi|test_storage_abi"
        # xvalue_head / new_wire_shm / xvalue_wire_golden 造的是语言层 wire，
        # 随构造器一起迁往 kvlang（`kvlang/xvalue/tests/`）。
        )
    foreach(pair IN LISTS KVSPACE_SHM_TEST_PAIRS)
        string(FIND "${pair}" "|" pair_sep)
        string(SUBSTRING "${pair}" 0 ${pair_sep} shm_name)
        math(EXPR pair_tail "${pair_sep} + 1")
        string(SUBSTRING "${pair}" ${pair_tail} -1 shm_source)
        add_executable(shm_${shm_name} tests/shm/${shm_source}.c)
        target_include_directories(shm_${shm_name} PRIVATE
            src/shm include)
        # 测试要调 P2_TEST_HOOKS 里的内部钩子（TestAnchorOf/ArtAlignCheck/
        # TestSetFormatVersion）。宏必须与库一致，否则头里的声明被 #ifdef 挡掉，
        # 调用方靠隐式声明链接成功——正是 -Werror=implicit-function-declaration
        # 要抓的那类缺口。
        target_compile_definitions(shm_${shm_name} PRIVATE P2_TEST_HOOKS)
        target_link_libraries(shm_${shm_name} PRIVATE kvspace)
        add_test(NAME shm_${shm_name} COMMAND shm_${shm_name})
    endforeach()

    # 导出面链接测试：逐个引用全部 16 个导出符号，缺一个即链接失败。
    #
    # 为什么单独一条：头文件声明与 .so 导出**各说各话**时，编译期与运行期都不会报错
    # ——`const.h` 曾声明 `kvspaceConst` 而实现/导出叫 `kvspaceConstStorage`，
    # 用头文件写代码的人「能编过、一链接就找不到符号」，而当时 tests/cli 里零覆盖。
    # 取地址是唯一在「不运行」的情况下就能挡住这类缺陷的形态：漏掉的符号没人调用，
    # 运行期测试永远不会红。
    add_executable(test_export_surface tests/test_export_surface.c)
    target_include_directories(test_export_surface PRIVATE include)
    target_link_libraries(test_export_surface PRIVATE kvspace)
    add_test(NAME export_surface COMMAND test_export_surface)
endif()

# 前端 SONAME：libkvspace.so.1
set_target_properties(kvspace PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
)

target_include_directories(kvspace
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# 活跃句柄表（退出兜底落盘）用 pthread 互斥。单库后没有 dlopen/dlsym，不再链 libdl。
find_package(Threads REQUIRED)
target_link_libraries(kvspace PRIVATE Threads::Threads)

# 生产库只导出公开头声明的符号；内部实现（kvspaceShm*、测试钩子）不是 API。
# 测试构建需要直接链接内部函数，故保持默认可见。
# 这条随原 kvspace-c target 一起在单库合并时丢失过一次——缺它的后果是 58 个
# 符号全部导出，其中 27 个 kvspaceShm* 内部实现泄漏到公开面。
# C_VISIBILITY_PRESET 在部分 CMake 版本/生成器上不会落到 C 的编译命令行
# （实测 flags.make 里没有 -fvisibility=hidden），故两个都设——属性管预期，
# 编译选项管实际。缺了它 27 个 kvspaceShm* 会全部泄漏到公开面。
if(NOT BUILD_TESTING)
    set_target_properties(kvspace PROPERTIES C_VISIBILITY_PRESET hidden)
    target_compile_options(kvspace PRIVATE -fvisibility=hidden)
endif()

target_compile_definitions(kvspace PRIVATE _GNU_SOURCE)

# 测试构建启用内部观测/注入钩子（生产库不含）：kvspaceShmTestAnchorOf、
# kvspaceShmArtAlignCheck、kvspaceShmTestSetFormatVersion。
if(BUILD_TESTING)
    target_compile_definitions(kvspace PRIVATE P2_TEST_HOOKS)
endif()

# 门禁：隐式函数声明一律当错误。
#
# 它抓的是「实现导出了、头里没声明」这类缺口——C 的老默认（隐式返回 int）
# 会把 `kvspaceNewInt64(...)` 的返回值当 int，指针被截断，崩在别处。
# 改成硬错误后，每个未声明的调用都在**编译期**点名。
target_compile_options(kvspace PRIVATE -Werror=implicit-function-declaration)

install(TARGETS kvspace
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/kvspace
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

configure_file(kvspace.pc.in ${CMAKE_CURRENT_BINARY_DIR}/kvspace.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/kvspace.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

# ── CLI（cli/ 独立 Cargo 项目）──────────────────────────────────────
# 走本仓 dispatch 前端的 C ABI，故须在 kvspace 目标之后构建；
# 安装到 <prefix>/bin（prefix 默认 /usr → /usr/bin，与 kvlang 的 CLI 一致）。
# Embed relative RUNPATH entries:
#   $ORIGIN/../lib                      -- release tarball (bin/ + lib/)
#   $ORIGIN/../<libdir>/kvspace         -- cmake --install (<prefix>/<libdir>/kvspace)
# Use @loader_path on macOS; escape $ as $$ for CMake.
# release.yml packages this binary directly without rebuilding.
#
# CLI 只做**存储**子命令（del/deltree/cp/cpdir/mkindex/clear/list/tree/head/attr），
# 因此只链 libkvspace，**不依赖 kvlang**。语言层命令（get/set/list --kind/tree --kind）
# 在 kvlang 仓自己的 CLI 里，那个 CLI 才同时链 libkvspace + libkvlang_xvalue。
find_program(CARGO_EXECUTABLE cargo)
if(CARGO_EXECUTABLE)
  set(KVSPACE_CLI_BIN "${CMAKE_CURRENT_SOURCE_DIR}/cli/target/release/kvspace")
  if(APPLE)
    set(KVSPACE_CLI_ORIGIN "@loader_path")
  else()
    set(KVSPACE_CLI_ORIGIN "$$ORIGIN")
  endif()
  add_custom_target(kvspace-cli ALL
    COMMAND ${CMAKE_COMMAND} -E env
            "RUSTFLAGS=-L native=$<TARGET_FILE_DIR:kvspace> -C link-arg=-Wl,-rpath,'${KVSPACE_CLI_ORIGIN}/../lib' -C link-arg=-Wl,-rpath,'${KVSPACE_CLI_ORIGIN}/../${CMAKE_INSTALL_LIBDIR}/kvspace'"
            "${CARGO_EXECUTABLE}" build --release
            --manifest-path "${CMAKE_CURRENT_SOURCE_DIR}/cli/Cargo.toml"
    BYPRODUCTS "${KVSPACE_CLI_BIN}"
    DEPENDS kvspace
    COMMENT "构建 kvspace CLI（cli/）")
  install(PROGRAMS "${KVSPACE_CLI_BIN}" DESTINATION ${CMAKE_INSTALL_BINDIR})
else()
  message(STATUS "未找到 cargo，跳过 kvspace CLI（cli/）")
endif()
