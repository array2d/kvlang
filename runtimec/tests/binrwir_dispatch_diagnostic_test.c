/*
 * 分派层**不得覆盖** handler 自己写的诊断码。
 *
 * 为什么单列一条：handler 是最了解自己失败原因的一层——值宽不符是 `0x4304`
 * 而不是笼统的「某个 handler 失败了」。分派层若在 `status != SUCCESS` 时无条件
 * 调 `vmbin_interpreter_error_exit`，那条精确诊断就被换成
 * `VMBIN_INTERPRETER_DIAG_HANDLER_FAILED`，下游只能看到一个更没用的数字。
 *
 * 两种情形必须分开：
 *   - handler 写了 `kind = ERROR` 且给了 detail_code → **只补位置**，不碰码；
 *   - handler 只回状态、没写出口 → 兜底成 `DIAG_HANDLER_FAILED`。
 *
 * 第三种是**实现 bug**：handler 置了 `kind = ERROR` 却返回 SUCCESS。两个通道
 * 矛盾时调用方没有唯一规则，执行窗口按返回状态判成败，会把失败当成成功原样
 * 拷贝（下游看到 `status=0 detail=0`）。`vmbin_execute` 必须拒绝以 SUCCESS
 * 返回 ERROR 出口，换成 `DIAG_CONTRADICTORY_EXIT`。
 *
 * 同时验证「清零在前」：handler 什么都不写时，输出不能残留上一次分派的 exit。
 *
 * 反向控制：把分派层改回「无条件 error_exit」，第一种情形必须红。
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "binrwir/execution_exit.h"
#include "binrwir/execution_frame.h"
#include "binrwir/instruction.h"
#include "binrwir/program.h"
#include "binrwir/registry.h"
#include "contract/allocator.h"
#include "contract/status.h"
#include "kvspace/xvalue_head.h"
#include "vmbin/interpreter.h"

static int failures;

#define EXPECT(condition)                                                      \
    do {                                                                       \
        if (!(condition)) {                                                    \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #condition);        \
            failures += 1;                                                     \
        }                                                                      \
    } while (0)

/* handler 自己写的精确诊断码，模拟 container 的 VALUE_WIDTH_MISMATCH。 */
#define TEST_PRECISE_DETAIL UINT32_C(0x4304)

/* 三种 handler 形态，各自的出口断言不同。 */
typedef enum test_expectation {
    TEST_EXPECTATION_PRECISE_DETAIL = 0, /* 写出口 + 返回非 SUCCESS：码原样保留 */
    TEST_EXPECTATION_DEFAULT_DETAIL,    /* 不写出口：兜底成 HANDLER_FAILED */
    TEST_EXPECTATION_CONTRADICTION       /* 写 ERROR 出口但返回 SUCCESS：矛盾 */
} test_expectation_t;

static void *test_allocate(void *context, size_t size, size_t alignment) {
    (void)context;
    (void)alignment;
    return malloc(size);
}
static void test_deallocate(void *context, void *allocation, size_t size,
                            size_t alignment) {
    (void)context;
    (void)size;
    (void)alignment;
    free(allocation);
}

/* ① 写 ERROR 出口并带精确 detail_code，返回非 SUCCESS。 */
static runtimec_status_t test_precise_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    (void)frame;
    (void)instruction;
    output_exit->kind = BINRWIR_EXECUTION_EXIT_ERROR;
    output_exit->status = RUNTIMEC_STATUS_INVALID;
    /* 故意**不写** logical_program_counter：验证分派层只补位置。 */
    output_exit->detail_code = TEST_PRECISE_DETAIL;
    return RUNTIMEC_STATUS_INVALID;
}

/* ② 只回状态、不写出口：分派层必须兜底成 DIAG_HANDLER_FAILED。 */
static runtimec_status_t test_bare_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    (void)frame;
    (void)instruction;
    (void)output_exit;
    return RUNTIMEC_STATUS_ERROR;
}

