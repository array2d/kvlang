# Component: tests
# Role: Single-place registration for runtimec unit tests. **Every test file must be
#       committed together with its registration**: leaving a `.c` file without `add_test`
#       causes `ctest` to print `No tests were found!!!` and **exit 0**, producing a false
#       green in CI (see TASK-086).
# Links: Included via `add_subdirectory(tests)` from the parent CMakeLists.txt; depends on
#        component targets with no external test framework (plain C11 + main exit code).
#
# Convention: Each test is a standalone executable where a nonzero `main` return fails.
# Avoid gtest/catch2 so test registration does not gain an extra layer.

# Key comparator and sorted cache: pure in-memory, no backend needed.
add_executable(key_order_test key_order_test.c)
target_link_libraries(key_order_test PRIVATE runtimec_contract)

# Pointer prefix resolution: uses a fake reader, no backend needed.
add_executable(key_resolution_test key_resolution_test.c)
target_link_libraries(key_resolution_test PRIVATE runtimec_contract)

# Extension operator registration: exercises public create, requires the full engine chain.
add_executable(extension_registration_test extension_registration_test.c)
target_link_libraries(extension_registration_test PRIVATE
    runtimec_interface runtimec_contract runtimec_binrwir runtimec_vmbin
    runtimec_vmkv runtimec_binrwir_compiler runtimec_engine)

# Pre-failure path for directory enumeration: target under test lives in vmbin.
add_executable(directory_walk_open_failure_test directory_walk_open_failure_test.c)
target_link_libraries(directory_walk_open_failure_test PRIVATE
    runtimec_vmbin runtimec_contract runtimec_binrwir)

# Error status and message precedence (TASK-071): failure paths at public entry points.
add_executable(error_status_priority_test error_status_priority_test.c)
target_link_libraries(error_status_priority_test PRIVATE
    runtimec_interface runtimec_contract runtimec_binrwir runtimec_vmbin
    runtimec_vmkv runtimec_binrwir_compiler runtimec_engine)

# Text semantics for extension argument reads (TASK-070): exercises interface readers, requires full engine chain.
add_executable(argument_text_wire_test argument_text_wire_test.c)
# Wire construction uses language-layer constructors (`pathlangXvalueHeadNew*`) under pathlang/include.
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
