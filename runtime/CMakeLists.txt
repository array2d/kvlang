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
# 生成族、decode 与赋值转换固定在热 TU 之后链接：改动热 TU 的 .text 布局会造成
# 明显实测回退（生成族约 1.5%、转换代码约 8%、decode 收缩曾致 k_nucleotide +8%）。
# decode 只在行加载期执行（每帧每行一次），排在 handler 之后不伤热路径。
set(KVLANG_GEN_SRC "${CMAKE_SOURCE_DIR}/src/rwir_special_gen.c")
set(KVLANG_COERCE_SRC "${CMAKE_SOURCE_DIR}/src/rwir_coerce.c")
set(KVLANG_DECODE_SRC "${CMAKE_SOURCE_DIR}/src/rwir_decode.c")
set(KVLANG_ISA_SRC "${CMAKE_SOURCE_DIR}/src/isa.c")
set(KVLANG_ISA_SIG_SRC "${CMAKE_SOURCE_DIR}/src/isa_sig.c")
set(KVLANG_SLOTVIEW_SRC "${CMAKE_SOURCE_DIR}/src/slotview.c")
list(REMOVE_ITEM KVLANG_RUNTIME_SRC "${KVLANG_GEN_SRC}" "${KVLANG_COERCE_SRC}"
    "${KVLANG_DECODE_SRC}" "${KVLANG_ISA_SRC}" "${KVLANG_ISA_SIG_SRC}"
    "${KVLANG_SLOTVIEW_SRC}")
list(APPEND KVLANG_RUNTIME_SRC "${KVLANG_DECODE_SRC}" "${KVLANG_GEN_SRC}"
    "${KVLANG_COERCE_SRC}" "${KVLANG_ISA_SRC}" "${KVLANG_ISA_SIG_SRC}"
    "${KVLANG_SLOTVIEW_SRC}")
add_library(kvlang_runtime SHARED ${KVLANG_RUNTIME_SRC})
target_include_directories(kvlang_runtime PUBLIC include src)
target_compile_definitions(kvlang_runtime PRIVATE _GNU_SOURCE)

option(KVLANG_DEBUG_TYPES "P1b3: assert typed-core slot kinds (test build)" OFF)
if(KVLANG_DEBUG_TYPES)
    target_compile_definitions(kvlang_runtime PRIVATE KV_DEBUG_TYPES)
