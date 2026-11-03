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
set_tests_properties(runtimec.host.kvspace_backend PROPERTIES
    SKIP_RETURN_CODE 77
    LABELS "runtimec;scaffold;host;backend")

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
    "${KVLANG_INCLUDE_DIRECTORY}")
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
    "${KVLANG_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_vmkv_encoder_test PRIVATE
    runtimec_contract runtimec_binrwir runtimec_vmkv)
runtimec_test_warnings(runtimec_vmkv_encoder_test)
add_test(NAME runtimec.vmkv.encoder COMMAND runtimec_vmkv_encoder_test)
set_tests_properties(runtimec.vmkv.encoder PROPERTIES
    LABELS "runtimec;scaffold;vmkv;encoder")

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
    "${KVLANG_INCLUDE_DIRECTORY}")
target_link_libraries(runtimec_vmbin_execute_test PRIVATE
    runtimec_contract runtimec_binrwir runtimec_vmkv runtimec_vmbin)
runtimec_test_warnings(runtimec_vmbin_execute_test)
add_test(NAME runtimec.vmbin.execute COMMAND runtimec_vmbin_execute_test)
set_tests_properties(runtimec.vmbin.execute PROPERTIES
    LABELS "runtimec;scaffold;vmbin;execute")

add_executable(runtimec_vmbin_call_test vmbin_call_test.c)
target_compile_features(runtimec_vmbin_call_test PRIVATE c_std_11)
target_include_directories(runtimec_vmbin_call_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${RUNTIMEC_INCLUDE_DIRECTORY}"
    "${KVLANG_INCLUDE_DIRECTORY}")
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
set_tests_properties(
    runtimec.architecture.layers
    runtimec.architecture.names
    runtimec.architecture.behavior_matrix
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
