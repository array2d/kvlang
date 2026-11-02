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
