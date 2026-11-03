# runtimec scaffold acceptance tests. The public surface is one header and one
# structured interface; no historical header or symbol is accepted.
function(runtimec_test_warnings target_name)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(${target_name} PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    elseif(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /WX)
    endif()
endfunction()

add_executable(runtimec_headers_c_test public_headers_c_test.c)
target_compile_features(runtimec_headers_c_test PRIVATE c_std_11)
target_include_directories(runtimec_headers_c_test PRIVATE
    "${RUNTIMEC_INCLUDE_DIRECTORY}")
runtimec_test_warnings(runtimec_headers_c_test)
add_test(NAME runtimec.headers.c COMMAND runtimec_headers_c_test)
set_tests_properties(runtimec.headers.c PROPERTIES
    LABELS "runtimec;scaffold;headers")

add_executable(runtimec_contracts_test contracts_test.c)
target_compile_features(runtimec_contracts_test PRIVATE c_std_11)
target_include_directories(runtimec_contracts_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${RUNTIMEC_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_contracts_test PRIVATE
    runtimec_contract runtimec_binrwir)
runtimec_test_warnings(runtimec_contracts_test)
add_test(NAME runtimec.contracts COMMAND runtimec_contracts_test)
set_tests_properties(runtimec.contracts PROPERTIES
    LABELS "runtimec;scaffold;contracts")

add_executable(runtimec_host_contracts_test host_contracts_test.c)
target_compile_features(runtimec_host_contracts_test PRIVATE c_std_11)
target_include_directories(runtimec_host_contracts_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${RUNTIMEC_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_host_contracts_test PRIVATE
    runtimec_contract runtimec_host)
runtimec_test_warnings(runtimec_host_contracts_test)
add_test(NAME runtimec.host.contracts COMMAND runtimec_host_contracts_test)
set_tests_properties(runtimec.host.contracts PROPERTIES
    LABELS "runtimec;scaffold;host;contracts")

add_executable(runtimec_host_kvspace_adapter_test
    host_kvspace_adapter_test.c fake_kvspace.c)
target_compile_features(runtimec_host_kvspace_adapter_test PRIVATE c_std_11)
target_include_directories(runtimec_host_kvspace_adapter_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${RUNTIMEC_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_host_kvspace_adapter_test PRIVATE
    runtimec_contract runtimec_host)
runtimec_test_warnings(runtimec_host_kvspace_adapter_test)
add_test(NAME runtimec.host.kvspace_adapter
    COMMAND runtimec_host_kvspace_adapter_test)
set_tests_properties(runtimec.host.kvspace_adapter PROPERTIES
    LABELS "runtimec;scaffold;host;adapter")

# 真实 kvspace 后端用例：库路径由 RUNTIMEC_KVSPACE_LIBRARY 给出；没有库或
# 没有 shm 目录时返回 77 跳过，不让缺依赖看起来像通过。
add_executable(runtimec_host_kvspace_backend_test
    host_kvspace_backend_test.c)
target_compile_features(runtimec_host_kvspace_backend_test PRIVATE c_std_11)
target_include_directories(runtimec_host_kvspace_backend_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${RUNTIMEC_INCLUDE_DIRECTORY}")
find_package(Threads REQUIRED)
# 后端用例需要 POSIX 的 usleep/dlfcn：与库目标一样显式打开 _GNU_SOURCE，
# 而不是靠编译器默认方言。
target_compile_definitions(runtimec_host_kvspace_backend_test
    PRIVATE _GNU_SOURCE)
target_link_libraries(runtimec_host_kvspace_backend_test PRIVATE
    runtimec_contract runtimec_host "${CMAKE_DL_LIBS}" Threads::Threads)
runtimec_test_warnings(runtimec_host_kvspace_backend_test)
add_test(NAME runtimec.host.kvspace_backend
    COMMAND runtimec_host_kvspace_backend_test)

# head 布局探针：先判**库**，再判代码。
#
# 为什么它是 backend 组的前置：`lib/` 里可能同时存在新旧两份产物，装错位置或加载
# 到旧的那份时，后端用例会红，但**红的原因不是代码错**。2026-11-03 就发生过——
# 新前端 + 旧后端，每个写操作都在报 `invalid XValue`，看着像 layout 写错。
# 探针先造一个值看字节，把「库装错版本」在跑用例之前就分离出来。
#
# CMake 的 `DEPENDS` 只能表达「先跑」不能表达「失败则跳过」，所以这里：
#   1. 把它注册成独立用例（可单独 `ctest -R head_layout` 跑）；
#   2. 给 backend 用例挂 FIXTURES_REQUIRED，让 ctest 保证顺序，
#      且探针失败时 backend 用例被标成 **Not Run** 而不是「也失败」。
find_package(Python3 COMPONENTS Interpreter REQUIRED)
# 探针必须探**本项目实际链接的那一份**（RUNTIMEC_KVSPACE_LIBRARY_PATH），
# 不能让它用自己猜的默认路径——否则库装错位置时探针探的是另一份，给出「16 字节，
# 没问题」的假结论，正是它要防的事。
add_test(NAME runtimec.architecture.head_layout
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../tools/architecture/check_kvspace_head_layout.py"
            --frontend "${RUNTIMEC_KVSPACE_LIBRARY_PATH}")
set_tests_properties(runtimec.architecture.head_layout PROPERTIES
    LABELS "runtimec;scaffold;architecture"
    FIXTURES_SETUP runtimec_kvspace_head)

set_tests_properties(runtimec.host.kvspace_backend PROPERTIES
    SKIP_RETURN_CODE 77
    LABELS "runtimec;scaffold;host;backend"
    FIXTURES_REQUIRED runtimec_kvspace_head)


# 替身与真库的 wire 金标准比对：验「替身不漂移」。
# 替身是对真实实现的照抄，抄的那一刻对、漂移时不报错；2026-11-03 就发生过一次
# （替身的 new_slack 还在造 8 字节头，红被误认成产品 bug）。
# 两个可执行除了链接对象外完全相同：一个链 tests/fake_kvspace.c、一个链真库。
add_executable(runtimec_wire_golden_fake xvalue_wire_golden.c fake_kvspace.c)
target_compile_features(runtimec_wire_golden_fake PRIVATE c_std_11)
target_compile_definitions(runtimec_wire_golden_fake PRIVATE
    WIRE_GOLDEN_FAKE)
target_include_directories(runtimec_wire_golden_fake PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${RUNTIMEC_INCLUDE_DIRECTORY}"
    "${KVSPACE_INCLUDE_DIRECTORY}")
runtimec_test_warnings(runtimec_wire_golden_fake)

add_executable(runtimec_wire_golden_real xvalue_wire_golden.c)
target_compile_features(runtimec_wire_golden_real PRIVATE c_std_11)
target_include_directories(runtimec_wire_golden_real PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${RUNTIMEC_INCLUDE_DIRECTORY}"
    "${KVSPACE_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_wire_golden_real PRIVATE
    "${RUNTIMEC_KVSPACE_LIBRARY_PATH}")
set_target_properties(runtimec_wire_golden_real PROPERTIES
    BUILD_RPATH "${RUNTIMEC_KVSPACE_LIBRARY_DIRECTORY}")
runtimec_test_warnings(runtimec_wire_golden_real)

add_test(NAME runtimec.host.wire_golden
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/compare_wire_golden.sh"
            $<TARGET_FILE:runtimec_wire_golden_fake>
            $<TARGET_FILE:runtimec_wire_golden_real>)
set_tests_properties(runtimec.host.wire_golden PROPERTIES
    SKIP_RETURN_CODE 77
    LABELS "runtimec;scaffold;host;golden")

add_executable(runtimec_binrwir_compiler_contracts_test
    binrwir_compiler_contracts_test.c)
target_compile_features(runtimec_binrwir_compiler_contracts_test PRIVATE c_std_11)
target_include_directories(runtimec_binrwir_compiler_contracts_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${RUNTIMEC_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_binrwir_compiler_contracts_test PRIVATE
    runtimec_contract runtimec_binrwir runtimec_vmkv
    runtimec_binrwir_compiler)
runtimec_test_warnings(runtimec_binrwir_compiler_contracts_test)
add_test(NAME runtimec.binrwir_compiler.contracts
    COMMAND runtimec_binrwir_compiler_contracts_test)
set_tests_properties(runtimec.binrwir_compiler.contracts PROPERTIES
    LABELS "runtimec;scaffold;binrwir_compiler;contracts")

add_executable(runtimec_binrwir_value_types_test binrwir_value_types_test.c)
target_compile_features(runtimec_binrwir_value_types_test PRIVATE c_std_11)
target_include_directories(runtimec_binrwir_value_types_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${RUNTIMEC_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_binrwir_value_types_test PRIVATE
    runtimec_contract runtimec_binrwir)
runtimec_test_warnings(runtimec_binrwir_value_types_test)
add_test(NAME runtimec.binrwir.value_types
    COMMAND runtimec_binrwir_value_types_test)
set_tests_properties(runtimec.binrwir.value_types PROPERTIES
    LABELS "runtimec;scaffold;binrwir;value_types")

add_executable(runtimec_vmkv_reader_test vmkv_reader_test.c)
target_compile_features(runtimec_vmkv_reader_test PRIVATE c_std_11)
target_include_directories(runtimec_vmkv_reader_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${RUNTIMEC_INCLUDE_DIRECTORY}"
    "${KVSPACE_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_vmkv_reader_test PRIVATE
    runtimec_contract runtimec_binrwir runtimec_vmkv)
runtimec_test_warnings(runtimec_vmkv_reader_test)
add_test(NAME runtimec.vmkv.reader COMMAND runtimec_vmkv_reader_test)
set_tests_properties(runtimec.vmkv.reader PROPERTIES
    LABELS "runtimec;scaffold;vmkv;reader")

add_executable(runtimec_vmkv_encoder_test vmkv_encoder_test.c)
target_compile_features(runtimec_vmkv_encoder_test PRIVATE c_std_11)
target_include_directories(runtimec_vmkv_encoder_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${RUNTIMEC_INCLUDE_DIRECTORY}"
    "${KVSPACE_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_vmkv_encoder_test PRIVATE
    runtimec_contract runtimec_binrwir runtimec_vmkv)
runtimec_test_warnings(runtimec_vmkv_encoder_test)
add_test(NAME runtimec.vmkv.encoder COMMAND runtimec_vmkv_encoder_test)
set_tests_properties(runtimec.vmkv.encoder PROPERTIES
    LABELS "runtimec;scaffold;vmkv;encoder")

# 端到端里程碑：真 .kv → 真 layout 库 → 真 kvspace → vmkv 编码。
#
# 与其它 encoder 用例的区别：它们的输入是手搭的 fake store，只能证明「假输入
# 下成立」；本用例走真链路，才能回答「真实 layout 产出的槽形状与假树有没有差异」。
#
# 两个外部依赖都靠 dlopen 在运行期加载，缺失时用例自身返回 77 跳过（并打印
# 原因），不伪装通过：
#   - layout 库：仓库 lib/ 下的 libkvlanglayout（先 make layout）
#   - kvspace 库：RUNTIMEC_KVSPACE_LIBRARY_PATH（配置期已解析）
# 路径经 CMake 写死进用例，避免运行期再猜目录。
find_library(RUNTIMEC_TEST_LAYOUT_LIBRARY
    NAMES kvlanglayout
    PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../../lib"
    NO_DEFAULT_PATH)
# kv 用例运行器：以 kvlang 语法为单位的三态判据（pass/fail/blocked）。
#
# 与 C 用例的分工：C 用例证明「组件按契约工作」，本运行器证明「一段 kvlang 程序
# 能被正确执行」。三态里 blocked 不是 skip —— 它是**有名字的缺口**，必须打印
# 文件名 + pc + 算子名，按算子名分组后就是能力缺口表。
add_executable(runtimec_kv_runner kv_runner.c)
target_compile_features(runtimec_kv_runner PRIVATE c_std_11)
target_include_directories(runtimec_kv_runner PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${RUNTIMEC_INCLUDE_DIRECTORY}"
    "${KVSPACE_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_kv_runner PRIVATE
    runtimec_contract runtimec_host runtimec_binrwir runtimec_vmkv
    runtimec_engine "${CMAKE_DL_LIBS}")
runtimec_test_warnings(runtimec_kv_runner)
# 骨架阶段**不注册 ctest**：运行器还没接执行链，不产出 pass 数。
# 一个不执行的 runner 若进 ctest 并显示通过，就是「永远绿的 runner」——
# 与「Skipped 伪装成通过」同族。执行链接通、能真正判 pass/fail 后再注册。

add_executable(runtimec_vmkv_end_to_end_test vmkv_end_to_end_test.c)
target_compile_features(runtimec_vmkv_end_to_end_test PRIVATE c_std_11)
target_include_directories(runtimec_vmkv_end_to_end_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${RUNTIMEC_INCLUDE_DIRECTORY}"
    "${KVSPACE_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_vmkv_end_to_end_test PRIVATE
    runtimec_contract runtimec_host runtimec_binrwir runtimec_vmkv
    "${CMAKE_DL_LIBS}")
runtimec_test_warnings(runtimec_vmkv_end_to_end_test)
add_test(NAME runtimec.vmkv.end_to_end
    COMMAND runtimec_vmkv_end_to_end_test
            "${RUNTIMEC_TEST_LAYOUT_LIBRARY}"
            "${RUNTIMEC_KVSPACE_LIBRARY_PATH}")
set_tests_properties(runtimec.vmkv.end_to_end PROPERTIES
    SKIP_RETURN_CODE 77
    LABELS "runtimec;scaffold;vmkv;end_to_end")

add_executable(runtimec_binrwir_frame_bindings_test
    binrwir_frame_bindings_test.c)
target_compile_features(runtimec_binrwir_frame_bindings_test PRIVATE c_std_11)
target_include_directories(runtimec_binrwir_frame_bindings_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${RUNTIMEC_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_binrwir_frame_bindings_test PRIVATE
    runtimec_contract runtimec_binrwir)
runtimec_test_warnings(runtimec_binrwir_frame_bindings_test)
add_test(NAME runtimec.binrwir.frame_bindings
    COMMAND runtimec_binrwir_frame_bindings_test)
set_tests_properties(runtimec.binrwir.frame_bindings PROPERTIES
    LABELS "runtimec;scaffold;binrwir;frame_bindings")

add_executable(runtimec_binrwir_program_test binrwir_program_test.c)
target_compile_features(runtimec_binrwir_program_test PRIVATE c_std_11)
target_include_directories(runtimec_binrwir_program_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${RUNTIMEC_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_binrwir_program_test PRIVATE
    runtimec_contract runtimec_binrwir)
runtimec_test_warnings(runtimec_binrwir_program_test)
add_test(NAME runtimec.binrwir.program COMMAND runtimec_binrwir_program_test)
set_tests_properties(runtimec.binrwir.program PROPERTIES
    LABELS "runtimec;scaffold;binrwir;program")

add_executable(runtimec_binrwir_execution_plan_test
    binrwir_execution_plan_test.c)
target_compile_features(runtimec_binrwir_execution_plan_test PRIVATE c_std_11)
target_include_directories(runtimec_binrwir_execution_plan_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${RUNTIMEC_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_binrwir_execution_plan_test PRIVATE
    runtimec_contract runtimec_binrwir)
runtimec_test_warnings(runtimec_binrwir_execution_plan_test)
add_test(NAME runtimec.binrwir.execution_plan
    COMMAND runtimec_binrwir_execution_plan_test)
set_tests_properties(runtimec.binrwir.execution_plan PROPERTIES
    LABELS "runtimec;scaffold;binrwir;execution_plan")

add_executable(runtimec_vmbin_execute_test vmbin_execute_test.c)
target_compile_features(runtimec_vmbin_execute_test PRIVATE c_std_11)
target_include_directories(runtimec_vmbin_execute_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${RUNTIMEC_INCLUDE_DIRECTORY}"
    "${KVSPACE_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_vmbin_execute_test PRIVATE
    runtimec_contract runtimec_binrwir runtimec_vmkv runtimec_vmbin)
runtimec_test_warnings(runtimec_vmbin_execute_test)
add_test(NAME runtimec.vmbin.execute COMMAND runtimec_vmbin_execute_test)
set_tests_properties(runtimec.vmbin.execute PROPERTIES
    LABELS "runtimec;scaffold;vmbin;execute")

add_executable(runtimec_binrwir_format_test binrwir_format_test.c)
target_compile_features(runtimec_binrwir_format_test PRIVATE c_std_11)
target_include_directories(runtimec_binrwir_format_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${RUNTIMEC_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_binrwir_format_test PRIVATE
    runtimec_contract runtimec_binrwir)
runtimec_test_warnings(runtimec_binrwir_format_test)
add_test(NAME runtimec.binrwir.format COMMAND runtimec_binrwir_format_test)
set_tests_properties(runtimec.binrwir.format PROPERTIES
    LABELS "runtimec;scaffold;binrwir;format")

add_executable(runtimec_vmbin_call_test vmbin_call_test.c)
target_compile_features(runtimec_vmbin_call_test PRIVATE c_std_11)
target_include_directories(runtimec_vmbin_call_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${RUNTIMEC_INCLUDE_DIRECTORY}"
    "${KVSPACE_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_vmbin_call_test PRIVATE
    runtimec_contract runtimec_binrwir runtimec_vmbin)
runtimec_test_warnings(runtimec_vmbin_call_test)
add_test(NAME runtimec.vmbin.call COMMAND runtimec_vmbin_call_test)
set_tests_properties(runtimec.vmbin.call PROPERTIES
    LABELS "runtimec;scaffold;vmbin;call")

add_executable(runtimec_engine_repository_test engine_repository_test.c)
target_compile_features(runtimec_engine_repository_test PRIVATE c_std_11)
target_include_directories(runtimec_engine_repository_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${RUNTIMEC_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_engine_repository_test PRIVATE
    runtimec_contract runtimec_binrwir runtimec_binrwir_compiler
    runtimec_engine)
runtimec_test_warnings(runtimec_engine_repository_test)
add_test(NAME runtimec.engine.repository
    COMMAND runtimec_engine_repository_test)
set_tests_properties(runtimec.engine.repository PROPERTIES
    LABELS "runtimec;scaffold;engine;repository")

add_executable(runtimec_execution_window_test execution_window_test.c)
target_compile_features(runtimec_execution_window_test PRIVATE c_std_11)
target_include_directories(runtimec_execution_window_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${RUNTIMEC_INCLUDE_DIRECTORY}"
    "${KVSPACE_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_execution_window_test PRIVATE
    runtimec_contract runtimec_binrwir runtimec_vmkv
    runtimec_binrwir_compiler runtimec_vmbin runtimec_engine)
runtimec_test_warnings(runtimec_execution_window_test)
add_test(NAME runtimec.engine.execution_window COMMAND runtimec_execution_window_test)
# 用例自带 77 跳过分支：session 建不起来时宁可报跳过，也不伪装成通过。
# 当前编译器已落地，正常运行不会走到该分支。
set_tests_properties(runtimec.engine.execution_window PROPERTIES
    SKIP_RETURN_CODE 77
    LABELS "runtimec;scaffold;engine;execution_window")

add_executable(runtimec_public_interface_bootstrap_test
    public_interface_bootstrap_test.c)
target_compile_features(runtimec_public_interface_bootstrap_test
    PRIVATE c_std_11)
target_include_directories(runtimec_public_interface_bootstrap_test PRIVATE
    "${RUNTIMEC_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_public_interface_bootstrap_test PRIVATE
    kvlang_runtimec_scaffold)
runtimec_test_warnings(runtimec_public_interface_bootstrap_test)
add_test(NAME runtimec.interface.bootstrap
    COMMAND runtimec_public_interface_bootstrap_test)
set_tests_properties(runtimec.interface.bootstrap PROPERTIES
    SKIP_RETURN_CODE 77
    LABELS "runtimec;scaffold;interface;bootstrap")

add_executable(runtimec_fail_closed_test stub_fail_closed_test.c)
target_compile_features(runtimec_fail_closed_test PRIVATE c_std_11)
target_include_directories(runtimec_fail_closed_test PRIVATE
    "${RUNTIMEC_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_fail_closed_test PRIVATE
    kvlang_runtimec_scaffold)
runtimec_test_warnings(runtimec_fail_closed_test)
add_test(NAME runtimec.fail_closed COMMAND runtimec_fail_closed_test)
set_tests_properties(runtimec.fail_closed PROPERTIES
    LABELS "runtimec;scaffold")

include(CheckLanguage)
check_language(CXX)
if(CMAKE_CXX_COMPILER)
    enable_language(CXX)
    add_executable(runtimec_headers_cplusplus_test
        public_headers_cplusplus_test.cpp)
    target_compile_features(runtimec_headers_cplusplus_test PRIVATE cxx_std_17)
    target_include_directories(runtimec_headers_cplusplus_test PRIVATE
        "${RUNTIMEC_INCLUDE_DIRECTORY}")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(runtimec_headers_cplusplus_test PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME runtimec.headers.cplusplus
        COMMAND runtimec_headers_cplusplus_test)
    set_tests_properties(runtimec.headers.cplusplus PROPERTIES
        LABELS "runtimec;scaffold;headers")
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME runtimec.architecture.layers
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../tools/architecture/check_layers.py"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_test(NAME runtimec.architecture.names
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../tools/architecture/check_names.py"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_test(NAME runtimec.architecture.behavior_matrix
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../tools/architecture/check_behavior_matrix.py"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_test(NAME runtimec.architecture.layer_selftest
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../tools/architecture/check_layers.py"
            --selftest)
set_tests_properties(
    runtimec.architecture.layers
    runtimec.architecture.names
    runtimec.architecture.behavior_matrix
    runtimec.architecture.layer_selftest
    PROPERTIES LABELS "runtimec;scaffold;architecture")

if(UNIX)
    add_test(NAME runtimec.interface.exports
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/../tools/architecture/check_exports.py"
                --library "$<TARGET_FILE:kvlang_runtimec_scaffold>"
                --manifest "${CMAKE_CURRENT_SOURCE_DIR}/../interface/exports.txt")
    set_tests_properties(runtimec.interface.exports PROPERTIES
        LABELS "runtimec;scaffold;interface")
endif()
