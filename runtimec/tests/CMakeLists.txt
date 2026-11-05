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

# 真实 kvspace 后端用例：库路径由 RUNTIMEC_KVSPACE_LIBRARY 给出；没有库或
# 没有 shm 目录时返回 77 跳过，不让缺依赖看起来像通过。
add_executable(runtimec_host_kvspace_backend_test
 host_kvspace_backend_test.c)
target_compile_features(runtimec_host_kvspace_backend_test PRIVATE c_std_11)
target_include_directories(runtimec_host_kvspace_backend_test PRIVATE
 "${CMAKE_CURRENT_SOURCE_DIR}/.."
 "${RUNTIMEC_INCLUDE_DIRECTORY}")
find_package(Threads REQUIRED)
# 后端用例需要 POSIX 的 usleep：与库目标一样显式打开 _GNU_SOURCE。
target_compile_definitions(runtimec_host_kvspace_backend_test
 PRIVATE _GNU_SOURCE)
# kvspace 是**链接期依赖**：缺库在配置期就失败，不再有「运行时 SKIP 掉整条用例」
# 这条路。SKIP 只剩「连不上这个 DSN」这种真·环境问题。
target_link_libraries(runtimec_host_kvspace_backend_test PRIVATE
 runtimec_contract "${RUNTIMEC_KVSPACE_LIBRARY_PATH}" Threads::Threads)
set_target_properties(runtimec_host_kvspace_backend_test PROPERTIES
 BUILD_RPATH "${RUNTIMEC_KVSPACE_LIBRARY_DIRECTORY}")
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
# 1. 把它注册成独立用例（可单独 `ctest -R head_layout` 跑）；
# 2. 给 backend 用例挂 FIXTURES_REQUIRED，让 ctest 保证顺序，
# 且探针失败时 backend 用例被标成 **Not Run** 而不是「也失败」。
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
 runtimec_contract runtimec_binrwir runtimec_vmkv
 "${RUNTIMEC_KVSPACE_LIBRARY_PATH}")
set_target_properties(runtimec_vmkv_reader_test PROPERTIES
 BUILD_RPATH "${RUNTIMEC_KVSPACE_LIBRARY_DIRECTORY}")
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
# - layout 库：仓库 lib/ 下的 libkvlanglayout（先 make layout）
# - kvspace 库：RUNTIMEC_KVSPACE_LIBRARY_PATH（配置期已解析）
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
 runtimec_kv_runner_vmbin runtimec_kv_runner_vmkv runtimec_kv_runner_engine
 runtimec_contract runtimec_binrwir
 "${CMAKE_DL_LIBS}")
runtimec_test_warnings(runtimec_kv_runner)
# 三个 `runtimec_kv_runner_*` 副本在 runtimec/CMakeLists.txt 里定义（带
# RUNTIMEC_TEMP_PRINTLN）。这里只用，不再定义——**副本必须与产品库共用同一份
# 源文件表**，否则两边会漂移。
#
# 注册进 ctest，退出码三态（见 kv_runner.c 末尾）：
# 1 有失败
# 0 executed > 0 且全相符
# 77 executed == 0（全是 blocked / 只编过）—— CTest 的 SKIP 约定
#
# 为什么现在就能注册：77 不伪装成绿，也不制造长期红。等 println 接上、executed
# 大于 0，它会**自动**从 SKIP 转成 0 或 1——转换那一刻就是行为层真正开始工作
# 的时刻，谁都看得见。这比「等 executed > 0 再注册」好：后者看不到它什么时候
# 开始真的验。
#
# 需要 layout 与 kvspace 两个动态库的路径；缺一个就 SKIP（由 runner 自己返回 77，
# 不是在 CMake 里判断——判断逻辑只留一处）。
add_test(NAME runtimec.kv.corpus
 COMMAND runtimec_kv_runner
 "${CMAKE_CURRENT_SOURCE_DIR}/kv"
 "${RUNTIMEC_TEST_LAYOUT_LIBRARY}"
 "${RUNTIMEC_KVSPACE_LIBRARY_PATH}")
set_tests_properties(runtimec.kv.corpus PROPERTIES
 LABELS "runtimec;scaffold;corpus"
 SKIP_RETURN_CODE 77)