endif()

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
    foreach(test_name test_getmember_siblings test_kvset_invalid_head test_pc_kvspace_source test_crash_resume test_param_address delegate_test test_scalar_encode test_pc_part_write test_ro_rewrite test_keytree_irseq_pc test_pc_view_probe)
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
    add_test(NAME delegate_shm COMMAND "${BIN_DIR}/delegate_test")
    add_test(NAME scalar_encode COMMAND "${BIN_DIR}/test_scalar_encode")
    add_test(NAME keytree_irseq_pc COMMAND "${BIN_DIR}/test_keytree_irseq_pc")
    add_test(NAME pc_part_write_shm COMMAND "${BIN_DIR}/test_pc_part_write")
    add_test(NAME pc_part_write_fs COMMAND "${BIN_DIR}/test_pc_part_write")
    add_test(NAME ro_rewrite_shm COMMAND "${BIN_DIR}/test_ro_rewrite")
    add_test(NAME ro_rewrite_fs COMMAND "${BIN_DIR}/test_ro_rewrite")
    add_test(NAME body_write COMMAND bash "${CMAKE_SOURCE_DIR}/tests/body_write.sh")
    add_test(NAME int_specialize COMMAND bash "${CMAKE_SOURCE_DIR}/tests/int_specialize.sh")
    add_test(NAME float_specialize COMMAND bash "${CMAKE_SOURCE_DIR}/tests/float_specialize.sh")
    add_test(NAME pause_resume COMMAND bash "${CMAKE_SOURCE_DIR}/tests/pause_resume.sh")
    add_test(NAME status_view COMMAND bash "${CMAKE_SOURCE_DIR}/tests/status_view.sh")
    add_test(NAME status_rebind COMMAND bash "${CMAKE_SOURCE_DIR}/tests/status_rebind.sh")
    set_tests_properties(status_rebind PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME pc_owned_view COMMAND bash "${CMAKE_SOURCE_DIR}/tests/pc_owned_view.sh")
    set_tests_properties(pc_owned_view PROPERTIES SKIP_RETURN_CODE 77
        ENVIRONMENT "KVSPACE_BACKEND_PATH=${KVSPACE_LIB_DIR};KVLANG_LAYOUT_BIN=${BIN_DIR}/kvlanglayout;KVLANG_TERM_BIN=${BIN_DIR}/kvlang;KVLANG_PC_PROBE=${BIN_DIR}/test_pc_view_probe")
    add_test(NAME layout_reject COMMAND bash "${CMAKE_SOURCE_DIR}/tests/layout_reject.sh")
    add_test(NAME coercion COMMAND bash "${CMAKE_SOURCE_DIR}/tests/coercion.sh")
    add_test(NAME dyn_specialize COMMAND bash "${CMAKE_SOURCE_DIR}/tests/dyn_specialize.sh")
    add_test(NAME isa_gate COMMAND bash "${CMAKE_SOURCE_DIR}/tests/isa_gate.sh")
    add_test(NAME isa_typed_violation
             COMMAND bash "${CMAKE_SOURCE_DIR}/tests/isa_typed_violation.sh")
    set_tests_properties(isa_typed_violation PROPERTIES SKIP_RETURN_CODE 77
        ENVIRONMENT "KVSPACE_BACKEND_PATH=${KVSPACE_LIB_DIR};KVLANG_LAYOUT_BIN=${BIN_DIR}/kvlanglayout;KVLANG_TERM_BIN=${BIN_DIR}/kvlang")
    find_program(PYTHON3_EXE python3)
    if(PYTHON3_EXE)
        add_test(NAME isa_conformance
                 COMMAND "${PYTHON3_EXE}" "${CMAKE_SOURCE_DIR}/tests/isa_conformance.py")
        set_tests_properties(isa_conformance PROPERTIES
            ENVIRONMENT "KVSPACE_BACKEND_PATH=${KVSPACE_LIB_DIR};KVLANG_LAYOUT_BIN=${BIN_DIR}/kvlanglayout;KVLANG_TERM_BIN=${BIN_DIR}/kvlang")
    endif()
    add_test(NAME specialize_gate COMMAND bash "${CMAKE_SOURCE_DIR}/tests/specialize_gate.sh")
    set_tests_properties(getmember_siblings kvset_invalid_head_shm pc_kvspace_source_shm crash_resume_shm param_address_shm delegate_shm scalar_encode pc_part_write_shm ro_rewrite_shm
        PROPERTIES ENVIRONMENT "KVSPACE_BACKEND_PATH=${KVSPACE_LIB_DIR};KVLANG_TEST_SCHEME=shm")
    set_tests_properties(kvset_invalid_head_fs pc_kvspace_source_fs crash_resume_fs param_address_fs pc_part_write_fs ro_rewrite_fs
        PROPERTIES ENVIRONMENT "KVSPACE_BACKEND_PATH=${KVSPACE_LIB_DIR};KVLANG_TEST_SCHEME=fs")
    set_tests_properties(body_write
        PROPERTIES ENVIRONMENT "KVSPACE_BACKEND_PATH=${KVSPACE_LIB_DIR};KVLANG_LAYOUT_BIN=${BIN_DIR}/kvlanglayout;KVLANG_TERM_BIN=${BIN_DIR}/kvlang")
    set_tests_properties(int_specialize
        PROPERTIES ENVIRONMENT "KVSPACE_BACKEND_PATH=${KVSPACE_LIB_DIR};KVLANG_LAYOUT_BIN=${BIN_DIR}/kvlanglayout;KVLANG_TERM_BIN=${BIN_DIR}/kvlang")
    set_tests_properties(float_specialize
        PROPERTIES ENVIRONMENT "KVSPACE_BACKEND_PATH=${KVSPACE_LIB_DIR};KVLANG_LAYOUT_BIN=${BIN_DIR}/kvlanglayout;KVLANG_TERM_BIN=${BIN_DIR}/kvlang")
    set_tests_properties(pause_resume
        PROPERTIES ENVIRONMENT "KVSPACE_BACKEND_PATH=${KVSPACE_LIB_DIR};KVLANG_LAYOUT_BIN=${BIN_DIR}/kvlanglayout;KVLANG_TERM_BIN=${BIN_DIR}/kvlang")
    set_tests_properties(status_view
        PROPERTIES ENVIRONMENT "KVSPACE_BACKEND_PATH=${KVSPACE_LIB_DIR};KVLANG_LAYOUT_BIN=${BIN_DIR}/kvlanglayout;KVLANG_TERM_BIN=${BIN_DIR}/kvlang")
    set_tests_properties(isa_gate
        PROPERTIES ENVIRONMENT "KVSPACE_BACKEND_PATH=${KVSPACE_LIB_DIR};KVLANG_LAYOUT_BIN=${BIN_DIR}/kvlanglayout;KVLANG_TERM_BIN=${BIN_DIR}/kvlang")
    set_tests_properties(layout_reject
        PROPERTIES ENVIRONMENT "KVSPACE_BACKEND_PATH=${KVSPACE_LIB_DIR};KVLANG_LAYOUT_BIN=${BIN_DIR}/kvlanglayout")
    set_tests_properties(coercion
        PROPERTIES ENVIRONMENT "KVSPACE_BACKEND_PATH=${KVSPACE_LIB_DIR};KVLANG_LAYOUT_BIN=${BIN_DIR}/kvlanglayout;KVLANG_TERM_BIN=${BIN_DIR}/kvlang")
    set_tests_properties(dyn_specialize
        PROPERTIES ENVIRONMENT "KVSPACE_BACKEND_PATH=${KVSPACE_LIB_DIR};KVLANG_LAYOUT_BIN=${BIN_DIR}/kvlanglayout;KVLANG_TERM_BIN=${BIN_DIR}/kvlang")
    set_tests_properties(specialize_gate
        PROPERTIES ENVIRONMENT "KVSPACE_BACKEND_PATH=${KVSPACE_LIB_DIR};KVLANG_LAYOUT_BIN=${BIN_DIR}/kvlanglayout")
endif()
