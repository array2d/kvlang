/*
 * vmbin 调用控制：进入时建立形参绑定、按地址直达实参存储、返回时只释放绑定表。
 * 值用测试自己持有的 XValue 形状缓冲模拟（值头 8 字节 + 值体）。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "binrwir/execution_frame.h"
#include "binrwir/frame_bindings.h"
#include "binrwir/program.h"
#include "contract/status.h"
#include "kvspace/xvalue_head.h"
#include "vmbin/builtin/registry.h"
#include "vmbin/call.h"
#include "vmbin/frame_slot.h"
#include "vmbin/interpreter.h"

static int failures;

#define RUNTIMEC_EXPECT(condition)                                                \
    do {                                                                          \
        if (!(condition)) {                                                        \
            fprintf(stderr, "%s:%d: expectation failed: %s\n", __FILE__,         \
                    __LINE__, #condition);                                        \
            ++failures;                                                            \
        }                                                                          \
    } while (0)

#define TEST_VALUE_CAPACITY 64U
#define TEST_COPY_OPERATION UINT32_C(0x0001)
#define TEST_CALL_OPERATION UINT32_C(0x0024)

typedef struct test_value {
    uint8_t storage[KVSPACE_XVALUE_HEAD_LEN + TEST_VALUE_CAPACITY];
    size_t body_size;
} test_value_t;

static void test_value_initialize(test_value_t *value, size_t body_size) {
    kvspaceXvalueHead head;

    memset(&head, 0, sizeof(head));
    kvspaceXvalueHeadSetLen(&head, body_size);
    head.storetype = KVSPACE_XVALUE_ST_SHORT;
    memset(value->storage, 0, sizeof(value->storage));
    memcpy(value->storage, &head, sizeof(head));
    value->body_size = body_size;
}

static const uint8_t *test_value_body(const test_value_t *value) {
    return value->storage + KVSPACE_XVALUE_HEAD_LEN;
}

static runtimec_value_body_address_t test_value_body_address(
    const test_value_t *value) {
    return (runtimec_value_body_address_t)(uintptr_t)test_value_body(value);
}

static runtimec_xvalue_address_t test_value_xvalue_address(
    const test_value_t *value) {
    return (runtimec_xvalue_address_t)(uintptr_t)value->storage;
}

static int64_t test_value_int64(const test_value_t *value) {
    int64_t result = 0;

    memcpy(&result, test_value_body(value), sizeof(result));
    return result;
}

static void test_value_set_int64(test_value_t *value, int64_t number) {
    memcpy((void *)test_value_body(value), &number, sizeof(number));
}

/* ---------------- 分配器 ---------------- */

typedef struct test_allocator_context {
    size_t live_allocations;
} test_allocator_context_t;

static void *test_allocate(void *context, size_t size, size_t alignment) {
    test_allocator_context_t *counters = (test_allocator_context_t *)context;
    size_t effective_alignment =
        alignment < sizeof(void *) ? sizeof(void *) : alignment;
    size_t rounded_size = size;
    void *allocation;

    if (rounded_size % effective_alignment != 0U) {
        rounded_size +=
            effective_alignment - (rounded_size % effective_alignment);
    }
    allocation = aligned_alloc(effective_alignment, rounded_size);
    if (allocation == NULL) {
        return NULL;
    }
    counters->live_allocations += 1U;
    return allocation;
}

static void test_deallocate(void *context, void *allocation, size_t size,
                            size_t alignment) {
    test_allocator_context_t *counters = (test_allocator_context_t *)context;

    (void)size;
    (void)alignment;
    if (allocation == NULL) {
        return;
    }
    counters->live_allocations -= 1U;
    free(allocation);
}

static runtimec_allocator_t test_allocator(
    test_allocator_context_t *counters) {
    runtimec_allocator_t allocator = {
        .context = counters,
        .allocate = test_allocate,
        .deallocate = test_deallocate,
    };

    return allocator;
}

/* ---------------- Program 组装 ---------------- */

typedef struct test_parameter {
    binrwir_parameter_direction_t direction;
    uint32_t argument_index;
    const char *type_name;
} test_parameter_t;

typedef struct test_alias_rule {
    uint32_t read_parameter_index;
    uint32_t write_parameter_index;
    binrwir_alias_policy_t policy;
} test_alias_rule_t;

typedef struct test_instruction {
    uint32_t operation_identifier;
    uint32_t source_program_counter;
    binrwir_argument_t reads[4];
    size_t read_count;
    binrwir_argument_t writes[2];
    size_t write_count;
} test_instruction_t;

typedef struct test_program_source {
    const test_parameter_t *parameters;
    size_t parameter_count;
    const test_alias_rule_t *alias_rules;
    size_t alias_rule_count;
    const char *symbols[8];
    size_t symbol_count;
    const char *literal_types[8];
    const uint8_t *literal_bytes[8];
    size_t literal_sizes[8];
    size_t literal_count;
    const test_instruction_t *instructions;
    size_t instruction_count;
} test_program_source_t;

static runtimec_status_t test_program_build(
    const runtimec_allocator_t *allocator, const test_program_source_t *source,
    binrwir_program_t **output_program) {
    binrwir_program_identity_t identity;
    binrwir_program_builder_t *builder = NULL;
    uint16_t symbol_index = BINRWIR_SYMBOL_INDEX_NONE;
    uint32_t identifier = BINRWIR_IDENTIFIER_NONE;
    size_t ordinal;
    runtimec_status_t status;

    memset(&identity, 0, sizeof(identity));
    identity.snapshot_generation = 1U;
    identity.program_generation = 2U;
    identity.schema_version = 1U;
    identity.rwir_manifest_version = 1U;
    identity.binrwir_binary_interface_generation =
        BINRWIR_BINARY_INTERFACE_GENERATION;
    identity.canonical_encoding_revision = 1U;
    status = binrwir_program_builder_create(allocator, &identity, &builder);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    for (ordinal = 0U; status == RUNTIMEC_STATUS_SUCCESS &&
                        ordinal < source->parameter_count;
         ++ordinal) {
        uint32_t parameter_index = BINRWIR_ARGUMENT_INDEX_NONE;

        status = binrwir_program_builder_add_parameter(
            builder, source->parameters[ordinal].direction,
            source->parameters[ordinal].argument_index,
            runtimec_string_view_from_c_string(
                source->parameters[ordinal].type_name),
            &parameter_index);
    }
    for (ordinal = 0U; status == RUNTIMEC_STATUS_SUCCESS &&
                        ordinal < source->alias_rule_count;
         ++ordinal) {
        binrwir_alias_rule_t rule;
        uint32_t rule_identifier = BINRWIR_IDENTIFIER_NONE;

        memset(&rule, 0, sizeof(rule));
        rule.read_parameter_index =
            source->alias_rules[ordinal].read_parameter_index;
        rule.write_parameter_index =
            source->alias_rules[ordinal].write_parameter_index;
        rule.policy = (uint8_t)source->alias_rules[ordinal].policy;
        status = binrwir_program_builder_add_alias_rule(builder, &rule,
                                                        &rule_identifier);
    }
    for (ordinal = 0U; status == RUNTIMEC_STATUS_SUCCESS &&
                        ordinal < source->symbol_count;
         ++ordinal) {
        status = binrwir_program_builder_add_symbol(
            builder,
            runtimec_string_view_from_c_string(source->symbols[ordinal]),
            &symbol_index);
    }
    for (ordinal = 0U; status == RUNTIMEC_STATUS_SUCCESS &&
                        ordinal < source->literal_count;
         ++ordinal) {
        uint32_t literal_index = BINRWIR_LITERAL_INDEX_NONE;

        status = binrwir_program_builder_add_literal(
            builder,
            runtimec_bytes_view_make(source->literal_bytes[ordinal],
                                     source->literal_sizes[ordinal]),
            runtimec_string_view_from_c_string(source->literal_types[ordinal]),
            &literal_index);
    }
    for (ordinal = 0U; status == RUNTIMEC_STATUS_SUCCESS &&
                        ordinal < source->instruction_count;
         ++ordinal) {
        binrwir_instruction_build_request_t request;
        const test_instruction_t *instruction = &source->instructions[ordinal];

        memset(&request, 0, sizeof(request));
        request.operation_identifier = instruction->operation_identifier;
        request.source_logical_program_counter =
            instruction->source_program_counter;
        request.read_arguments.data = instruction->reads;
        request.read_arguments.count = instruction->read_count;
        request.write_arguments.data = instruction->writes;
        request.write_arguments.count = instruction->write_count;
        status = binrwir_program_builder_add_instruction(builder, &request,
                                                         &identifier);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_program_builder_finalize(builder, output_program);
    }
    (void)binrwir_program_builder_destroy(builder);
    return status;
}

