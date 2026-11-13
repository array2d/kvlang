# 组件：tests
# 作用：runtimec 的单元测试一键注册。**测试文件与它的注册必须同批提交**——只有
#       .c 没有 add_test 时，`ctest` 报 `No tests were found!!!` 且**退出码 0**，
#       CI 因此恒绿而一个测试都没跑过（这是真实发生过的假绿，见 TASK-086）。
# 联系：由上一级 CMakeLists.txt 的 `add_subdirectory(tests)` 引入；依赖各组件
#       target，不依赖任何测试框架（纯 C11 + main 返回码）。
#
# 约定：每个测试是一个可执行文件，`main` 返回非零即失败。不用 gtest/catch2——
# 引入框架会让「注册」这一步多一层，而这一步已经出过问题。

# 键比较器与排序缓存：纯内存，不需要后端。
add_executable(key_order_test key_order_test.c)
target_link_libraries(key_order_test PRIVATE runtimec_contract)

# Ptr 前缀解析：用假 reader，不需要后端。
add_executable(key_resolution_test key_resolution_test.c)
target_link_libraries(key_resolution_test PRIVATE runtimec_contract)

# 扩展算子注册：走公共接口 create，需要 engine 全链。
add_executable(extension_registration_test extension_registration_test.c)
target_link_libraries(extension_registration_test PRIVATE
    runtimec_interface runtimec_contract runtimec_binrwir runtimec_vmbin
    runtimec_vmkv runtimec_binrwir_compiler runtimec_engine)

# 目录枚举的前置失败路径：被测函数在 vmbin。
add_executable(directory_walk_open_failure_test directory_walk_open_failure_test.c)
target_link_libraries(directory_walk_open_failure_test PRIVATE
    runtimec_vmbin runtimec_contract runtimec_binrwir)

# 错误状态与错误文本的优先级（TASK-071）：公共入口的失败路径。
add_executable(error_status_priority_test error_status_priority_test.c)
target_link_libraries(error_status_priority_test PRIVATE
    runtimec_interface runtimec_contract runtimec_binrwir runtimec_vmbin
    runtimec_vmkv runtimec_binrwir_compiler runtimec_engine)

# 扩展读参的文本语义（TASK-070）：走 interface 的读参入口，需要 engine 全链。
add_executable(argument_text_wire_test argument_text_wire_test.c)
# 造 wire 要用语言层构造器（`pathlangXvalueHeadNew*`），其头在 pathlang/include。
target_include_directories(argument_text_wire_test PRIVATE
    "${PATHLANG_INCLUDE_DIRECTORY}")
target_link_libraries(argument_text_wire_test PRIVATE
    runtimec_interface runtimec_contract runtimec_binrwir runtimec_vmbin
    runtimec_vmkv runtimec_binrwir_compiler runtimec_engine)

foreach(test_target key_order_test key_resolution_test
                   extension_registration_test directory_walk_open_failure_test
                   argument_text_wire_test error_status_priority_test)
    target_compile_features(${test_target} PRIVATE c_std_11)
    target_include_directories(${test_target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
        "${RUNTIMEC_INCLUDE_DIRECTORY}")
    set_target_properties(${test_target} PROPERTIES
        BUILD_RPATH "${RUNTIMEC_PATHSPACE_LIBRARY_DIRECTORY}")
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(${test_target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    string(REPLACE "_test" "" test_name "${test_target}")
    add_test(NAME ${test_name} COMMAND ${test_target})
endforeach()