add_executable(runtimec_vmkv_end_to_end_test vmkv_end_to_end_test.c)
target_compile_features(runtimec_vmkv_end_to_end_test PRIVATE c_std_11)
target_include_directories(runtimec_vmkv_end_to_end_test PRIVATE
 "${CMAKE_CURRENT_SOURCE_DIR}/.."
 "${RUNTIMEC_INCLUDE_DIRECTORY}"
 "${KVSPACE_INCLUDE_DIRECTORY}")
# kvspace 是链接期依赖（缺库在配置期失败）；layout 仍是运行期 dlopen。
target_link_libraries(runtimec_vmkv_end_to_end_test PRIVATE
 runtimec_contract runtimec_binrwir runtimec_vmkv
 "${RUNTIMEC_KVSPACE_LIBRARY_PATH}" "${CMAKE_DL_LIBS}")
set_target_properties(runtimec_vmkv_end_to_end_test PROPERTIES
 BUILD_RPATH "${RUNTIMEC_KVSPACE_LIBRARY_DIRECTORY}")
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

# `kvspace·set` 的 wire 完整性：写出去的必须是完整 wire，不是裸 body。
# 断言比对**整条 wire**而非值相等——后者在 bug 存在时照样绿（body 字节可能
# 一字不差，缺的是 head 与 attr）。缺 kvspace 库时用例自身返回 77。
add_executable(runtimec_host_set_wire_test host_kvspace_set_wire_test.c)
target_compile_features(runtimec_host_set_wire_test PRIVATE c_std_11)
target_include_directories(runtimec_host_set_wire_test PRIVATE
 "${CMAKE_CURRENT_SOURCE_DIR}/.."
 "${RUNTIMEC_INCLUDE_DIRECTORY}"
 "${KVSPACE_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_host_set_wire_test PRIVATE
 runtimec_contract runtimec_binrwir runtimec_vmbin
 "${RUNTIMEC_KVSPACE_LIBRARY_PATH}")
set_target_properties(runtimec_host_set_wire_test PROPERTIES
 BUILD_RPATH "${RUNTIMEC_KVSPACE_LIBRARY_DIRECTORY}")
runtimec_test_warnings(runtimec_host_set_wire_test)
add_test(NAME runtimec.host.set_wire COMMAND runtimec_host_set_wire_test)
set_tests_properties(runtimec.host.set_wire PROPERTIES
 SKIP_RETURN_CODE 77
 LABELS "runtimec;scaffold;host;wire")

# 字面量池对齐：每条 wire 起点 8 字节对齐（body 继承）。x86 容忍未对齐所以
# 看不出问题，aarch64 是 UB，JIT 出单条 ldr 就真崩。
add_executable(runtimec_literal_alignment_test binrwir_literal_alignment_test.c)
target_compile_features(runtimec_literal_alignment_test PRIVATE c_std_11)
target_include_directories(runtimec_literal_alignment_test PRIVATE
 "${CMAKE_CURRENT_SOURCE_DIR}/.."
 "${RUNTIMEC_INCLUDE_DIRECTORY}"
 "${KVSPACE_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_literal_alignment_test PRIVATE
 runtimec_contract runtimec_binrwir)
runtimec_test_warnings(runtimec_literal_alignment_test)
add_test(NAME runtimec.binrwir.literal_alignment
 COMMAND runtimec_literal_alignment_test)
set_tests_properties(runtimec.binrwir.literal_alignment PROPERTIES
 LABELS "runtimec;scaffold;binrwir;alignment")

# 分派层不得覆盖 handler 写的诊断码：精确码（如 0x4304 值宽不符）必须原样保留。
add_executable(runtimec_dispatch_diagnostic_test
 binrwir_dispatch_diagnostic_test.c)
target_compile_features(runtimec_dispatch_diagnostic_test PRIVATE c_std_11)
target_include_directories(runtimec_dispatch_diagnostic_test PRIVATE
 "${CMAKE_CURRENT_SOURCE_DIR}/.."
 "${RUNTIMEC_INCLUDE_DIRECTORY}"
 "${KVSPACE_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_dispatch_diagnostic_test PRIVATE
 runtimec_contract runtimec_binrwir runtimec_vmbin)
runtimec_test_warnings(runtimec_dispatch_diagnostic_test)
add_test(NAME runtimec.binrwir.dispatch_diagnostic
 COMMAND runtimec_dispatch_diagnostic_test)

# 诊断命名空间不得混用：error.code 是下层的、exit.detail_code 是本层的，两者不同。
add_executable(runtimec_vmbin_diagnostic_namespace_test
 vmbin_diagnostic_namespace_test.c)
