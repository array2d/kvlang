# 组件：cli —— kvspace 的命令行探查工具。
#
# 只链接 runtimec_kvlangxvalue：类型逻辑在 kvlangxvalue，存储寻址在 kvspace，
# 本目录不含任何一份自己的类型判断（见 kvspace_cli.c 头注释）。
#
# 由顶层 `add_subdirectory(cli)` 引入，位置在 runtimec_kvlangxvalue 组件注册之后。

add_executable(kvspace_cli kvspace_cli.c)

# 与其它组件同一套警告门禁（-Wall -Wextra -Wpedantic -Werror）与 C11 标准。
#
# 注意 `runtimec_configure_target` 内部用的是 `CMAKE_CURRENT_SOURCE_DIR`——在函数里
# 它就是**调用点**的目录。在 cli/ 里调时那等于 `runtimec/cli`，而本文件要的是仓根
# （`kvlangxvalue/` 的头在仓根下）。所以先调它（拿警告与标准），再补正确路径；
# 两处 include 指向同一目录不冲突。
runtimec_configure_target(kvspace_cli)
target_include_directories(kvspace_cli PRIVATE "${PROJECT_SOURCE_DIR}")

target_link_libraries(kvspace_cli PRIVATE runtimec_kvlangxvalue
    "${RUNTIMEC_KVSPACE_LIBRARY_PATH}")

set_target_properties(kvspace_cli PROPERTIES
    BUILD_RPATH "${RUNTIMEC_KVSPACE_LIBRARY_DIRECTORY}"
    OUTPUT_NAME kvspace-cli)