static void test_int64_bytes(int64_t value, uint8_t output[8]) {
    memcpy(output, &value, sizeof(value));
}

/* 建 Frame 并给一张容量足够的绑定表。 */
static runtimec_status_t test_frame_create(
    binrwir_execution_frame_t *frame, const binrwir_program_t *program,
    const runtimec_allocator_t *allocator) {
    runtimec_status_t status = binrwir_execution_frame_initialize(
        frame, program, NULL, 1U, 2U, 0U);
    binrwir_frame_bindings_t *bindings = NULL;

    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = binrwir_frame_bindings_create(allocator, 8U, &bindings);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    frame->bindings = bindings;
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t test_frame_release(binrwir_execution_frame_t *frame) {
    runtimec_status_t status = RUNTIMEC_STATUS_SUCCESS;

    if (frame->bindings != NULL) {
        status = binrwir_frame_bindings_destroy(frame->bindings);
        frame->bindings = NULL;
    }
    (void)binrwir_execution_frame_destroy(frame);
    return status;
}

/*
 * 调用方签名：1 读参、1 写参。绑定槽号按 frame_slot 的划分：读参 0、写参 0+1=1。
 */
static runtimec_status_t test_bind_caller(
    binrwir_execution_frame_t *caller, const test_value_t *read_value,
    const test_value_t *write_value) {
    /* 文字面量/静态实参的用例不需要调用方读参绑定，传 NULL 跳过。 */
    if (read_value == NULL) {
        return binrwir_frame_bindings_bind(
            caller->bindings, 1U,
            runtimec_key_view_from_c_string("/vthread/1/[d]/[0,1]"),
            runtimec_key_view_from_c_string("/t/write_actual"),
            test_value_xvalue_address(write_value), 1U);
    }
    runtimec_status_t status = binrwir_frame_bindings_bind(
        caller->bindings, 0U,
        runtimec_key_view_from_c_string("/vthread/1/[d]/[0,-1]"),
        runtimec_key_view_from_c_string("/t/read_actual"),
        test_value_xvalue_address(read_value), 1U);

    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    return binrwir_frame_bindings_bind(
        caller->bindings, 1U,
        runtimec_key_view_from_c_string("/vthread/1/[d]/[0,1]"),
        runtimec_key_view_from_c_string("/t/write_actual"),
        test_value_xvalue_address(write_value), 1U);
}

/* 一条 call 指令：读区 = [函数名, 实参...]，写区 = 写实参。 */
static void test_call_instruction(test_instruction_t *instruction,
                                  const char *function_name,
                                  const binrwir_argument_t *read_actuals,
                                  size_t read_actual_count,
                                  const binrwir_argument_t *write_actuals,
                                  size_t write_actual_count) {
    size_t ordinal;

    memset(instruction, 0, sizeof(*instruction));
    instruction->operation_identifier = TEST_CALL_OPERATION;
    instruction->source_program_counter = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        0U, BINRWIR_REFERENCE_FUNCTION, 0U, 0U,
                        &instruction->reads[0]) == RUNTIMEC_STATUS_SUCCESS);
    (void)function_name;
    for (ordinal = 0U; ordinal < read_actual_count; ++ordinal) {
        instruction->reads[ordinal + 1U] = read_actuals[ordinal];
    }
    instruction->read_count = read_actual_count + 1U;
    for (ordinal = 0U; ordinal < write_actual_count; ++ordinal) {
        instruction->writes[ordinal] = write_actuals[ordinal];
    }
    instruction->write_count = write_actual_count;
}

/* ---------------- 用例 ---------------- */