target_compile_features(runtimec_vmbin_diagnostic_namespace_test PRIVATE c_std_11)
target_include_directories(runtimec_vmbin_diagnostic_namespace_test PRIVATE
 "${CMAKE_CURRENT_SOURCE_DIR}/.."
 "${RUNTIMEC_INCLUDE_DIRECTORY}"
 "${KVSPACE_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_vmbin_diagnostic_namespace_test PRIVATE
 runtimec_contract runtimec_binrwir runtimec_vmbin)
runtimec_test_warnings(runtimec_vmbin_diagnostic_namespace_test)
add_test(NAME runtimec.vmbin.diagnostic_namespace
 COMMAND runtimec_vmbin_diagnostic_namespace_test)
set_tests_properties(runtimec.binrwir.dispatch_diagnostic PROPERTIES
 LABELS "runtimec;scaffold;binrwir;diagnostics")

# 特化轮次：同型命中改 opid、跨类型保留原 opid（不拒绝整条 Program）、幂等。
add_executable(runtimec_specialize_test binrwir_specialize_test.c)
target_compile_features(runtimec_specialize_test PRIVATE c_std_11)
target_include_directories(runtimec_specialize_test PRIVATE
 "${CMAKE_CURRENT_SOURCE_DIR}/.."
 "${RUNTIMEC_INCLUDE_DIRECTORY}"
 "${KVSPACE_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_specialize_test PRIVATE
 runtimec_contract runtimec_binrwir)
runtimec_test_warnings(runtimec_specialize_test)
add_test(NAME runtimec.binrwir.specialize COMMAND runtimec_specialize_test)
set_tests_properties(runtimec.binrwir.specialize PROPERTIES
 LABELS "runtimec;scaffold;binrwir;specialize")

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

# builtin 注册表契约：坏表（条目 handler 为空）与「好表但没这个 opid」必须是
# **两种可区分的失败**（INVALID vs UNSUPPORTED），不能共用一个信号。
# 纯内存、不连 kvspace、不建 session。
add_executable(runtimec_binrwir_registry_corrupt_test binrwir_registry_corrupt_test.c)
target_compile_features(runtimec_binrwir_registry_corrupt_test PRIVATE c_std_11)
target_include_directories(runtimec_binrwir_registry_corrupt_test PRIVATE
 "${CMAKE_CURRENT_SOURCE_DIR}/.."
 "${RUNTIMEC_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_binrwir_registry_corrupt_test PRIVATE
 runtimec_contract runtimec_binrwir runtimec_vmbin)
runtimec_test_warnings(runtimec_binrwir_registry_corrupt_test)
add_test(NAME runtimec.binrwir.registry_corrupt
 COMMAND runtimec_binrwir_registry_corrupt_test)
set_tests_properties(runtimec.binrwir.registry_corrupt PROPERTIES
 LABELS "runtimec;scaffold;binrwir;registry")

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


# 临时 println 的端到端验收：仅 RUNTIMEC_TEMP_PRINTLN=ON 时存在。
# 链路 publish → layout → execute → 断言；关联 layout 产物（check_layers 不约束
# tests/CMakeLists.txt，见其 iter_product_sources 只遍历 PRODUCT_DIRECTORIES）。
if(RUNTIMEC_TEMP_PRINTLN)
 add_executable(runtimec_temp_println_test temp_println_test.c)
 target_compile_features(runtimec_temp_println_test PRIVATE c_std_11)
 target_compile_definitions(runtimec_temp_println_test PRIVATE
 _GNU_SOURCE)
 target_include_directories(runtimec_temp_println_test PRIVATE
 "${CMAKE_CURRENT_SOURCE_DIR}/.."
 "${RUNTIMEC_INCLUDE_DIRECTORY}"
 "${KVLANG_INCLUDE_DIRECTORY}")
 target_link_libraries(runtimec_temp_println_test PRIVATE
 runtimec_contract runtimec_vmbin runtimec_engine
 "${CMAKE_DL_LIBS}")
 runtimec_test_warnings(runtimec_temp_println_test)
 add_test(NAME runtimec.temp.println
 COMMAND runtimec_temp_println_test
 "${RUNTIMEC_KVSPACE_LIBRARY_PATH}"
 "${KVLANG_LAYOUT_LIBRARY}")
 set_tests_properties(runtimec.temp.println PROPERTIES
 SKIP_RETURN_CODE 77
 LABELS "runtimec;temp;println")