/*
 * ③ 置了 ERROR 出口，却返回 SUCCESS：两个通道自相矛盾，是实现 bug。
 *
 * 曾经这种形态一路活到调用方：执行窗口按返回状态判成败，于是把一次失败当成
 * 成功原样拷贝，下游看到 `status=0 detail=0`——「失败却报成功码」。
 */
static runtimec_status_t test_contradictory_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    (void)frame;
    (void)instruction;
    output_exit->kind = BINRWIR_EXECUTION_EXIT_ERROR;
    output_exit->status = RUNTIMEC_STATUS_INVALID;
    output_exit->detail_code = TEST_PRECISE_DETAIL;
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t test_build_program(const runtimec_allocator_t *allocator,
                                            binrwir_operation_identifier_t opid,
                                            binrwir_program_t **output_program) {
    binrwir_program_identity_t identity;
    binrwir_program_builder_t *builder = NULL;
    binrwir_argument_t argument;
    binrwir_instruction_build_request_t request;
    uint32_t identifier = BINRWIR_IDENTIFIER_NONE;
    runtimec_status_t status;

    memset(&identity, 0, sizeof(identity));
    identity.snapshot_generation = 1U;
    identity.program_generation = 1U;
    identity.binrwir_binary_interface_generation =
        BINRWIR_BINARY_INTERFACE_GENERATION;
    status = binrwir_program_builder_create(allocator, &identity, &builder);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    /*
     * 一条最小指令，只为让 Program 过校验。
     * 读参必须是**已登记**的字面量：LITERAL 的 payload 是池下标，池里没有对应
     * 条目时 validate 判 INVALID（这正是先前 status=5 的来源）。
     */
    {
        static const uint64_t body_value = 7U;
        uint8_t wire[KVSPACE_XVALUE_HEAD_LEN + sizeof(body_value)];
        kvspaceXvalueHead head;
        uint32_t literal_index = BINRWIR_LITERAL_INDEX_NONE;

        memset(&head, 0, sizeof(head));
        kvspaceXvalueHeadSetLen(&head, sizeof(body_value));
        head.layout = KVSPACE_XVALUE_LAYOUT_LENGTH;
        memcpy(wire, &head, KVSPACE_XVALUE_HEAD_LEN);
        memcpy(wire + KVSPACE_XVALUE_HEAD_LEN, &body_value, sizeof(body_value));
        status = binrwir_program_builder_add_literal(
            builder, runtimec_bytes_view_make(wire, sizeof(wire)),
            runtimec_string_view_from_c_string("int64"), &literal_index);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            (void)binrwir_program_builder_destroy(builder);
            return status;
        }
        status = binrwir_argument_create(0U, BINRWIR_REFERENCE_LITERAL, 0U,
                                         literal_index, &argument);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        (void)binrwir_program_builder_destroy(builder);
        return status;
    }
    memset(&request, 0, sizeof(request));
    request.source_logical_program_counter = 7U;
    request.operation_identifier = opid;
    request.read_arguments.data = &argument;
    request.read_arguments.count = 1U;
    status = binrwir_program_builder_add_instruction(builder, &request,
                                                     &identifier);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_program_builder_finalize(builder, output_program);
    }
    (void)binrwir_program_builder_destroy(builder);
    return status;
}