/* 进入 + 执行 + 返回：读参绑定到实参存储，写参直达调用方存储。 */
static void test_enter_binds_and_return_releases(void) {
    test_allocator_context_t counters = {0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    test_value_t read_value;
    test_value_t write_value;
    binrwir_program_t *caller_program = NULL;
    binrwir_program_t *callee_program = NULL;
    binrwir_execution_frame_t caller;
    binrwir_execution_frame_t callee;
    binrwir_execution_exit_t exit_state;
    vmbin_builtin_registry_t registry;
    vmbin_call_control_t *control = NULL;
    vmbin_call_request_t request = {0U, 2U};
    binrwir_argument_t read_actual;
    binrwir_argument_t write_actual;
    test_instruction_t call_instruction;
    test_parameter_t caller_parameters[2];
    test_parameter_t callee_parameters[2];
    test_program_source_t caller_source;
    test_program_source_t callee_source;
    test_instruction_t callee_instruction;

    test_value_initialize(&read_value, sizeof(int64_t));
    test_value_set_int64(&read_value, 11);
    test_value_initialize(&write_value, sizeof(int64_t));

    caller_parameters[0].direction = BINRWIR_PARAMETER_DIRECTION_READ;
    caller_parameters[0].argument_index = 1U;
    caller_parameters[0].type_name = "int64";
    caller_parameters[1].direction = BINRWIR_PARAMETER_DIRECTION_WRITE;
    caller_parameters[1].argument_index = 2U;
    caller_parameters[1].type_name = "int64";
    callee_parameters[0] = caller_parameters[0];
    callee_parameters[1] = caller_parameters[1];

    RUNTIMEC_EXPECT(binrwir_argument_create(
                        1U, BINRWIR_REFERENCE_PARAMETER_READ, 0U, 0U,
                        &read_actual) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        2U, BINRWIR_REFERENCE_PARAMETER_WRITE, 0U, 0U,
                        &write_actual) == RUNTIMEC_STATUS_SUCCESS);
    test_call_instruction(&call_instruction, "callee", &read_actual, 1U,
                          &write_actual, 1U);
    memset(&caller_source, 0, sizeof(caller_source));
    caller_source.parameters = caller_parameters;
    caller_source.parameter_count = 2U;
    caller_source.symbols[0] = "callee";
    caller_source.symbol_count = 1U;
    caller_source.instructions = &call_instruction;
    caller_source.instruction_count = 1U;

    /* 被调体：把读形参复制到写形参。 */
    memset(&callee_instruction, 0, sizeof(callee_instruction));
    callee_instruction.operation_identifier = TEST_COPY_OPERATION;
    callee_instruction.source_program_counter = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        1U, BINRWIR_REFERENCE_PARAMETER_READ, 0U, 0U,
                        &callee_instruction.reads[0]) == RUNTIMEC_STATUS_SUCCESS);
    callee_instruction.read_count = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        2U, BINRWIR_REFERENCE_PARAMETER_WRITE, 0U, 0U,
                        &callee_instruction.writes[0]) == RUNTIMEC_STATUS_SUCCESS);
    callee_instruction.write_count = 1U;
    memset(&callee_source, 0, sizeof(callee_source));
    callee_source.parameters = callee_parameters;
    callee_source.parameter_count = 2U;
    callee_source.instructions = &callee_instruction;
    callee_source.instruction_count = 1U;

    RUNTIMEC_EXPECT(test_program_build(&allocator, &caller_source,
                                       &caller_program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(test_program_build(&allocator, &callee_source,
                                       &callee_program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(test_frame_create(&caller, caller_program, &allocator) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(test_bind_caller(&caller, &read_value, &write_value) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_execution_frame_initialize(&callee, callee_program,
                                                       NULL, 1U, 2U, 0U) ==
                    RUNTIMEC_STATUS_SUCCESS);

    RUNTIMEC_EXPECT(vmbin_call_control_create(&caller, callee_program, &request,
                                              &allocator, &control) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_call_enter(&caller, &callee, control, &exit_state) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(callee.bindings != NULL);
    RUNTIMEC_EXPECT(callee.parent == &caller);

    /* 被调体执行：读形参（= 调用方实参存储）写进写形参（= 调用方另一块存储）。 */
    RUNTIMEC_EXPECT(vmbin_builtin_registry_initialize(&registry, NULL, 0U) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_execute(callee_program, NULL, &callee, &registry,
                                  NULL, &exit_state) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(exit_state.kind == BINRWIR_EXECUTION_EXIT_DONE);
    RUNTIMEC_EXPECT(test_value_int64(&write_value) == 11);

    RUNTIMEC_EXPECT(vmbin_call_return(&caller, &callee, control, &exit_state) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_call_control_destroy(control) ==
                    RUNTIMEC_STATUS_SUCCESS);

    RUNTIMEC_EXPECT(binrwir_execution_frame_destroy(&callee) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(test_frame_release(&caller) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_destroy(caller_program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_destroy(callee_program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

/* 进入的公共前半段：调用方帧、被调程序、call 指令。 */
typedef struct call_fixture {
    binrwir_program_t *caller_program;
    binrwir_program_t *callee_program;
    binrwir_execution_frame_t caller;
    binrwir_execution_frame_t callee;
    vmbin_call_control_t *control;
} call_fixture_t;

static runtimec_status_t call_fixture_build(
    const runtimec_allocator_t *allocator, const test_program_source_t *callee_source,
    const test_value_t *read_value, const test_value_t *write_value,
    const test_program_source_t *caller_source, call_fixture_t *fixture) {
    vmbin_call_request_t request = {0U, 2U};
    runtimec_status_t status;

    memset(fixture, 0, sizeof(*fixture));
    status = test_program_build(allocator, caller_source,
                                &fixture->caller_program);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = test_program_build(allocator, callee_source,
                                    &fixture->callee_program);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = test_frame_create(&fixture->caller, fixture->caller_program,
                                   allocator);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = test_bind_caller(&fixture->caller, read_value, write_value);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_execution_frame_initialize(
            &fixture->callee, fixture->callee_program, NULL, 1U, 2U, 0U);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = vmbin_call_control_create(&fixture->caller,
                                           fixture->callee_program, &request,
                                           allocator, &fixture->control);
    }
    return status;
}

static void call_fixture_release(const runtimec_allocator_t *allocator,
                                 call_fixture_t *fixture) {
    (void)allocator;
    if (fixture->control != NULL) {
        (void)vmbin_call_control_destroy(fixture->control);
    }
    (void)binrwir_execution_frame_destroy(&fixture->callee);
    (void)test_frame_release(&fixture->caller);
    if (fixture->caller_program != NULL) {
        (void)binrwir_program_destroy(fixture->caller_program);
    }
    if (fixture->callee_program != NULL) {
        (void)binrwir_program_destroy(fixture->callee_program);
    }
}

/* 形状固定的调用方源码：1 读参 + 1 写参，一条 call。 */
static void call_fixture_caller_source(test_parameter_t *parameters,
                                       test_instruction_t *instruction,
                                       test_program_source_t *source,
                                       const binrwir_argument_t *read_actual,
                                       const binrwir_argument_t *write_actual) {
    parameters[0].direction = BINRWIR_PARAMETER_DIRECTION_READ;
    parameters[0].argument_index = 1U;
    parameters[0].type_name = "int64";
    parameters[1].direction = BINRWIR_PARAMETER_DIRECTION_WRITE;
    parameters[1].argument_index = 2U;
    parameters[1].type_name = "int64";
    test_call_instruction(instruction, "callee", read_actual, 1U, write_actual,
                          1U);
    memset(source, 0, sizeof(*source));
    source->parameters = parameters;
    source->parameter_count = 2U;
    source->symbols[0] = "callee";
    source->symbol_count = 1U;
    source->instructions = instruction;
    source->instruction_count = 1U;
}

/* NO_ALIAS（未声明）下读写指向同一地址：进入必须失败。 */
static void test_alias_violation_is_rejected(void) {
    test_allocator_context_t counters = {0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    test_value_t shared;
    binrwir_argument_t read_actual;
    binrwir_argument_t write_actual;
    binrwir_execution_exit_t exit_state;
    test_parameter_t caller_parameters[2];
    test_parameter_t callee_parameters[2];
    test_instruction_t caller_instruction;
    test_instruction_t callee_instruction;
    test_program_source_t caller_source;
    test_program_source_t callee_source;
    call_fixture_t fixture;

    test_value_initialize(&shared, sizeof(int64_t));
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        1U, BINRWIR_REFERENCE_PARAMETER_READ, 0U, 0U,
                        &read_actual) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        2U, BINRWIR_REFERENCE_PARAMETER_WRITE, 0U, 0U,
                        &write_actual) == RUNTIMEC_STATUS_SUCCESS);
    call_fixture_caller_source(caller_parameters, &caller_instruction,
                               &caller_source, &read_actual, &write_actual);

    callee_parameters[0] = caller_parameters[0];
    callee_parameters[1] = caller_parameters[1];
    memset(&callee_instruction, 0, sizeof(callee_instruction));
    callee_instruction.operation_identifier = TEST_COPY_OPERATION;
    callee_instruction.source_program_counter = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        1U, BINRWIR_REFERENCE_PARAMETER_READ, 0U, 0U,
                        &callee_instruction.reads[0]) == RUNTIMEC_STATUS_SUCCESS);
    callee_instruction.read_count = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        2U, BINRWIR_REFERENCE_PARAMETER_WRITE, 0U, 0U,
                        &callee_instruction.writes[0]) == RUNTIMEC_STATUS_SUCCESS);
    callee_instruction.write_count = 1U;
    memset(&callee_source, 0, sizeof(callee_source));
    callee_source.parameters = callee_parameters;
    callee_source.parameter_count = 2U;
    callee_source.instructions = &callee_instruction;
    callee_source.instruction_count = 1U;

    RUNTIMEC_EXPECT(call_fixture_build(&allocator, &callee_source, &shared,
                                       &shared, &caller_source, &fixture) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_call_enter(&fixture.caller, &fixture.callee,
                                     fixture.control, &exit_state) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(exit_state.kind == BINRWIR_EXECUTION_EXIT_ERROR);
    RUNTIMEC_EXPECT(fixture.callee.bindings == NULL);
    call_fixture_release(&allocator, &fixture);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

/* MAY_ALIAS 下同地址合法：写形参改完后，读形参看得到新值。 */
static void test_may_alias_shares_storage(void) {
    test_allocator_context_t counters = {0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    test_value_t shared;
    test_value_t seen;
    binrwir_argument_t read_actual;
    binrwir_argument_t write_actual;
    binrwir_execution_exit_t exit_state;
    vmbin_builtin_registry_t registry;
    test_parameter_t caller_parameters[2];
    test_parameter_t callee_parameters[2];
    test_alias_rule_t alias_rules[1];
    test_instruction_t caller_instruction;
    test_instruction_t callee_instruction;
    test_program_source_t caller_source;
    test_program_source_t callee_source;
    uint8_t literal[8];
    call_fixture_t fixture;

    test_value_initialize(&shared, sizeof(int64_t));
    test_value_initialize(&seen, sizeof(int64_t));
    test_int64_bytes(5, literal);
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        1U, BINRWIR_REFERENCE_PARAMETER_READ, 0U, 0U,
                        &read_actual) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        2U, BINRWIR_REFERENCE_PARAMETER_WRITE, 0U, 0U,
                        &write_actual) == RUNTIMEC_STATUS_SUCCESS);
    call_fixture_caller_source(caller_parameters, &caller_instruction,
                               &caller_source, &read_actual, &write_actual);

    /* 被调体：[0] 字面量写进写形参；[1] 读形参复制到静态 key "/t/seen"。 */
    callee_parameters[0] = caller_parameters[0];
    callee_parameters[1] = caller_parameters[1];
    alias_rules[0].read_parameter_index = 0U;
    alias_rules[0].write_parameter_index = 0U;
    alias_rules[0].policy = BINRWIR_ALIAS_MAY_ALIAS;
    memset(&callee_instruction, 0, sizeof(callee_instruction));
    callee_instruction.operation_identifier = TEST_COPY_OPERATION;
    callee_instruction.source_program_counter = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        1U, BINRWIR_REFERENCE_LITERAL, 0U, 0U,
                        &callee_instruction.reads[0]) == RUNTIMEC_STATUS_SUCCESS);
    callee_instruction.read_count = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        2U, BINRWIR_REFERENCE_PARAMETER_WRITE, 0U, 0U,
                        &callee_instruction.writes[0]) == RUNTIMEC_STATUS_SUCCESS);
    callee_instruction.write_count = 1U;
    {
        test_instruction_t instructions[2];

        memset(instructions, 0, sizeof(instructions));
        instructions[0] = callee_instruction;
        instructions[1].operation_identifier = TEST_COPY_OPERATION;
        instructions[1].source_program_counter = 2U;
        RUNTIMEC_EXPECT(binrwir_argument_create(
                            1U, BINRWIR_REFERENCE_PARAMETER_READ, 0U, 0U,
                            &instructions[1].reads[0]) ==
                        RUNTIMEC_STATUS_SUCCESS);
        instructions[1].read_count = 1U;
        RUNTIMEC_EXPECT(binrwir_argument_create(
                            4U, BINRWIR_REFERENCE_STATIC, 0U,
                            test_value_body_address(&seen),
                            &instructions[1].writes[0]) ==
                        RUNTIMEC_STATUS_SUCCESS);
        instructions[1].write_count = 1U;

        memset(&callee_source, 0, sizeof(callee_source));
        callee_source.parameters = callee_parameters;
        callee_source.parameter_count = 2U;
        callee_source.alias_rules = alias_rules;
        callee_source.alias_rule_count = 1U;
        callee_source.symbols[0] = "/t/seen";
        callee_source.symbol_count = 1U;
        callee_source.literal_types[0] = "int64";
        callee_source.literal_bytes[0] = literal;
        callee_source.literal_sizes[0] = sizeof(literal);
        callee_source.literal_count = 1U;
        callee_source.instructions = instructions;
        callee_source.instruction_count = 2U;

        RUNTIMEC_EXPECT(call_fixture_build(&allocator, &callee_source, &shared,
                                           &shared, &caller_source,
                                           &fixture) == RUNTIMEC_STATUS_SUCCESS);
        RUNTIMEC_EXPECT(vmbin_call_enter(&fixture.caller, &fixture.callee,
                                         fixture.control, &exit_state) ==
                        RUNTIMEC_STATUS_SUCCESS);
        RUNTIMEC_EXPECT(vmbin_builtin_registry_initialize(&registry, NULL,
                                                          0U) ==
                        RUNTIMEC_STATUS_SUCCESS);
        RUNTIMEC_EXPECT(vmbin_execute(fixture.callee_program, NULL,
                                      &fixture.callee, &registry, NULL,
                                      &exit_state) == RUNTIMEC_STATUS_SUCCESS);
        /* 别名生效：写进去的 5 被随后的读看到了。 */
        RUNTIMEC_EXPECT(test_value_int64(&seen) == 5);
        RUNTIMEC_EXPECT(vmbin_call_return(&fixture.caller, &fixture.callee,
                                          fixture.control, &exit_state) ==
                        RUNTIMEC_STATUS_SUCCESS);
    }
    call_fixture_release(&allocator, &fixture);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

/* 嵌套两层：内层被调的写形参仍指向最外层实参的存储。 */
static void test_nested_calls_keep_outer_addresses(void) {
    test_allocator_context_t counters = {0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    test_value_t source_value;
    test_value_t sink_value;
    binrwir_argument_t read_actual;
    binrwir_argument_t write_actual;
    binrwir_execution_exit_t exit_state;
    vmbin_builtin_registry_t registry;
    test_parameter_t middle_parameters[2];
    test_parameter_t inner_parameters[2];
    test_parameter_t caller_parameters[2];
    test_instruction_t caller_instruction;
    test_instruction_t middle_call_instruction;
    test_instruction_t inner_instruction;
    test_program_source_t caller_source;
    test_program_source_t middle_source;
    test_program_source_t inner_source;
    binrwir_program_t *middle_program = NULL;
    binrwir_program_t *inner_program = NULL;
    call_fixture_t fixture;
    binrwir_execution_frame_t middle_frame;
    binrwir_execution_frame_t inner_frame;
    vmbin_call_control_t *middle_control = NULL;
    vmbin_call_control_t *inner_control = NULL;
    vmbin_call_request_t middle_request = {0U, 2U};
    vmbin_call_request_t inner_request = {0U, 2U};

    test_value_initialize(&source_value, sizeof(int64_t));
    test_value_set_int64(&source_value, 33);
    test_value_initialize(&sink_value, sizeof(int64_t));

    /* 最外层调用方：1 读参 + 1 写参，一条 call 到 middle。 */
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        1U, BINRWIR_REFERENCE_PARAMETER_READ, 0U, 0U,
                        &read_actual) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        2U, BINRWIR_REFERENCE_PARAMETER_WRITE, 0U, 0U,
                        &write_actual) == RUNTIMEC_STATUS_SUCCESS);
    call_fixture_caller_source(caller_parameters, &caller_instruction,
                               &caller_source, &read_actual, &write_actual);

    /* middle：形参原样转交给 inner（读实参 + 写实参都是自己的形参）。 */
    middle_parameters[0] = caller_parameters[0];
    middle_parameters[1] = caller_parameters[1];
    {
        binrwir_argument_t middle_read_actual;
        binrwir_argument_t middle_write_actual;

        RUNTIMEC_EXPECT(binrwir_argument_create(
                            1U, BINRWIR_REFERENCE_PARAMETER_READ, 0U, 0U,
                            &middle_read_actual) == RUNTIMEC_STATUS_SUCCESS);
        RUNTIMEC_EXPECT(binrwir_argument_create(
                            2U, BINRWIR_REFERENCE_PARAMETER_WRITE, 0U, 0U,
                            &middle_write_actual) == RUNTIMEC_STATUS_SUCCESS);
        test_call_instruction(&middle_call_instruction, "inner",
                              &middle_read_actual, 1U, &middle_write_actual, 1U);
    }
    memset(&middle_source, 0, sizeof(middle_source));
    middle_source.parameters = middle_parameters;
    middle_source.parameter_count = 2U;
    middle_source.symbols[0] = "inner";
    middle_source.symbol_count = 1U;
    middle_source.instructions = &middle_call_instruction;
    middle_source.instruction_count = 1U;

    /* inner：读形参复制到写形参。 */
    inner_parameters[0] = caller_parameters[0];
    inner_parameters[1] = caller_parameters[1];
    memset(&inner_instruction, 0, sizeof(inner_instruction));
    inner_instruction.operation_identifier = TEST_COPY_OPERATION;
    inner_instruction.source_program_counter = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        1U, BINRWIR_REFERENCE_PARAMETER_READ, 0U, 0U,
                        &inner_instruction.reads[0]) == RUNTIMEC_STATUS_SUCCESS);
    inner_instruction.read_count = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        2U, BINRWIR_REFERENCE_PARAMETER_WRITE, 0U, 0U,
                        &inner_instruction.writes[0]) == RUNTIMEC_STATUS_SUCCESS);
    inner_instruction.write_count = 1U;
    memset(&inner_source, 0, sizeof(inner_source));
    inner_source.parameters = inner_parameters;
    inner_source.parameter_count = 2U;
    inner_source.instructions = &inner_instruction;
    inner_source.instruction_count = 1U;

    RUNTIMEC_EXPECT(call_fixture_build(&allocator, &middle_source,
                                       &source_value, &sink_value,
                                       &caller_source, &fixture) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(test_program_build(&allocator, &inner_source,
                                       &inner_program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    middle_program = fixture.callee_program;

    /* 第一层：caller → middle。 */
    RUNTIMEC_EXPECT(vmbin_call_enter(&fixture.caller, &fixture.callee,
                                     fixture.control, &exit_state) ==
                    RUNTIMEC_STATUS_SUCCESS);
    middle_frame = fixture.callee;

    /* 第二层：middle → inner，实参是 middle 自己的形参。 */
    RUNTIMEC_EXPECT(binrwir_execution_frame_initialize(
                        &inner_frame, inner_program, NULL, 1U, 2U, 0U) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_call_control_create(&middle_frame, inner_program,
                                              &inner_request, &allocator,
                                              &inner_control) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_call_enter(&middle_frame, &inner_frame, inner_control,
                                     &exit_state) ==
                    RUNTIMEC_STATUS_SUCCESS);

    RUNTIMEC_EXPECT(vmbin_builtin_registry_initialize(&registry, NULL, 0U) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_execute(inner_program, NULL, &inner_frame, &registry,
                                  NULL, &exit_state) ==
                    RUNTIMEC_STATUS_SUCCESS);
    /* 内层写进的是 middle 形参的地址，而 middle 形参指的就是最外层实参存储。 */
    RUNTIMEC_EXPECT(test_value_int64(&sink_value) == 33);

    RUNTIMEC_EXPECT(vmbin_call_return(&middle_frame, &inner_frame,
                                      inner_control, &exit_state) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_call_control_destroy(inner_control) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_execution_frame_destroy(&inner_frame) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_call_return(&fixture.caller, &middle_frame,
                                      fixture.control, &exit_state) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_destroy(inner_program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    call_fixture_release(&allocator, &fixture);
    (void)middle_program;
    (void)middle_control;
    (void)middle_request;
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

/* 字面量实参：没有存储位置，绑定表存内联字节，被调体直接从池里读。 */
static void test_literal_actual_is_inline(void) {
    test_allocator_context_t counters = {0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    test_value_t write_value;
    binrwir_argument_t read_actual;
    binrwir_argument_t write_actual;
    binrwir_execution_exit_t exit_state;
    vmbin_builtin_registry_t registry;
    test_parameter_t caller_parameters[2];
    test_parameter_t callee_parameters[2];
    test_instruction_t caller_instruction;
    test_program_source_t caller_source;
    test_program_source_t callee_source;
    test_instruction_t callee_instructions[1];
    uint8_t literal[8];
    call_fixture_t fixture;

    test_value_initialize(&write_value, sizeof(int64_t));
    test_int64_bytes(64, literal);
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        1U, BINRWIR_REFERENCE_LITERAL, 0U, 0U,
                        &read_actual) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        2U, BINRWIR_REFERENCE_PARAMETER_WRITE, 0U, 0U,
                        &write_actual) == RUNTIMEC_STATUS_SUCCESS);
    call_fixture_caller_source(caller_parameters, &caller_instruction,
                               &caller_source, &read_actual, &write_actual);
    caller_source.literal_types[0] = "int64";
    caller_source.literal_bytes[0] = literal;
    caller_source.literal_sizes[0] = sizeof(literal);
    caller_source.literal_count = 1U;

    callee_parameters[0] = caller_parameters[0];
    callee_parameters[1] = caller_parameters[1];
    memset(callee_instructions, 0, sizeof(callee_instructions));
    callee_instructions[0].operation_identifier = TEST_COPY_OPERATION;
    callee_instructions[0].source_program_counter = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        1U, BINRWIR_REFERENCE_PARAMETER_READ, 0U, 0U,
                        &callee_instructions[0].reads[0]) ==
                    RUNTIMEC_STATUS_SUCCESS);
    callee_instructions[0].read_count = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        2U, BINRWIR_REFERENCE_PARAMETER_WRITE, 0U, 0U,
                        &callee_instructions[0].writes[0]) ==
                    RUNTIMEC_STATUS_SUCCESS);
    callee_instructions[0].write_count = 1U;
    memset(&callee_source, 0, sizeof(callee_source));
    callee_source.parameters = callee_parameters;
    callee_source.parameter_count = 2U;
    callee_source.instructions = callee_instructions;
    callee_source.instruction_count = 1U;

    RUNTIMEC_EXPECT(call_fixture_build(&allocator, &callee_source, NULL,
                                       &write_value, &caller_source,
                                       &fixture) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_call_enter(&fixture.caller, &fixture.callee,
                                     fixture.control, &exit_state) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_builtin_registry_initialize(&registry, NULL, 0U) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_execute(fixture.callee_program, NULL, &fixture.callee,
                                  &registry, NULL, &exit_state) ==
                    RUNTIMEC_STATUS_SUCCESS);
    /* 字面量 64 经内联绑定进了被调形参，又被复制到调用方的写实参存储。 */
    RUNTIMEC_EXPECT(test_value_int64(&write_value) == 64);
    RUNTIMEC_EXPECT(vmbin_call_return(&fixture.caller, &fixture.callee,
                                      fixture.control, &exit_state) ==
                    RUNTIMEC_STATUS_SUCCESS);
    call_fixture_release(&allocator, &fixture);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

