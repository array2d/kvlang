# 组件：test —— 运行期行为的最小验证，不是对实现的镜像。
#
# 输入是 kvspace 的 key（见 benchmark/cases/iops/iops.kv 的形态）：
# 测试从存储里取出 `.bin` 的格，再跑解释器主循环的取指主体。
add_executable(binirgroup_test binirgroup_test.c)
runtimec_configure_target(binirgroup_test)
target_include_directories(binirgroup_test PRIVATE
    "${PROJECT_SOURCE_DIR}" "${RUNTIMEC_INCLUDE_DIRECTORY}"
    "${KVSPACE_INCLUDE_DIRECTORY}" "${KVLANG_INCLUDE_DIRECTORY}")
target_link_libraries(binirgroup_test PRIVATE runtimec_binir
    "${RUNTIMEC_KVSPACE_LIBRARY_PATH}")
add_test(NAME binirgroup_test
         COMMAND binirgroup_test
                 "shm://${CMAKE_CURRENT_BINARY_DIR}/binirgroup.shm")
