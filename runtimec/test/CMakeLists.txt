# 组件：test —— 运行期行为的最小验证，不是对实现的镜像。
#
# 输入是 kvspace 的 key（见 benchmark/cases/iops/iops.kv 的形态）：
# 测试从存储里取出 `.bin` 的格，再跑解释器主循环的取指主体。
add_executable(binrwirgroup_test binrwirgroup_test.c)
runtimec_configure_target(binrwirgroup_test)
target_include_directories(binrwirgroup_test PRIVATE
    "${PROJECT_SOURCE_DIR}" "${RUNTIMEC_INCLUDE_DIRECTORY}"
    "${KVSPACE_INCLUDE_DIRECTORY}" "${KVLANG_INCLUDE_DIRECTORY}")
target_link_libraries(binrwirgroup_test PRIVATE runtimec_binrwir
    "${RUNTIMEC_KVSPACE_LIBRARY_PATH}")
add_test(NAME binrwirgroup_test
         COMMAND binrwirgroup_test
                 "shm://${CMAKE_CURRENT_BINARY_DIR}/binrwirgroup.shm")

# xvalue_store：类型往返 + writeT 不猜类型 + 非法输入终止。
# 「终止」用 fork/waitpid 验（组件的契约是 abort，没有返回值可断言）。
add_executable(xvalue_store_test xvalue_store_test.c)
runtimec_configure_target(xvalue_store_test)
target_include_directories(xvalue_store_test PRIVATE
    "${PROJECT_SOURCE_DIR}" "${RUNTIMEC_INCLUDE_DIRECTORY}"
    "${KVSPACE_INCLUDE_DIRECTORY}")
target_link_libraries(xvalue_store_test PRIVATE runtimec_kvlangxvalue
    "${RUNTIMEC_KVSPACE_LIBRARY_PATH}")
add_test(NAME xvalue_store_test COMMAND xvalue_store_test)