/* 静态实参：payload 是值体地址，进入时经合同逆入口换成 XValue 基址再绑定。 */
static void test_static_actual_is_bound_by_inverse(void) {
    test_allocator_context_t counters = {0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    test_value_t static_value;
    test_value_t write_value;
    binrwir_argument_t read_actual;
    binrwir_argument_t write_actual;
    binrwir_execution_exit_t exit_state;
    vmbin_builtin_registry_t registry;
    test_parameter_t caller_parameters[2];
    test_parameter_t callee_parameters[2];
    test_instruction_t caller_instruction;
    test_program_source_t caller_source;
    test_program_source_t callee_source;
    test_instruction_t callee_instructions[1];
    call_fixture_t fixture;

    test_value_initialize(&static_value, sizeof(int64_t));
    test_value_set_int64(&static_value, 21);
    test_value_initialize(&write_value, sizeof(int64_t));
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        1U, BINRWIR_REFERENCE_STATIC, 0U,
                        test_value_body_address(&static_value),
                        &read_actual) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        2U, BINRWIR_REFERENCE_PARAMETER_WRITE, 0U, 0U,
                        &write_actual) == RUNTIMEC_STATUS_SUCCESS);
    call_fixture_caller_source(caller_parameters, &caller_instruction,
                               &caller_source, &read_actual, &write_actual);
    caller_source.symbols[1] = "/t/static_actual";
    caller_source.symbol_count = 2U;

    callee_parameters[0] = caller_parameters[0];
    callee_parameters[1] = caller_parameters[1];
    memset(callee_instructions, 0, sizeof(callee_instructions));
    callee_instructions[0].operation_identifier = TEST_COPY_OPERATION;
    callee_instructions[0].source_program_counter = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        1U, BINRWIR_REFERENCE_PARAMETER_READ, 0U, 0U,
                        &callee_instructions[0].reads[0]) ==
                    RUNTIMEC_STATUS_SUCCESS);
    callee_instructions[0].read_count = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        2U, BINRWIR_REFERENCE_PARAMETER_WRITE, 0U, 0U,
                        &callee_instructions[0].writes[0]) ==
                    RUNTIMEC_STATUS_SUCCESS);
    callee_instructions[0].write_count = 1U;
    memset(&callee_source, 0, sizeof(callee_source));
    callee_source.parameters = callee_parameters;
    callee_source.parameter_count = 2U;
    callee_source.instructions = callee_instructions;
    callee_source.instruction_count = 1U;

    RUNTIMEC_EXPECT(call_fixture_build(&allocator, &callee_source, NULL,
                                       &write_value, &caller_source,
                                       &fixture) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_call_enter(&fixture.caller, &fixture.callee,
                                     fixture.control, &exit_state) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_builtin_registry_initialize(&registry, NULL, 0U) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_execute(fixture.callee_program, NULL, &fixture.callee,
                                  &registry, NULL, &exit_state) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(test_value_int64(&write_value) == 21);
    RUNTIMEC_EXPECT(vmbin_call_return(&fixture.caller, &fixture.callee,
                                      fixture.control, &exit_state) ==
                    RUNTIMEC_STATUS_SUCCESS);
    call_fixture_release(&allocator, &fixture);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

/* 返回后帧被还原：同一帧可以再进一次调用，不留悬空指针。 */
static void test_return_restores_frame_for_reuse(void) {
    test_allocator_context_t counters = {0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    test_value_t read_value;
    test_value_t write_value;
    binrwir_argument_t read_actual;
    binrwir_argument_t write_actual;
    binrwir_execution_exit_t exit_state;
    test_parameter_t caller_parameters[2];
    test_parameter_t callee_parameters[2];
    test_instruction_t caller_instruction;
    test_instruction_t callee_instruction;
    test_program_source_t caller_source;
    test_program_source_t callee_source;
    call_fixture_t fixture;

    test_value_initialize(&read_value, sizeof(int64_t));
    test_value_initialize(&write_value, sizeof(int64_t));
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        1U, BINRWIR_REFERENCE_PARAMETER_READ, 0U, 0U,
                        &read_actual) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        2U, BINRWIR_REFERENCE_PARAMETER_WRITE, 0U, 0U,
                        &write_actual) == RUNTIMEC_STATUS_SUCCESS);
    call_fixture_caller_source(caller_parameters, &caller_instruction,
                               &caller_source, &read_actual, &write_actual);
    callee_parameters[0] = caller_parameters[0];
    callee_parameters[1] = caller_parameters[1];
    memset(&callee_instruction, 0, sizeof(callee_instruction));
    callee_instruction.operation_identifier = TEST_COPY_OPERATION;
    callee_instruction.source_program_counter = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        1U, BINRWIR_REFERENCE_PARAMETER_READ, 0U, 0U,
                        &callee_instruction.reads[0]) == RUNTIMEC_STATUS_SUCCESS);
    callee_instruction.read_count = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        2U, BINRWIR_REFERENCE_PARAMETER_WRITE, 0U, 0U,
                        &callee_instruction.writes[0]) == RUNTIMEC_STATUS_SUCCESS);
    callee_instruction.write_count = 1U;
    memset(&callee_source, 0, sizeof(callee_source));
    callee_source.parameters = callee_parameters;
    callee_source.parameter_count = 2U;
    callee_source.instructions = &callee_instruction;
    callee_source.instruction_count = 1U;

    RUNTIMEC_EXPECT(call_fixture_build(&allocator, &callee_source, &read_value,
                                       &write_value, &caller_source,
                                       &fixture) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_call_enter(&fixture.caller, &fixture.callee,
                                     fixture.control, &exit_state) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(fixture.callee.bindings != NULL);
    RUNTIMEC_EXPECT(fixture.callee.parent == &fixture.caller);
    RUNTIMEC_EXPECT(vmbin_call_return(&fixture.caller, &fixture.callee,
                                      fixture.control, &exit_state) ==
                    RUNTIMEC_STATUS_SUCCESS);
    /* 还原：不再持有绑定表、不再有父帧、代数归零。 */
    RUNTIMEC_EXPECT(fixture.callee.bindings == NULL);
    RUNTIMEC_EXPECT(fixture.callee.parent == NULL);
    RUNTIMEC_EXPECT(fixture.callee.snapshot_generation ==
                    BINRWIR_FRAME_BINDING_GENERATION_NONE);

    /* 同一帧再次进入：没有被「已有绑定表」挡住。 */
    RUNTIMEC_EXPECT(vmbin_call_enter(&fixture.caller, &fixture.callee,
                                     fixture.control, &exit_state) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_call_return(&fixture.caller, &fixture.callee,
                                      fixture.control, &exit_state) ==
                    RUNTIMEC_STATUS_SUCCESS);
    call_fixture_release(&allocator, &fixture);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

/* 槽容量必须含局部符号：读参 + 写参 + 符号数。 */
static void test_slot_capacity_includes_symbols(void) {
    test_allocator_context_t counters = {0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    binrwir_program_t *program = NULL;
    size_t capacity = 0U;
    test_parameter_t parameters[2];
    test_instruction_t instruction;
    test_program_source_t source;

    parameters[0].direction = BINRWIR_PARAMETER_DIRECTION_READ;
    parameters[0].argument_index = 1U;
    parameters[0].type_name = "int64";
    parameters[1].direction = BINRWIR_PARAMETER_DIRECTION_WRITE;
    parameters[1].argument_index = 2U;
    parameters[1].type_name = "int64";
    memset(&instruction, 0, sizeof(instruction));
    instruction.operation_identifier = TEST_COPY_OPERATION;
    instruction.source_program_counter = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        1U, BINRWIR_REFERENCE_PARAMETER_READ, 0U, 0U,
                        &instruction.reads[0]) == RUNTIMEC_STATUS_SUCCESS);
    instruction.read_count = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        2U, BINRWIR_REFERENCE_PARAMETER_WRITE, 0U, 0U,
                        &instruction.writes[0]) == RUNTIMEC_STATUS_SUCCESS);
    instruction.write_count = 1U;
    memset(&source, 0, sizeof(source));
    source.parameters = parameters;
    source.parameter_count = 2U;
    source.symbols[0] = "/t/local_a";
    source.symbols[1] = "/t/local_b";
    source.symbol_count = 2U;
    source.instructions = &instruction;
    source.instruction_count = 1U;

    RUNTIMEC_EXPECT(test_program_build(&allocator, &source, &program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_frame_slot_capacity(program, &capacity) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(capacity == 4U);
    RUNTIMEC_EXPECT(binrwir_program_destroy(program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

/*
 * CALL 出口必须给出可恢复位置：调用指令后面还有后继指令时，
 * resume_logical_program_counter 是后继的 source pc，不是 NONE。
 */
static void test_call_exit_reports_resume_position(void) {
    test_allocator_context_t counters = {0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    test_value_t read_value;
    test_value_t write_value;
    binrwir_argument_t read_actual;
    binrwir_argument_t write_actual;
    binrwir_execution_exit_t exit_state;
    vmbin_builtin_registry_t registry;
    test_parameter_t caller_parameters[2];
    test_parameter_t callee_parameters[2];
    test_instruction_t caller_instructions[2];
    test_instruction_t callee_instruction;
    test_program_source_t caller_source;
    test_program_source_t callee_source;
    call_fixture_t fixture;

    test_value_initialize(&read_value, sizeof(int64_t));
    test_value_initialize(&write_value, sizeof(int64_t));
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        1U, BINRWIR_REFERENCE_PARAMETER_READ, 0U, 0U,
                        &read_actual) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        2U, BINRWIR_REFERENCE_PARAMETER_WRITE, 0U, 0U,
                        &write_actual) == RUNTIMEC_STATUS_SUCCESS);

    /* [0] call（source pc 1），[1] copy（source pc 2）= 后继。 */
    caller_parameters[0].direction = BINRWIR_PARAMETER_DIRECTION_READ;
    caller_parameters[0].argument_index = 1U;
    caller_parameters[0].type_name = "int64";
    caller_parameters[1].direction = BINRWIR_PARAMETER_DIRECTION_WRITE;
    caller_parameters[1].argument_index = 2U;
    caller_parameters[1].type_name = "int64";
    memset(caller_instructions, 0, sizeof(caller_instructions));
    test_call_instruction(&caller_instructions[0], "callee", &read_actual, 1U,
                          &write_actual, 1U);
    caller_instructions[1].operation_identifier = TEST_COPY_OPERATION;
    caller_instructions[1].source_program_counter = 2U;
    /* argidx 必须等于签名里同槽参数的 argidx：读参 slot0 = 1，写参 slot0 = 2。 */
    caller_instructions[1].reads[0] = read_actual;
    caller_instructions[1].read_count = 1U;
    caller_instructions[1].writes[0] = write_actual;
    caller_instructions[1].write_count = 1U;
    memset(&caller_source, 0, sizeof(caller_source));
    caller_source.parameters = caller_parameters;
    caller_source.parameter_count = 2U;
    caller_source.symbols[0] = "callee";
    caller_source.symbol_count = 1U;
    caller_source.instructions = caller_instructions;
    caller_source.instruction_count = 2U;

    callee_parameters[0] = caller_parameters[0];
    callee_parameters[1] = caller_parameters[1];
    memset(&callee_instruction, 0, sizeof(callee_instruction));
    callee_instruction.operation_identifier = TEST_COPY_OPERATION;
    callee_instruction.source_program_counter = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        1U, BINRWIR_REFERENCE_PARAMETER_READ, 0U, 0U,
                        &callee_instruction.reads[0]) == RUNTIMEC_STATUS_SUCCESS);
    callee_instruction.read_count = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        2U, BINRWIR_REFERENCE_PARAMETER_WRITE, 0U, 0U,
                        &callee_instruction.writes[0]) == RUNTIMEC_STATUS_SUCCESS);
    callee_instruction.write_count = 1U;
    memset(&callee_source, 0, sizeof(callee_source));
    callee_source.parameters = callee_parameters;
    callee_source.parameter_count = 2U;
    callee_source.instructions = &callee_instruction;
    callee_source.instruction_count = 1U;

    RUNTIMEC_EXPECT(call_fixture_build(&allocator, &callee_source, &read_value,
                                       &write_value, &caller_source,
                                       &fixture) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_builtin_registry_initialize(&registry, NULL, 0U) ==
                    RUNTIMEC_STATUS_SUCCESS);
    /* 执行调用方自己的 Program：第一条就是 call。 */
    RUNTIMEC_EXPECT(vmbin_execute(fixture.caller_program, NULL, &fixture.caller,
                                  &registry, NULL, &exit_state) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(exit_state.kind == BINRWIR_EXECUTION_EXIT_CALL);
    RUNTIMEC_EXPECT(exit_state.logical_program_counter == 1U);
    RUNTIMEC_EXPECT(exit_state.resume_logical_program_counter == 2U);
    call_fixture_release(&allocator, &fixture);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

/*
 * 同一帧连续多次调用：每个 round 用不同的实参存储，绑定表每次都重建，
 * 槽号与容量保持自洽，且早先的写不会被后续调用污染。
 *
 * 被调带两个局部符号：容量必须覆盖 读参 + 写参 + 符号数，连续进入也如此。
 */
static void test_repeated_calls_on_same_frame(void) {
    enum { TEST_ROUNDS = 3 };
    test_allocator_context_t counters = {0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    test_value_t read_values[TEST_ROUNDS];
    test_value_t write_values[TEST_ROUNDS];
    binrwir_argument_t read_actual;
    binrwir_argument_t write_actual;
    binrwir_execution_exit_t exit_state;
    vmbin_builtin_registry_t registry;
    test_parameter_t caller_parameters[2];
    test_parameter_t callee_parameters[2];
    test_instruction_t caller_instruction;
    test_instruction_t callee_instruction;
    test_program_source_t caller_source;
    test_program_source_t callee_source;
    call_fixture_t fixture;
    int round;

    for (round = 0; round < TEST_ROUNDS; ++round) {
        test_value_initialize(&read_values[round], sizeof(int64_t));
        test_value_set_int64(&read_values[round], (int64_t)(round + 1) * 10);
        test_value_initialize(&write_values[round], sizeof(int64_t));
    }
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        1U, BINRWIR_REFERENCE_PARAMETER_READ, 0U, 0U,
                        &read_actual) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        2U, BINRWIR_REFERENCE_PARAMETER_WRITE, 0U, 0U,
                        &write_actual) == RUNTIMEC_STATUS_SUCCESS);
    call_fixture_caller_source(caller_parameters, &caller_instruction,
                               &caller_source, &read_actual, &write_actual);

    /* 被调带两个局部符号：容量应当是 2 + 2 = 4，且连续进入都成立。 */
    callee_parameters[0] = caller_parameters[0];
    callee_parameters[1] = caller_parameters[1];
    memset(&callee_instruction, 0, sizeof(callee_instruction));
    callee_instruction.operation_identifier = TEST_COPY_OPERATION;
    callee_instruction.source_program_counter = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        1U, BINRWIR_REFERENCE_PARAMETER_READ, 0U, 0U,
                        &callee_instruction.reads[0]) == RUNTIMEC_STATUS_SUCCESS);
    callee_instruction.read_count = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        2U, BINRWIR_REFERENCE_PARAMETER_WRITE, 0U, 0U,
                        &callee_instruction.writes[0]) == RUNTIMEC_STATUS_SUCCESS);
    callee_instruction.write_count = 1U;
    memset(&callee_source, 0, sizeof(callee_source));
    callee_source.parameters = callee_parameters;
    callee_source.parameter_count = 2U;
    callee_source.symbols[0] = "/t/local_a";
    callee_source.symbols[1] = "/t/local_b";
    callee_source.symbol_count = 2U;
    callee_source.instructions = &callee_instruction;
    callee_source.instruction_count = 1U;

    RUNTIMEC_EXPECT(call_fixture_build(&allocator, &callee_source,
                                       &read_values[0], &write_values[0],
                                       &caller_source, &fixture) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_builtin_registry_initialize(&registry, NULL, 0U) ==
                    RUNTIMEC_STATUS_SUCCESS);

    for (round = 0; round < TEST_ROUNDS; ++round) {
        const binrwir_frame_binding_t *binding = NULL;

        /* 每个 round 把调用方实参改指向本 round 的存储。 */
        RUNTIMEC_EXPECT(binrwir_frame_bindings_bind(
                            fixture.caller.bindings, 0U,
                            runtimec_key_view_from_c_string("/vthread/1/rd"),
                            runtimec_key_view_from_c_string("/t/read_actual"),
                            test_value_xvalue_address(&read_values[round]),
                            1U) == RUNTIMEC_STATUS_SUCCESS);
        RUNTIMEC_EXPECT(binrwir_frame_bindings_bind(
                            fixture.caller.bindings, 1U,
                            runtimec_key_view_from_c_string("/vthread/1/wr"),
                            runtimec_key_view_from_c_string("/t/write_actual"),
                            test_value_xvalue_address(&write_values[round]),
                            1U) == RUNTIMEC_STATUS_SUCCESS);

        RUNTIMEC_EXPECT(vmbin_call_enter(&fixture.caller, &fixture.callee,
                                         fixture.control, &exit_state) ==
                        RUNTIMEC_STATUS_SUCCESS);
        /* 每次进入都重建表，容量含符号，槽号仍是「读 0 / 写 nr+0」。 */
        RUNTIMEC_EXPECT(binrwir_frame_bindings_get_capacity(
                            fixture.callee.bindings) == 4U);
        RUNTIMEC_EXPECT(binrwir_frame_bindings_get(
                            fixture.callee.bindings, 0U,
                            fixture.callee.snapshot_generation, &binding) ==
                        RUNTIMEC_STATUS_SUCCESS);
        RUNTIMEC_EXPECT(binding->xvalue_address ==
                        test_value_xvalue_address(&read_values[round]));
        binding = NULL;
        RUNTIMEC_EXPECT(binrwir_frame_bindings_get(
                            fixture.callee.bindings, 1U,
                            fixture.callee.snapshot_generation, &binding) ==
                        RUNTIMEC_STATUS_SUCCESS);
        RUNTIMEC_EXPECT(binding->xvalue_address ==
                        test_value_xvalue_address(&write_values[round]));

        RUNTIMEC_EXPECT(vmbin_execute(fixture.callee_program, NULL,
                                      &fixture.callee, &registry, NULL,
                                      &exit_state) == RUNTIMEC_STATUS_SUCCESS);
        RUNTIMEC_EXPECT(test_value_int64(&write_values[round]) ==
                        (int64_t)(round + 1) * 10);
        RUNTIMEC_EXPECT(vmbin_call_return(&fixture.caller, &fixture.callee,
                                          fixture.control, &exit_state) ==
                        RUNTIMEC_STATUS_SUCCESS);
        RUNTIMEC_EXPECT(fixture.callee.bindings == NULL);
    }

    /* 前面的 round 没有被后面的调用写脏。 */
    for (round = 0; round < TEST_ROUNDS; ++round) {
        RUNTIMEC_EXPECT(test_value_int64(&write_values[round]) ==
                        (int64_t)(round + 1) * 10);
    }

    call_fixture_release(&allocator, &fixture);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