static void test_case(const runtimec_allocator_t *allocator,
                      binrwir_operation_identifier_t opid,
                      binrwir_registry_handler_t handler,
                      test_expectation_t expectation) {
    binrwir_program_t *program = NULL;
    binrwir_registry_t registry;
    binrwir_frame_bindings_t *bindings = NULL;
    binrwir_execution_frame_t frame;
    binrwir_execution_exit_t exit_state;
    binrwir_registry_entry_t entry;
    binrwir_frame_bindings_t *created = NULL;
    runtimec_status_t status;

    if (test_build_program(allocator, opid, &program) !=
        RUNTIMEC_STATUS_SUCCESS) {
        printf("FAIL cannot build program for opid 0x%04X\n",
               (unsigned)opid);
        failures += 1;
        return;
    }
    EXPECT(binrwir_registry_initialize(&registry) == RUNTIMEC_STATUS_SUCCESS);
    entry.operation_identifier = opid;
    entry.flags = BINRWIR_REGISTRY_FLAG_NONE;
    entry.handler = handler;
    EXPECT(binrwir_registry_append(&registry, "test", &entry, 1U, NULL) ==
           RUNTIMEC_STATUS_SUCCESS);
    EXPECT(binrwir_frame_bindings_create(allocator, 4U, &created) ==
           RUNTIMEC_STATUS_SUCCESS);
    bindings = created;
    EXPECT(binrwir_execution_frame_initialize(&frame, program, NULL, 1U, 1U,
                                              0U) == RUNTIMEC_STATUS_SUCCESS);
    frame.bindings = bindings;

    memset(&exit_state, 0, sizeof(exit_state));
    /* 故意塞一条**陈旧**的 exit：验证分派层的清零在前。 */
    exit_state.kind = BINRWIR_EXECUTION_EXIT_ERROR;
    exit_state.detail_code = UINT32_C(0xDEAD);
    status = vmbin_execute(program, NULL, &frame, &registry, NULL, NULL,
                           &exit_state);
    EXPECT(status != RUNTIMEC_STATUS_SUCCESS);
    EXPECT(exit_state.kind == BINRWIR_EXECUTION_EXIT_ERROR);
    switch (expectation) {
    case TEST_EXPECTATION_PRECISE_DETAIL:
        /* handler 的精确码必须原样保留，且位置被补上。 */
        EXPECT(exit_state.detail_code == TEST_PRECISE_DETAIL);
        EXPECT(exit_state.logical_program_counter == 7U);
        EXPECT(exit_state.resume_logical_program_counter == 7U);
        break;
    case TEST_EXPECTATION_DEFAULT_DETAIL:
        /* 没写出口：兜底成通用诊断，且不能残留 0xDEAD。 */
        EXPECT(exit_state.detail_code ==
               VMBIN_INTERPRETER_DIAG_HANDLER_FAILED);
        EXPECT(exit_state.logical_program_counter == 7U);
        break;
    case TEST_EXPECTATION_CONTRADICTION:
        /*
         * 两个通道矛盾：`vmbin_execute` 不许以 SUCCESS 返回 ERROR 出口。
         * 换成只属于本层的诊断，位置保留；handler 自己的码不再冒充本层分类。
         */
        EXPECT(exit_state.detail_code ==
               VMBIN_INTERPRETER_DIAG_CONTRADICTORY_EXIT);
        EXPECT(exit_state.logical_program_counter == 7U);
        EXPECT(exit_state.detail_code != TEST_PRECISE_DETAIL);
        break;
    }

    EXPECT(binrwir_frame_bindings_destroy(bindings) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(binrwir_execution_frame_destroy(&frame) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(binrwir_program_destroy(program) == RUNTIMEC_STATUS_SUCCESS);
}

int main(void) {
    runtimec_allocator_t allocator = {NULL, test_allocate, test_deallocate};

    /* 三个 opid 都不能与真实能力行冲突：用保留区以外、catalog 未用的值。 */
    test_case(&allocator, UINT16_C(0x7F01), test_precise_handler,
              TEST_EXPECTATION_PRECISE_DETAIL);
    test_case(&allocator, UINT16_C(0x7F02), test_bare_handler,
              TEST_EXPECTATION_DEFAULT_DETAIL);
    test_case(&allocator, UINT16_C(0x7F03), test_contradictory_handler,
              TEST_EXPECTATION_CONTRADICTION);

    if (failures != 0) {
        printf("binrwir_dispatch_diagnostic_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("binrwir_dispatch_diagnostic_test: OK\n");
    return 0;
}
