cmake_minimum_required(VERSION 3.10)
project(kvlang-runtime C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Outputs go under bin/.
set(BIN_DIR "${CMAKE_SOURCE_DIR}/../bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${BIN_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${BIN_DIR}")

file(GLOB KVLANG_RUNTIME_SRC CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.c")
add_library(kvlang_runtime SHARED ${KVLANG_RUNTIME_SRC})
target_include_directories(kvlang_runtime PUBLIC include src)
target_compile_definitions(kvlang_runtime PRIVATE _GNU_SOURCE)

# 链接 kvspace dispatch 前端（与后端同装 <prefix>/lib/kvspace），运行期按 DSN 选后端，编译期不再选后端。
# KVSPACE_LIB_DIR 可覆盖安装目录：macOS 的 /usr 受 SIP 保护，应指向 <prefix>/lib/kvspace。
set(KVSPACE_LIB_DIR "/usr/lib/kvspace" CACHE PATH "kvspace dispatch 前端安装目录")
target_link_directories(kvlang_runtime PRIVATE ${KVSPACE_LIB_DIR})

if(APPLE)
    # macOS：数学/线程符号在 libSystem 内（无 -lm/-lpthread）；ld64 无 --disable-new-dtags。
    target_link_libraries(kvlang_runtime PRIVATE kvspace)
    set_target_properties(kvlang_runtime PROPERTIES
        LINK_FLAGS "-Wl,-export_dynamic -Wl,-rpath,${KVSPACE_LIB_DIR}")
else()
    target_link_libraries(kvlang_runtime PRIVATE kvspace m pthread)
    # --export-dynamic 供 kvlang（term 扩展）找符号；--disable-new-dtags 使 rpath 转 DT_RPATH（传递）。
    set_target_properties(kvlang_runtime PROPERTIES
        LINK_FLAGS "-Wl,--export-dynamic -Wl,--disable-new-dtags -Wl,-rpath,${KVSPACE_LIB_DIR}")
endif()

if(NOT APPLE)
    enable_testing()
    foreach(test_name test_getmember_siblings test_kvset_invalid_head test_pc_kvspace_source test_crash_resume test_param_address)
        add_custom_command(
            OUTPUT "${BIN_DIR}/${test_name}"
            COMMAND ${CMAKE_C_COMPILER} -O2 -D_GNU_SOURCE
                -I${CMAKE_SOURCE_DIR}/include -I${CMAKE_SOURCE_DIR}/src
                ${CMAKE_SOURCE_DIR}/tests/${test_name}.c
                -o ${BIN_DIR}/${test_name}
                -L${BIN_DIR} -L${KVSPACE_LIB_DIR}
                -Wl,--disable-new-dtags
                -Wl,-rpath,${BIN_DIR} -Wl,-rpath,${KVSPACE_LIB_DIR}
                -Wl,--no-as-needed -lkvspace -Wl,--as-needed -lkvlang_runtime
            DEPENDS kvlang_runtime "${CMAKE_SOURCE_DIR}/tests/${test_name}.c"
            VERBATIM)
        add_custom_target(${test_name} ALL DEPENDS "${BIN_DIR}/${test_name}")
    endforeach()
    add_test(NAME getmember_siblings COMMAND "${BIN_DIR}/test_getmember_siblings")
    add_test(NAME kvset_invalid_head_shm COMMAND "${BIN_DIR}/test_kvset_invalid_head")
    add_test(NAME kvset_invalid_head_fs COMMAND "${BIN_DIR}/test_kvset_invalid_head")
    add_test(NAME pc_kvspace_source_shm COMMAND "${BIN_DIR}/test_pc_kvspace_source")
    add_test(NAME pc_kvspace_source_fs COMMAND "${BIN_DIR}/test_pc_kvspace_source")
    add_test(NAME crash_resume_shm COMMAND "${BIN_DIR}/test_crash_resume")
    add_test(NAME crash_resume_fs COMMAND "${BIN_DIR}/test_crash_resume")
    add_test(NAME param_address_shm COMMAND "${BIN_DIR}/test_param_address")
    add_test(NAME param_address_fs COMMAND "${BIN_DIR}/test_param_address")
    set_tests_properties(getmember_siblings kvset_invalid_head_shm pc_kvspace_source_shm crash_resume_shm param_address_shm
        PROPERTIES ENVIRONMENT "KVSPACE_BACKEND_PATH=${KVSPACE_LIB_DIR};KVLANG_TEST_SCHEME=shm")
    set_tests_properties(kvset_invalid_head_fs pc_kvspace_source_fs crash_resume_fs param_address_fs
        PROPERTIES ENVIRONMENT "KVSPACE_BACKEND_PATH=${KVSPACE_LIB_DIR};KVLANG_TEST_SCHEME=fs")
endif()
