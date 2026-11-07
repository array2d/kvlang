/*
 * 诊断命名空间不得混用：`error.code` 是下层的码，`exit.detail_code` 是本层
 * （解释器）的码，两者**不同**。
 *
 * 曾经解释器把 `error.code` 抄进 `exit.detail_code`，理由是「给更具体的细节」。
 * 代价是这个字段变成混合命名空间：解释器、执行窗口、值访问三个枚举同值 6，
 * 同一个数字在三种解释下都含义明确，于是没有任何地方会报「你不确定」。实际
 * 后果是全队两次误判。判据：同一份数据不许有两种解释。
 *
 * 一条缺陷同时经两条公开面观察：
 *   ① `vmbin_value_access_locate_body` 报出下层的 `UNBOUND_SLOT`（3）；
 *   ② `vmbin_execute` 把它折成自己的 `VALUE_ACCESS`（7），detail 不是 3。
 *
 * 反向控制：把那段抄写改回来，② 的断言必须红。
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "binrwir/argument.h"
#include "binrwir/execution_exit.h"
#include "binrwir/execution_frame.h"
#include "binrwir/frame_bindings.h"
#include "binrwir/program.h"
#include "binrwir/registry.h"
#include "contract/allocator.h"
#include "contract/error.h"
#include "contract/status.h"
#include "vmbin/interpreter.h"
#include "vmbin/value_access.h"

static int failures;

#define EXPECT(condition)                                                      \
    do {                                                                       \
        if (!(condition)) {                                                    \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #condition);        \
            failures += 1;                                                     \
        }                                                                      \
    } while (0)

#define TEST_OPERATION_IDENTIFIER_COPY UINT32_C(0x0001)
#define TEST_SOURCE_PROGRAM_COUNTER 7U

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

static runtimec_string_view_t test_view(const char *text) {
    return runtimec_string_view_from_c_string(text);
}

/* 一条 COPY：读局部符号 0、写局部符号 1；两者都不在绑定表里。 */
static runtimec_status_t test_build_program(const runtimec_allocator_t *allocator,
                                            binrwir_program_t **output_program,
                                            binrwir_argument_t *output_read_argument) {
    binrwir_program_identity_t identity;
    binrwir_program_builder_t *builder = NULL;
    binrwir_argument_t write_argument;
    binrwir_instruction_build_request_t request;
    uint32_t identifier = BINRWIR_IDENTIFIER_NONE;
    uint16_t symbol_index = 0U;
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
    status = binrwir_program_builder_add_symbol(builder, test_view("alpha"),
                                                test_view("int64"), &symbol_index);
    if (status == RUNTIMEC_STATUS_SUCCESS && symbol_index != 0U) {
        status = RUNTIMEC_STATUS_INVALID;
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_program_builder_add_symbol(
            builder, test_view("beta"), test_view("int64"), &symbol_index);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS && symbol_index != 1U) {
        status = RUNTIMEC_STATUS_INVALID;
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_argument_create(0U, BINRWIR_REFERENCE_LOCAL, 0U, 0U,
                                         output_read_argument);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_argument_create(0U, BINRWIR_REFERENCE_LOCAL, 1U, 0U,
                                         &write_argument);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        memset(&request, 0, sizeof(request));
        request.source_logical_program_counter = TEST_SOURCE_PROGRAM_COUNTER;
        request.operation_identifier = TEST_OPERATION_IDENTIFIER_COPY;
        request.read_arguments.data = output_read_argument;
        request.read_arguments.count = 1U;
        request.write_arguments.data = &write_argument;
        request.write_arguments.count = 1U;
        status = binrwir_program_builder_add_instruction(builder, &request,
                                                         &identifier);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_program_builder_finalize(builder, output_program);
    }
    (void)binrwir_program_builder_destroy(builder);
    return status;
}

int main(void) {
    const runtimec_allocator_t allocator = {
        .context = NULL,
        .allocate = test_allocate,
        .deallocate = test_deallocate,
    };
    binrwir_program_t *program = NULL;
    binrwir_frame_bindings_t *created_bindings = NULL;
    binrwir_execution_frame_t frame;
    binrwir_execution_exit_t exit_state;
    binrwir_registry_t registry;
    binrwir_argument_t read_argument;
    vmbin_value_location_t location;
    runtimec_error_t error;
    runtimec_status_t status;

    status = test_build_program(&allocator, &program, &read_argument);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        printf("FAIL cannot build program (status=%d)\n", (int)status);
        return 1;
    }

    /* ① 下层直调：error.code 是值访问自己的 UNBOUND_SLOT。 */
    EXPECT(binrwir_execution_frame_initialize(&frame, program, NULL, 1U, 1U,
                                              0U) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(binrwir_frame_bindings_create(&allocator, 4U, &created_bindings) ==
           RUNTIMEC_STATUS_SUCCESS);
    frame.bindings = created_bindings;
    runtimec_error_reset(&error);
    status = vmbin_value_access_locate_body(&frame, &read_argument, &location,
                                            &error);
    EXPECT(status != RUNTIMEC_STATUS_SUCCESS);
    EXPECT(error.code == (int64_t)VMBIN_VALUE_ACCESS_DIAG_UNBOUND_SLOT);

    /* ② 经解释器：detail_code 只能是解释器自己的 VALUE_ACCESS，不是 3。 */
    EXPECT(binrwir_registry_initialize(&registry) == RUNTIMEC_STATUS_SUCCESS);
    memset(&exit_state, 0, sizeof(exit_state));
    status = vmbin_execute(program, NULL, &frame, &registry, NULL, NULL,
                           &exit_state);
    EXPECT(status != RUNTIMEC_STATUS_SUCCESS);
    EXPECT(exit_state.kind == BINRWIR_EXECUTION_EXIT_ERROR);
    EXPECT(exit_state.detail_code == VMBIN_INTERPRETER_DIAG_VALUE_ACCESS);
    EXPECT(exit_state.detail_code !=
           (uint32_t)VMBIN_VALUE_ACCESS_DIAG_UNBOUND_SLOT);

    EXPECT(binrwir_frame_bindings_destroy(created_bindings) ==
           RUNTIMEC_STATUS_SUCCESS);
    EXPECT(binrwir_execution_frame_destroy(&frame) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(binrwir_program_destroy(program) == RUNTIMEC_STATUS_SUCCESS);

    if (failures != 0) {
        printf("FAILURES %d\n", failures);
    }
    return failures == 0 ? 0 : 1;
}