/* 失败形状：缺参、已进入过的帧、返回前没进入。 */
static void test_call_failure_shapes(void) {
    test_allocator_context_t counters = {0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    test_value_t read_value;
    test_value_t write_value;
    binrwir_argument_t read_actual;
    binrwir_argument_t write_actual;
    binrwir_execution_exit_t exit_state;
    test_parameter_t caller_parameters[2];
    test_parameter_t callee_parameters[2];
    test_instruction_t caller_instruction;
    test_instruction_t callee_instruction;
    test_program_source_t caller_source;
    test_program_source_t callee_source;
    call_fixture_t fixture;

    test_value_initialize(&read_value, sizeof(int64_t));
    test_value_initialize(&write_value, sizeof(int64_t));
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        1U, BINRWIR_REFERENCE_PARAMETER_READ, 0U, 0U,
                        &read_actual) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        2U, BINRWIR_REFERENCE_PARAMETER_WRITE, 0U, 0U,
                        &write_actual) == RUNTIMEC_STATUS_SUCCESS);
    call_fixture_caller_source(caller_parameters, &caller_instruction,
                               &caller_source, &read_actual, &write_actual);
    callee_parameters[0] = caller_parameters[0];
    callee_parameters[1] = caller_parameters[1];
    memset(&callee_instruction, 0, sizeof(callee_instruction));
    callee_instruction.operation_identifier = TEST_COPY_OPERATION;
    callee_instruction.source_program_counter = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        1U, BINRWIR_REFERENCE_PARAMETER_READ, 0U, 0U,
                        &callee_instruction.reads[0]) == RUNTIMEC_STATUS_SUCCESS);
    callee_instruction.read_count = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        2U, BINRWIR_REFERENCE_PARAMETER_WRITE, 0U, 0U,
                        &callee_instruction.writes[0]) == RUNTIMEC_STATUS_SUCCESS);
    callee_instruction.write_count = 1U;
    memset(&callee_source, 0, sizeof(callee_source));
    callee_source.parameters = callee_parameters;
    callee_source.parameter_count = 2U;
    callee_source.instructions = &callee_instruction;
    callee_source.instruction_count = 1U;

    RUNTIMEC_EXPECT(call_fixture_build(&allocator, &callee_source, &read_value,
                                       &write_value, &caller_source,
                                       &fixture) == RUNTIMEC_STATUS_SUCCESS);

    /* 没进入就返回。 */
    RUNTIMEC_EXPECT(vmbin_call_return(&fixture.caller, &fixture.callee,
                                      fixture.control, &exit_state) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(exit_state.kind == BINRWIR_EXECUTION_EXIT_ERROR);

    /* NULL 入参。 */
    RUNTIMEC_EXPECT(vmbin_call_enter(NULL, &fixture.callee, fixture.control,
                                     &exit_state) == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(vmbin_call_enter(&fixture.caller, &fixture.callee, NULL,
                                     &exit_state) == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(vmbin_call_control_create(NULL, fixture.callee_program,
                                              NULL, &allocator, NULL) ==
                    RUNTIMEC_STATUS_INVALID);

    /* 正常进入后，同一帧再进一次要拒绝。 */
    RUNTIMEC_EXPECT(vmbin_call_enter(&fixture.caller, &fixture.callee,
                                     fixture.control, &exit_state) ==
                    RUNTIMEC_STATUS_SUCCESS);
    {
        vmbin_call_control_t *second_control = NULL;
        vmbin_call_request_t request = {0U, 2U};

        RUNTIMEC_EXPECT(vmbin_call_control_create(
                            &fixture.caller, fixture.callee_program, &request,
                            &allocator, &second_control) ==
                        RUNTIMEC_STATUS_SUCCESS);
        RUNTIMEC_EXPECT(vmbin_call_enter(&fixture.caller, &fixture.callee,
                                         second_control, &exit_state) ==
                        RUNTIMEC_STATUS_INVALID);
        RUNTIMEC_EXPECT(vmbin_call_control_destroy(second_control) ==
                        RUNTIMEC_STATUS_SUCCESS);
    }
    RUNTIMEC_EXPECT(vmbin_call_return(&fixture.caller, &fixture.callee,
                                      fixture.control, &exit_state) ==
                    RUNTIMEC_STATUS_SUCCESS);
    call_fixture_release(&allocator, &fixture);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

int main(void) {
    test_enter_binds_and_return_releases();
    test_alias_violation_is_rejected();
    test_may_alias_shares_storage();
    test_nested_calls_keep_outer_addresses();
    test_literal_actual_is_inline();
    test_static_actual_is_bound_by_inverse();
    test_return_restores_frame_for_reuse();
    test_slot_capacity_includes_symbols();
    test_call_exit_reports_resume_position();
    test_repeated_calls_on_same_frame();
    test_call_failure_shapes();
    if (failures != 0) {
        fprintf(stderr, "vmbin_call_test: %d failure(s)\n", failures);
        return 1;
    }
    return 0;
}