endif()

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

add_executable(runtimec_invalid_input_test interface_invalid_input_test.c)
target_compile_features(runtimec_invalid_input_test PRIVATE c_std_11)
target_include_directories(runtimec_invalid_input_test PRIVATE
 "${RUNTIMEC_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_invalid_input_test PRIVATE
 kvlang_runtimec_scaffold)
runtimec_test_warnings(runtimec_invalid_input_test)
add_test(NAME runtimec.interface.invalid_input COMMAND runtimec_invalid_input_test)
set_tests_properties(runtimec.interface.invalid_input PROPERTIES
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
# handler 体积：运算类 >3 行、复杂类型 >5 行需人工审查；另含条目表自检与
# 族-算子对照表一致性。判据是「可审查性」——超限说明函数里在分岔。
add_test(NAME runtimec.architecture.handler_size
 COMMAND "${Python3_EXECUTABLE}"
 "${CMAKE_CURRENT_SOURCE_DIR}/../tools/architecture/check_handler_size.py"
 --source-root "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set_tests_properties(
 runtimec.architecture.layers
 runtimec.architecture.names
 runtimec.architecture.behavior_matrix
 runtimec.architecture.layer_selftest
 runtimec.architecture.handler_size
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

add_executable(runtimec_handler_behavior_test handler_behavior_test.c)
target_compile_features(runtimec_handler_behavior_test PRIVATE c_std_11)
target_include_directories(runtimec_handler_behavior_test PRIVATE
 "${CMAKE_CURRENT_SOURCE_DIR}/.."
 "${RUNTIMEC_INCLUDE_DIRECTORY}"
 "${KVSPACE_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_handler_behavior_test PRIVATE
 runtimec_contract runtimec_binrwir runtimec_vmbin)
runtimec_test_warnings(runtimec_handler_behavior_test)
add_test(NAME runtimec.vmbin.handler_behavior
 COMMAND runtimec_handler_behavior_test)
set_tests_properties(runtimec.vmbin.handler_behavior PROPERTIES
 LABELS "runtimec;scaffold;vmbin;behavior")

# 变参 `...` 的落点纯度：只许落在签名行锚点的 dynamic 字节，不许进参数类型串。
#
# 判据见 stdlib/kvlang/spec/03-类型系统/22-storetype-def族与签名行.kv 的「铁律」段。
# 依赖真 layout 库 + 真 kvspace；缺任一即返回 77。layout 拒载 fixture 算失败。
add_executable(runtimec_variadic_purity_test variadic_purity_test.c)
target_compile_features(runtimec_variadic_purity_test PRIVATE c_std_11)
target_compile_definitions(runtimec_variadic_purity_test PRIVATE _GNU_SOURCE)
target_include_directories(runtimec_variadic_purity_test PRIVATE
 "${CMAKE_CURRENT_SOURCE_DIR}/.."
 "${RUNTIMEC_INCLUDE_DIRECTORY}"
 "${KVSPACE_INCLUDE_DIRECTORY}")
# kvspace 是**链接期依赖**（缺库在配置期就失败）；layout 仍是运行期 dlopen。
target_link_libraries(runtimec_variadic_purity_test PRIVATE
 runtimec_contract "${RUNTIMEC_KVSPACE_LIBRARY_PATH}" "${CMAKE_DL_LIBS}")
set_target_properties(runtimec_variadic_purity_test PROPERTIES
 BUILD_RPATH "${RUNTIMEC_KVSPACE_LIBRARY_DIRECTORY}")
runtimec_test_warnings(runtimec_variadic_purity_test)
add_test(NAME runtimec.architecture.variadic_purity
 COMMAND runtimec_variadic_purity_test
 "unused-kvspace-now-linked" "${KVLANG_LAYOUT_LIBRARY}")
set_tests_properties(runtimec.architecture.variadic_purity PROPERTIES
 SKIP_RETURN_CODE 77
 LABELS "runtimec;scaffold;architecture;variadic")

# 以下三个测试源文件早已在 HEAD 里、能编译，但从未出现在任何 add_test 中——
# 「未注册」的后果与「注册被覆盖」一样：改坏了不会有任何东西红。
add_executable(runtimec_scalar_generated_test scalar_generated_test.c)
target_compile_features(runtimec_scalar_generated_test PRIVATE c_std_11)
target_include_directories(runtimec_scalar_generated_test PRIVATE
 "${CMAKE_CURRENT_SOURCE_DIR}/.."
 "${RUNTIMEC_INCLUDE_DIRECTORY}"
 "${KVSPACE_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_scalar_generated_test PRIVATE
 runtimec_contract runtimec_binrwir runtimec_vmbin)
runtimec_test_warnings(runtimec_scalar_generated_test)
add_test(NAME runtimec.vmbin.scalar_generated
 COMMAND runtimec_scalar_generated_test)
set_tests_properties(runtimec.vmbin.scalar_generated PROPERTIES
 LABELS "runtimec;scaffold;vmbin;scalar")

add_executable(runtimec_scalar_special_test scalar_special_test.c)
target_compile_features(runtimec_scalar_special_test PRIVATE c_std_11)
target_include_directories(runtimec_scalar_special_test PRIVATE
 "${CMAKE_CURRENT_SOURCE_DIR}/.."
 "${RUNTIMEC_INCLUDE_DIRECTORY}"
 "${KVSPACE_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_scalar_special_test PRIVATE
 runtimec_contract runtimec_binrwir runtimec_vmbin)
runtimec_test_warnings(runtimec_scalar_special_test)
add_test(NAME runtimec.vmbin.scalar_special
 COMMAND runtimec_scalar_special_test)
set_tests_properties(runtimec.vmbin.scalar_special PROPERTIES
 LABELS "runtimec;scaffold;vmbin;scalar")

# 字符族：解码、码点判定、单字符编码、跨编码相等。
#
# 与 scalar_generated / scalar_special 分开成三个目标，是因为它们验的是**三层
# 不同的事实**：生成物的模板语义、helper 的判据、字符族的解码与编码。合成一个
# 目标时，一层红会被另一层的绿淹掉，看不出是哪层坏了。
add_executable(runtimec_scalar_character_test scalar_character_test.c)
target_compile_features(runtimec_scalar_character_test PRIVATE c_std_11)
target_include_directories(runtimec_scalar_character_test PRIVATE
 "${CMAKE_CURRENT_SOURCE_DIR}/.."
 "${RUNTIMEC_INCLUDE_DIRECTORY}"
 "${KVSPACE_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_scalar_character_test PRIVATE
 runtimec_contract runtimec_binrwir runtimec_vmbin)
runtimec_test_warnings(runtimec_scalar_character_test)
add_test(NAME runtimec.vmbin.scalar_character
 COMMAND runtimec_scalar_character_test)
set_tests_properties(runtimec.vmbin.scalar_character PROPERTIES
 LABELS "runtimec;scaffold;vmbin;scalar")

# 条目表：每个 opid 都能被 registry_find 找到，且正向/取反成对完整。
#
# 与 handler 行为测试分开：那个验「算得对不对」，这个验「在不在表里」。
# `scalar_entries` 是文件内 static，模块外只能经 `runtimec_scalar_handler_entries`
# 看见它——所以这条路径是唯一能证明「注册了」的方式，看源码里的表不算。
#
# 链接 kvspace 代理库：scalar.c 的 vthread_status 依赖它。
# `‥status` 读入口：形状校验、空值与坏槽的分界。
#
# J-5 之后解释器要靠它判「继续还是停」，而它的三种情形处置完全不同：
# 合法有内容 → 成功；**合法空内容 → 成功**（不是错误）；形状不符 → INVALID。
# 第二条最容易被写成错误，第三条最容易被漏——两条都有反向控制验证。
#
# 用真 kvspace 句柄（shm 后端），验的是真链路不是替身。
add_executable(runtimec_vthread_status_test vthread_status_test.c)
target_compile_features(runtimec_vthread_status_test PRIVATE c_std_11)
target_compile_definitions(runtimec_vthread_status_test PRIVATE _GNU_SOURCE)
target_include_directories(runtimec_vthread_status_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${RUNTIMEC_INCLUDE_DIRECTORY}"
    "${KVSPACE_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_vthread_status_test PRIVATE
    runtimec_contract runtimec_vmbin
    "${RUNTIMEC_KVSPACE_LIBRARY_PATH}")
set_target_properties(runtimec_vthread_status_test PROPERTIES
    BUILD_RPATH "${RUNTIMEC_KVSPACE_LIBRARY_DIRECTORY}")
runtimec_test_warnings(runtimec_vthread_status_test)
add_test(NAME runtimec.vmbin.vthread_status
    COMMAND runtimec_vthread_status_test)
set_tests_properties(runtimec.vmbin.vthread_status PROPERTIES
    LABELS "runtimec;scaffold;vmbin;status")

add_executable(runtimec_scalar_registry_test scalar_registry_test.c)
target_compile_features(runtimec_scalar_registry_test PRIVATE c_std_11)
target_include_directories(runtimec_scalar_registry_test PRIVATE
 "${CMAKE_CURRENT_SOURCE_DIR}/.."
 "${RUNTIMEC_INCLUDE_DIRECTORY}"
 "${KVSPACE_INCLUDE_DIRECTORY}")
# 链接**文件**而不是 `-lkvspace`：仓库里没有名为 libkvspace.so 的链接目录在
# 默认搜索路径上，裸写 `kvspace` 会在链接期报 `cannot find -lkvspace`。
# 这与 `runtimec_wire_golden_real` 是同一套写法，本文件其它用例都这么链。
target_link_libraries(runtimec_scalar_registry_test PRIVATE
 runtimec_contract runtimec_binrwir runtimec_vmbin
 "${RUNTIMEC_KVSPACE_LIBRARY_PATH}")
set_target_properties(runtimec_scalar_registry_test PROPERTIES
 BUILD_RPATH "${RUNTIMEC_KVSPACE_LIBRARY_DIRECTORY}")
runtimec_test_warnings(runtimec_scalar_registry_test)
add_test(NAME runtimec.vmbin.scalar_registry
 COMMAND runtimec_scalar_registry_test)
set_tests_properties(runtimec.vmbin.scalar_registry PROPERTIES
 LABELS "runtimec;scaffold;vmbin;scalar")

# ptreq / ptreq·not 的四种操作数组合。
#
# 不能塞进 handler_behavior：那个数据驱动用例走**字面量**操作数，wire 的 `ptr`
# 字节恒为 INLINE，永远走不到指针分支。本目标用形参绑定构造真 XValue，
# 才能覆盖 `(Ptr,Ptr)` / `(Ptr,None)` / `(None,None)` 三态。
#
# 重点在 `(None,None)`：`None != None` 必须为 false。漏了这一支的取反不会崩，
# 只会让所有判空逻辑反向。
add_executable(runtimec_scalar_pointer_equality_test
 scalar_pointer_equality_test.c)
target_compile_features(runtimec_scalar_pointer_equality_test PRIVATE c_std_11)
target_include_directories(runtimec_scalar_pointer_equality_test PRIVATE
 "${CMAKE_CURRENT_SOURCE_DIR}/.."
 "${RUNTIMEC_INCLUDE_DIRECTORY}"
 "${KVSPACE_INCLUDE_DIRECTORY}")
# 链接**文件**而不是 `-lkvspace`：仓库里没有名为 libkvspace.so 的链接目录在
# 默认搜索路径上，裸写 `kvspace` 会在链接期报 `cannot find -lkvspace`。
# 这与 `runtimec_wire_golden_real` 是同一套写法，本文件其它用例都这么链。
target_link_libraries(runtimec_scalar_pointer_equality_test PRIVATE
 runtimec_contract runtimec_binrwir runtimec_vmbin
 "${RUNTIMEC_KVSPACE_LIBRARY_PATH}")
set_target_properties(runtimec_scalar_pointer_equality_test PROPERTIES
 BUILD_RPATH "${RUNTIMEC_KVSPACE_LIBRARY_DIRECTORY}")
runtimec_test_warnings(runtimec_scalar_pointer_equality_test)
add_test(NAME runtimec.vmbin.scalar_pointer_equality
 COMMAND runtimec_scalar_pointer_equality_test)
set_tests_properties(runtimec.vmbin.scalar_pointer_equality PROPERTIES
 LABELS "runtimec;scaffold;vmbin;scalar;pointer")

add_executable(runtimec_vmbin_langtype_handler_test vmbin_langtype_handler_test.c)
target_compile_features(runtimec_vmbin_langtype_handler_test PRIVATE c_std_11)
target_include_directories(runtimec_vmbin_langtype_handler_test PRIVATE
 "${CMAKE_CURRENT_SOURCE_DIR}/.."
 "${RUNTIMEC_INCLUDE_DIRECTORY}"
 "${KVSPACE_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_vmbin_langtype_handler_test PRIVATE
 runtimec_contract runtimec_binrwir runtimec_vmbin)
runtimec_test_warnings(runtimec_vmbin_langtype_handler_test)
add_test(NAME runtimec.vmbin.langtype_handler
 COMMAND runtimec_vmbin_langtype_handler_test)
set_tests_properties(runtimec.vmbin.langtype_handler PROPERTIES
 LABELS "runtimec;scaffold;vmbin;langtype")

# binrwir-dump：逐文件独立临时 kvspace → layout → 编码 → 打印成 .binkv。
# 不进 ctest：它是产出文本产物的工具，不是断言式用例；失败判据由它自己打印
# 「共 N：成功 X / 失败 Y」并写 FAILURES.md。
add_executable(runtimec_binrwir_dump ../tools/binrwir_dump.c)
target_compile_features(runtimec_binrwir_dump PRIVATE c_std_11)
target_include_directories(runtimec_binrwir_dump PRIVATE
 "${CMAKE_CURRENT_SOURCE_DIR}/.."
 "${RUNTIMEC_INCLUDE_DIRECTORY}"
 "${KVSPACE_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_binrwir_dump PRIVATE
 runtimec_contract runtimec_binrwir runtimec_vmkv
 "${CMAKE_DL_LIBS}")
runtimec_test_warnings(runtimec_binrwir_dump)

# 能力目录的「声称」与 runtime 的「实际」必须一致。
#
# 判据：目录里 runtimec 声称自己执行的每一个 opid，session 建好之后
# `binrwir_registry_find` 必须成功。
#
# 为什么需要：`generate_rwir_catalog.py` 的 REGISTRY_SOURCE 指向
# `runtime/src/mywircaps.c`（不可构建的旧 runtime），而真正执行的是 session 装的
# 表。两套「注册」互不校验，于是「目录说 0 gaps」与「registry_find(0x0014)
# 返回 UNSUPPORTED」可以同时成立——`pow` 就躺在这个缝里。
#
# 走真 session（不自建 registry）：自建就绕过了 session.c 的装载路径，
# 而那正是缺口所在。缺 kvspace 库时返回 77。
add_executable(runtimec_catalog_coverage_test catalog_coverage_test.c)
target_compile_features(runtimec_catalog_coverage_test PRIVATE c_std_11)
target_compile_definitions(runtimec_catalog_coverage_test PRIVATE _GNU_SOURCE)
target_include_directories(runtimec_catalog_coverage_test PRIVATE
 "${CMAKE_CURRENT_SOURCE_DIR}/.."
 "${RUNTIMEC_INCLUDE_DIRECTORY}"
 "${KVSPACE_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_catalog_coverage_test PRIVATE
 runtimec_contract runtimec_engine runtimec_vmbin
 runtimec_binrwir runtimec_vmkv "${CMAKE_DL_LIBS}")
runtimec_test_warnings(runtimec_catalog_coverage_test)
add_test(NAME runtimec.catalog.coverage
 COMMAND runtimec_catalog_coverage_test
 "${RUNTIMEC_KVSPACE_LIBRARY_PATH}")
set_tests_properties(runtimec.catalog.coverage PROPERTIES
 SKIP_RETURN_CODE 77
 LABELS "runtimec;scaffold;catalog")

# 写不存在的绝对 key（甲案）：正向创建 + 负向读目标拒绝。
# 旧规则「argument.c 必须拒绝零地址」在语义放开后移走，判据落到这里。
add_executable(runtimec_absolute_write_test absolute_write_test.c)
target_compile_features(runtimec_absolute_write_test PRIVATE c_std_11)
target_include_directories(runtimec_absolute_write_test PRIVATE
 "${CMAKE_CURRENT_SOURCE_DIR}/.."
 "${RUNTIMEC_INCLUDE_DIRECTORY}"
 "${KVSPACE_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_absolute_write_test PRIVATE
 runtimec_contract runtimec_vmkv runtimec_engine
 "${CMAKE_DL_LIBS}")
runtimec_test_warnings(runtimec_absolute_write_test)
add_test(NAME runtimec.engine.absolute_write
 COMMAND runtimec_absolute_write_test
 "${KVLANG_LAYOUT_LIBRARY}"
 "${RUNTIMEC_KVSPACE_LIBRARY_PATH}")
set_tests_properties(runtimec.engine.absolute_write PROPERTIES
 SKIP_RETURN_CODE 77
 LABELS "runtimec;scaffold;engine;absolute-write")
