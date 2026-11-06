/*
 * vmbin 执行入口：core 规则、按地址数据面、完成边界与失败形状。
 * 值用测试自己持有的 XValue 形状缓冲模拟（值头 8 字节 + 值体），不经过任何
 * 存储层——阶段 1 的执行模型就是按 shm 地址直接读写。
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
#include "kvspace/xvalue_head.h"
#include "binrwir/registry.h"
#include "vmbin/core_operations.h"
#include "vmbin/interpreter.h"
#include "vmbin/profile.h"
#include "vmbin/value_access.h"
#include "vmkv/rwir_manifest.h"

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
#define TEST_COMMITTED_FLAG \
    ((uint16_t)BINRWIR_EXECUTION_EXIT_FLAG_COMPLETED_STATE_COMMITTED)
#define TEST_COPY_OPERATION VMBIN_OPERATION_IDENTIFIER_COPY

/* ---------------- 测试用 XValue：值头 + 值体，按地址读写 ---------------- */

typedef struct test_value {
    uint8_t storage[KVSPACE_XVALUE_HEAD_LEN + TEST_VALUE_CAPACITY];
    size_t body_size;
} test_value_t;

static void test_value_initialize(test_value_t *value, size_t body_size) {
    kvspaceXvalueHead head;

    memset(&head, 0, sizeof(head));
    kvspaceXvalueHeadSetLen(&head, body_size);
    head.layout = KVSPACE_XVALUE_LAYOUT_LENGTH;
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

/* ---------------- 分配器：Program builder 用 ---------------- */

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

typedef struct test_instruction {
    binrwir_operation_identifier_t operation_identifier;
    uint32_t source_program_counter;
    binrwir_argument_t reads[4];
    size_t read_count;
    binrwir_argument_t writes[1];
    size_t write_count;
} test_instruction_t;

typedef struct test_program_source {
    const test_parameter_t *parameters;
    size_t parameter_count;
    const char *symbols[8];
    /* 只有被 LOCAL 引用的符号才需要类型；其余留 NULL（取空视图）。 */
    const char *symbol_types[8];
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
    /*
     * 字面量 wire 的落地缓冲。必须活到 `finalize`——`add_literal` 借用这段
     * 字节直到那时，用函数内的自动变量会在循环下一轮就被覆盖。
     * 上限取 source 的容量（8 条）。
     */
    static uint8_t literal_wires[8][KVSPACE_XVALUE_HEAD_LEN + TEST_VALUE_CAPACITY];
    binrwir_program_identity_t identity;
    binrwir_program_builder_t *builder = NULL;
    uint16_t symbol_index = BINRWIR_SYMBOL_INDEX_NONE;
    uint32_t literal_index = 0U;
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
                        ordinal < source->symbol_count;
         ++ordinal) {
        const runtimec_string_view_t symbol_type =
            source->symbol_types[ordinal] != NULL
                ? runtimec_string_view_from_c_string(
                      source->symbol_types[ordinal])
                : runtimec_string_view_make(NULL, 0U);

        status = binrwir_program_builder_add_symbol(
            builder,
            runtimec_string_view_from_c_string(source->symbols[ordinal]),
            symbol_type, &symbol_index);
    }
    for (ordinal = 0U; status == RUNTIMEC_STATUS_SUCCESS &&
                        ordinal < source->literal_count;
         ++ordinal) {
        /*
         * `add_literal` 收的是**完整 wire**（XValue head + body + attr），
         * 而本测试的 `literal_bytes` 是**裸 body**——过去直接把 body 当 wire
         * 传，能跑只是因为读取端当时也按原样返回、写入端按等长写，两个错误
         * 恰好抵消。
         *
         * 现在在登记处补头：body 原样搬进 wire，长度按真实 body 长度写进 head。
         * 这样每个用例的 `literal_bytes` 仍然只写值，不必各自手搓 16 字节头。
         */
        uint8_t *literal_wire = literal_wires[ordinal];
        kvspaceXvalueHead head;

        if (source->literal_sizes[ordinal] > TEST_VALUE_CAPACITY) {
            status = RUNTIMEC_STATUS_INVALID;
            break;
        }
        memset(&head, 0, sizeof(head));
        kvspaceXvalueHeadSetLen(&head, source->literal_sizes[ordinal]);
        head.layout = KVSPACE_XVALUE_LAYOUT_LENGTH;
        memcpy(literal_wire, &head, KVSPACE_XVALUE_HEAD_LEN);
        memcpy(literal_wire + KVSPACE_XVALUE_HEAD_LEN,
               source->literal_bytes[ordinal], source->literal_sizes[ordinal]);
        status = binrwir_program_builder_add_literal(
            builder,
            runtimec_bytes_view_make(
                literal_wire,
                KVSPACE_XVALUE_HEAD_LEN + source->literal_sizes[ordinal]),
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

static runtimec_status_t test_frame_initialize(
    binrwir_execution_frame_t *frame,
    binrwir_frame_bindings_t **output_bindings,
    const binrwir_program_t *program, const runtimec_allocator_t *allocator) {
    runtimec_status_t status = binrwir_execution_frame_initialize(
        frame, program, NULL, 1U, 2U, 0U);

    *output_bindings = NULL;
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = binrwir_frame_bindings_create(allocator, 4U, output_bindings);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    frame->bindings = *output_bindings;
    return RUNTIMEC_STATUS_SUCCESS;
}

/* ---------------- 用例 ---------------- */

/* opid 常量必须与 catalog 一致：catalog 是唯一权威，这里是它的镜像。 */
static void test_core_operation_identifiers_match_catalog(void) {
    struct {
        const char *name;
        uint32_t identifier;
    } cases[] = {
        {"=", VMBIN_OPERATION_IDENTIFIER_COPY},
        {"call", VMBIN_OPERATION_IDENTIFIER_CALL},
        {"return", VMBIN_OPERATION_IDENTIFIER_RETURN},
        {"goto", VMBIN_OPERATION_IDENTIFIER_GOTO},
        {"br", VMBIN_OPERATION_IDENTIFIER_BRANCH},
    };

    for (size_t ordinal = 0U; ordinal < sizeof(cases) / sizeof(cases[0]);
         ++ordinal) {
        const vmkv_rwir_capability_t *capability =
            vmkv_rwir_manifest_find_operation(
                runtimec_string_view_from_c_string(cases[ordinal].name));

        RUNTIMEC_EXPECT(capability != NULL);
        if (capability != NULL) {
            RUNTIMEC_EXPECT(capability->operation_identifier ==
                            cases[ordinal].identifier);
        }
    }
}

/* copy：从字面量读到静态值体地址，值体被就地改写。 */
static void test_copy_writes_into_body(void) {
    test_allocator_context_t counters = {0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    test_value_t destination;
    binrwir_program_t *program = NULL;
    binrwir_execution_frame_t frame;
    binrwir_frame_bindings_t *bindings = NULL;
    binrwir_execution_exit_t exit_state;
    binrwir_registry_t registry;
    uint8_t literal[8];
    binrwir_argument_t read_argument;
    binrwir_argument_t write_argument;
    test_instruction_t instruction;
    test_program_source_t source;

    test_value_initialize(&destination, sizeof(int64_t));
    test_int64_bytes(42, literal);
    RUNTIMEC_EXPECT(binrwir_argument_create(1U, BINRWIR_REFERENCE_LITERAL, 0U,
                                            0U, &read_argument) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_argument_create(2U, BINRWIR_REFERENCE_STATIC, 0U,
                                            test_value_body_address(&destination),
                                            &write_argument) ==
                    RUNTIMEC_STATUS_SUCCESS);
    memset(&instruction, 0, sizeof(instruction));
    instruction.operation_identifier = VMBIN_OPERATION_IDENTIFIER_COPY;
    instruction.source_program_counter = 1U;
    instruction.reads[0] = read_argument;
    instruction.read_count = 1U;
    instruction.writes[0] = write_argument;
    instruction.write_count = 1U;
    memset(&source, 0, sizeof(source));
    source.symbols[0] = "/t/a";
    source.symbol_count = 1U;
    source.literal_types[0] = "int64";
    source.literal_bytes[0] = literal;
    source.literal_sizes[0] = sizeof(literal);
    source.literal_count = 1U;
    source.instructions = &instruction;
    source.instruction_count = 1U;

    RUNTIMEC_EXPECT(test_program_build(&allocator, &source, &program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(test_frame_initialize(&frame, &bindings, program,
                                          &allocator) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_registry_initialize(&registry) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_execute(program, NULL, &frame, &registry, NULL, NULL,
                                  &exit_state) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(exit_state.kind == BINRWIR_EXECUTION_EXIT_DONE);
    RUNTIMEC_EXPECT((exit_state.flags & TEST_COMMITTED_FLAG) != 0U);
    RUNTIMEC_EXPECT(test_value_int64(&destination) == 42);

    RUNTIMEC_EXPECT(binrwir_frame_bindings_destroy(bindings) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_execution_frame_destroy(&frame) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_destroy(program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

/* 参数引用：帧绑定给 XValue 基址，值体由合同的推导函数定位。 */
static void test_parameter_read_uses_binding_address(void) {
    test_allocator_context_t counters = {0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    test_value_t parameter_value;
    test_value_t destination;
    binrwir_program_t *program = NULL;
    binrwir_execution_frame_t frame;
    binrwir_frame_bindings_t *bindings = NULL;
    binrwir_execution_exit_t exit_state;
    binrwir_registry_t registry;
    binrwir_argument_t read_argument;
    binrwir_argument_t write_argument;
    test_parameter_t parameters[1];
    test_instruction_t instruction;
    test_program_source_t source;

    test_value_initialize(&parameter_value, sizeof(int64_t));
    test_value_set_int64(&parameter_value, 7);
    test_value_initialize(&destination, sizeof(int64_t));
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        1U, BINRWIR_REFERENCE_PARAMETER_READ, 0U, 0U,
                        &read_argument) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_argument_create(2U, BINRWIR_REFERENCE_STATIC, 0U,
                                            test_value_body_address(&destination),
                                            &write_argument) ==
                    RUNTIMEC_STATUS_SUCCESS);
    parameters[0].direction = BINRWIR_PARAMETER_DIRECTION_READ;
    parameters[0].argument_index = 1U;
    parameters[0].type_name = "int64";
    memset(&instruction, 0, sizeof(instruction));
    instruction.operation_identifier = VMBIN_OPERATION_IDENTIFIER_COPY;
    instruction.source_program_counter = 1U;
    instruction.reads[0] = read_argument;
    instruction.read_count = 1U;
    instruction.writes[0] = write_argument;
    instruction.write_count = 1U;
    memset(&source, 0, sizeof(source));
    source.parameters = parameters;
    source.parameter_count = 1U;
    source.symbols[0] = "/t/destination";
    source.symbol_count = 1U;
    source.instructions = &instruction;
    source.instruction_count = 1U;

    RUNTIMEC_EXPECT(test_program_build(&allocator, &source, &program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(test_frame_initialize(&frame, &bindings, program,
                                          &allocator) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_frame_bindings_bind(
                        bindings, 0U,
                        runtimec_key_view_from_c_string("/vthread/1/[d]/[0,-1]"),
                        runtimec_key_view_from_c_string("/t/parameter"),
                        test_value_xvalue_address(&parameter_value), 1U) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_registry_initialize(&registry) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_execute(program, NULL, &frame, &registry, NULL, NULL,
                                  &exit_state) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(exit_state.kind == BINRWIR_EXECUTION_EXIT_DONE);
    RUNTIMEC_EXPECT(test_value_int64(&destination) == 7);

    RUNTIMEC_EXPECT(binrwir_frame_bindings_destroy(bindings) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_execution_frame_destroy(&frame) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_destroy(program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

static void test_goto_skips_instruction(void) {
    test_allocator_context_t counters = {0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    test_value_t skipped;
    test_value_t done;
    binrwir_program_t *program = NULL;
    binrwir_execution_frame_t frame;
    binrwir_frame_bindings_t *bindings = NULL;
    binrwir_execution_exit_t exit_state;
    binrwir_registry_t registry;
    uint8_t target_literal[8];
    uint8_t value_literal[8];
    binrwir_argument_t target_argument;
    binrwir_argument_t read_argument;
    binrwir_argument_t skipped_write;
    binrwir_argument_t done_write;
    test_instruction_t instructions[3];
    test_program_source_t source;

    test_value_initialize(&skipped, sizeof(int64_t));
    test_value_initialize(&done, sizeof(int64_t));
    test_int64_bytes(3, target_literal);
    test_int64_bytes(7, value_literal);
    RUNTIMEC_EXPECT(binrwir_argument_create(1U, BINRWIR_REFERENCE_LITERAL, 0U,
                                            0U, &target_argument) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_argument_create(2U, BINRWIR_REFERENCE_LITERAL, 0U,
                                            1U, &read_argument) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_argument_create(3U, BINRWIR_REFERENCE_STATIC, 0U,
                                            test_value_body_address(&skipped),
                                            &skipped_write) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_argument_create(4U, BINRWIR_REFERENCE_STATIC, 1U,
                                            test_value_body_address(&done),
                                            &done_write) ==
                    RUNTIMEC_STATUS_SUCCESS);
    memset(instructions, 0, sizeof(instructions));
    instructions[0].operation_identifier = VMBIN_OPERATION_IDENTIFIER_GOTO;
    instructions[0].source_program_counter = 1U;
    instructions[0].reads[0] = target_argument;
    instructions[0].read_count = 1U;
    instructions[1].operation_identifier = VMBIN_OPERATION_IDENTIFIER_COPY;
    instructions[1].source_program_counter = 2U;
    instructions[1].reads[0] = read_argument;
    instructions[1].read_count = 1U;
    instructions[1].writes[0] = skipped_write;
    instructions[1].write_count = 1U;
    instructions[2].operation_identifier = VMBIN_OPERATION_IDENTIFIER_COPY;
    instructions[2].source_program_counter = 3U;
    instructions[2].reads[0] = read_argument;
    instructions[2].read_count = 1U;
    instructions[2].writes[0] = done_write;
    instructions[2].write_count = 1U;
    memset(&source, 0, sizeof(source));
    source.symbols[0] = "/t/skipped";
    source.symbols[1] = "/t/done";
    source.symbol_count = 2U;
    source.literal_types[0] = "int64";
    source.literal_bytes[0] = target_literal;
    source.literal_sizes[0] = sizeof(target_literal);
    source.literal_types[1] = "int64";
    source.literal_bytes[1] = value_literal;
    source.literal_sizes[1] = sizeof(value_literal);
    source.literal_count = 2U;
    source.instructions = instructions;
    source.instruction_count = 3U;

    RUNTIMEC_EXPECT(test_program_build(&allocator, &source, &program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(test_frame_initialize(&frame, &bindings, program,
                                          &allocator) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_registry_initialize(&registry) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_execute(program, NULL, &frame, &registry, NULL, NULL,
                                  &exit_state) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(exit_state.kind == BINRWIR_EXECUTION_EXIT_DONE);
    RUNTIMEC_EXPECT(test_value_int64(&done) == 7);
    RUNTIMEC_EXPECT(test_value_int64(&skipped) == 0);

    RUNTIMEC_EXPECT(binrwir_frame_bindings_destroy(bindings) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_execution_frame_destroy(&frame) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_destroy(program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

/*
 * br(cond, then, else)：cond 非零走 then，为零走 else。then 块以 goto(merge) 收尾，
 * 否则会顺序落进 else 块——这也是 layout 保证「每块以终结符收尾」的原因。
 */
static void test_branch_selects_target(int64_t condition) {
    test_allocator_context_t counters = {0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    test_value_t then_value;
    test_value_t else_value;
    binrwir_program_t *program = NULL;
    binrwir_execution_frame_t frame;
    binrwir_frame_bindings_t *bindings = NULL;
    binrwir_execution_exit_t exit_state;
    binrwir_registry_t registry;
    uint8_t literal_bytes[5][8];
    binrwir_argument_t reads[3];
    binrwir_argument_t then_write;
    binrwir_argument_t else_write;
    test_instruction_t instructions[5];
    test_program_source_t source;

    test_value_initialize(&then_value, sizeof(int64_t));
    test_value_initialize(&else_value, sizeof(int64_t));
    test_int64_bytes(condition, literal_bytes[0]);
    test_int64_bytes(2, literal_bytes[1]);
    test_int64_bytes(4, literal_bytes[2]);
    test_int64_bytes(9, literal_bytes[3]);
    test_int64_bytes(5, literal_bytes[4]);
    RUNTIMEC_EXPECT(binrwir_argument_create(1U, BINRWIR_REFERENCE_LITERAL, 0U,
                                            0U, &reads[0]) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_argument_create(2U, BINRWIR_REFERENCE_LITERAL, 0U,
                                            1U, &reads[1]) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_argument_create(3U, BINRWIR_REFERENCE_LITERAL, 0U,
                                            2U, &reads[2]) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_argument_create(4U, BINRWIR_REFERENCE_STATIC, 0U,
                                            test_value_body_address(&then_value),
                                            &then_write) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_argument_create(5U, BINRWIR_REFERENCE_STATIC, 1U,
                                            test_value_body_address(&else_value),
                                            &else_write) ==
                    RUNTIMEC_STATUS_SUCCESS);
    memset(instructions, 0, sizeof(instructions));
    instructions[0].operation_identifier = VMBIN_OPERATION_IDENTIFIER_BRANCH;
    instructions[0].source_program_counter = 1U;
    memcpy(instructions[0].reads, reads, sizeof(reads));
    instructions[0].read_count = 3U;
    instructions[1].operation_identifier = VMBIN_OPERATION_IDENTIFIER_COPY;
    instructions[1].source_program_counter = 2U;
    RUNTIMEC_EXPECT(binrwir_argument_create(6U, BINRWIR_REFERENCE_LITERAL, 0U,
                                            3U, &instructions[1].reads[0]) ==
                    RUNTIMEC_STATUS_SUCCESS);
    instructions[1].read_count = 1U;
    instructions[1].writes[0] = then_write;
    instructions[1].write_count = 1U;
    instructions[2].operation_identifier = VMBIN_OPERATION_IDENTIFIER_GOTO;
    instructions[2].source_program_counter = 3U;
    RUNTIMEC_EXPECT(binrwir_argument_create(7U, BINRWIR_REFERENCE_LITERAL, 0U,
                                            4U, &instructions[2].reads[0]) ==
                    RUNTIMEC_STATUS_SUCCESS);
    instructions[2].read_count = 1U;
    instructions[3].operation_identifier = VMBIN_OPERATION_IDENTIFIER_COPY;
    instructions[3].source_program_counter = 4U;
    instructions[3].reads[0] = instructions[1].reads[0];
    instructions[3].read_count = 1U;
    instructions[3].writes[0] = else_write;
    instructions[3].write_count = 1U;
    instructions[4].operation_identifier = VMBIN_OPERATION_IDENTIFIER_RETURN;
    instructions[4].source_program_counter = 5U;
    memset(&source, 0, sizeof(source));
    source.symbols[0] = "/t/then";
    source.symbols[1] = "/t/else";
    source.symbol_count = 2U;
    for (size_t ordinal = 0U; ordinal < 5U; ++ordinal) {
        source.literal_types[ordinal] = "int64";
        source.literal_bytes[ordinal] = literal_bytes[ordinal];
        source.literal_sizes[ordinal] = sizeof(literal_bytes[ordinal]);
    }
    source.literal_count = 5U;
    source.instructions = instructions;
    source.instruction_count = 5U;

    RUNTIMEC_EXPECT(test_program_build(&allocator, &source, &program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(test_frame_initialize(&frame, &bindings, program,
                                          &allocator) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_registry_initialize(&registry) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_execute(program, NULL, &frame, &registry, NULL, NULL,
                                  &exit_state) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(exit_state.kind == BINRWIR_EXECUTION_EXIT_DONE);
    RUNTIMEC_EXPECT(exit_state.logical_program_counter == 5U);
    if (condition != 0) {
        RUNTIMEC_EXPECT(test_value_int64(&then_value) == 9);
        RUNTIMEC_EXPECT(test_value_int64(&else_value) == 0);
    } else {
        RUNTIMEC_EXPECT(test_value_int64(&then_value) == 0);
        RUNTIMEC_EXPECT(test_value_int64(&else_value) == 9);
    }

    RUNTIMEC_EXPECT(binrwir_frame_bindings_destroy(bindings) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_execution_frame_destroy(&frame) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_destroy(program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

static void test_call_reports_target_symbol(void) {
    test_allocator_context_t counters = {0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    binrwir_program_t *program = NULL;
    binrwir_execution_frame_t frame;
    binrwir_frame_bindings_t *bindings = NULL;
    binrwir_execution_exit_t exit_state;
    binrwir_registry_t registry;
    binrwir_argument_t function_argument;
    test_instruction_t instruction;
    test_program_source_t source;

    RUNTIMEC_EXPECT(binrwir_argument_create(1U, BINRWIR_REFERENCE_FUNCTION, 0U,
                                            0U, &function_argument) ==
                    RUNTIMEC_STATUS_SUCCESS);
    memset(&instruction, 0, sizeof(instruction));
    instruction.operation_identifier = VMBIN_OPERATION_IDENTIFIER_CALL;
    instruction.source_program_counter = 1U;
    instruction.reads[0] = function_argument;
    instruction.read_count = 1U;
    memset(&source, 0, sizeof(source));
    source.symbols[0] = "target.function";
    source.symbol_count = 1U;
    source.instructions = &instruction;
    source.instruction_count = 1U;

    RUNTIMEC_EXPECT(test_program_build(&allocator, &source, &program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(test_frame_initialize(&frame, &bindings, program,
                                          &allocator) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_registry_initialize(&registry) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_execute(program, NULL, &frame, &registry, NULL, NULL,
                                  &exit_state) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(exit_state.kind == BINRWIR_EXECUTION_EXIT_CALL);
    RUNTIMEC_EXPECT(exit_state.target_identifier == 0U);
    RUNTIMEC_EXPECT((exit_state.flags & TEST_COMMITTED_FLAG) != 0U);
    {
        runtimec_string_view_t name = runtimec_string_view_make(NULL, 0U);

        runtimec_string_view_t symbol_type =
            runtimec_string_view_make(NULL, 0U);

        RUNTIMEC_EXPECT(binrwir_program_get_symbol(
                            program, (uint16_t)exit_state.target_identifier,
                            &name, &symbol_type) == RUNTIMEC_STATUS_SUCCESS);
        RUNTIMEC_EXPECT(memcmp(name.data, "target.function", name.size) == 0);
    }

    RUNTIMEC_EXPECT(binrwir_frame_bindings_destroy(bindings) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_execution_frame_destroy(&frame) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_destroy(program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

static void test_return_completes_frame(void) {
    test_allocator_context_t counters = {0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    binrwir_program_t *program = NULL;
    binrwir_execution_frame_t frame;
    binrwir_frame_bindings_t *bindings = NULL;
    binrwir_execution_exit_t exit_state;
    binrwir_registry_t registry;
    test_instruction_t instruction;
    test_program_source_t source;

    memset(&instruction, 0, sizeof(instruction));
    instruction.operation_identifier = VMBIN_OPERATION_IDENTIFIER_RETURN;
    instruction.source_program_counter = 4U;
    memset(&source, 0, sizeof(source));
    source.instructions = &instruction;
    source.instruction_count = 1U;

    RUNTIMEC_EXPECT(test_program_build(&allocator, &source, &program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(test_frame_initialize(&frame, &bindings, program,
                                          &allocator) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_registry_initialize(&registry) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_execute(program, NULL, &frame, &registry, NULL, NULL,
                                  &exit_state) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(exit_state.kind == BINRWIR_EXECUTION_EXIT_DONE);
    RUNTIMEC_EXPECT(exit_state.logical_program_counter == 4U);
    RUNTIMEC_EXPECT(exit_state.resume_logical_program_counter ==
                    BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE);

    RUNTIMEC_EXPECT(binrwir_frame_bindings_destroy(bindings) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_execution_frame_destroy(&frame) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_destroy(program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

/* add：读两个 int64、写一个 int64；handler 走与 core 相同的地址数据面。 */
static runtimec_status_t test_add_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    runtimec_error_t error;
    runtimec_bytes_buffer_t buffer;
    uint8_t storage[16];
    int64_t left = 0;
    int64_t right = 0;
    int64_t sum = 0;

    (void)output_exit;
    runtimec_error_reset(&error);
    if (instruction->read_arguments.count != 2U ||
        instruction->write_arguments.count != 1U) {
        return RUNTIMEC_STATUS_INVALID;
    }
    /*
     * 算术只吃值（body），不吃整条 XValue：用 `_body` 三条。此前用 wire 版，
     * 对字面量它会返回整条 wire（16 字节头 + body），于是 `buffer.size` 断言
     * 依赖了「字面量恰好是裸 8 字节」这个**与契约相反**的形状。
     */
    buffer = (runtimec_bytes_buffer_t){storage, 0U, sizeof(storage)};
    if (vmbin_value_access_read_body(
            frame, &instruction->read_arguments.data[0], &buffer,
            &error) != RUNTIMEC_STATUS_SUCCESS) {
        return error.status;
    }
    RUNTIMEC_EXPECT(buffer.size == 8U);
    memcpy(&left, storage, sizeof(left));
    buffer = (runtimec_bytes_buffer_t){storage, 0U, sizeof(storage)};
    if (vmbin_value_access_read_body(
            frame, &instruction->read_arguments.data[1], &buffer,
            &error) != RUNTIMEC_STATUS_SUCCESS) {
        return error.status;
    }
    RUNTIMEC_EXPECT(buffer.size == 8U);
    memcpy(&right, storage, sizeof(right));
    sum = left + right;
    return vmbin_value_access_write_body(
        frame, &instruction->write_arguments.data[0],
        runtimec_bytes_view_make(&sum, sizeof(sum)), &error);
}

static void test_add_builtin_handler(void) {
    test_allocator_context_t counters = {0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    test_value_t destination;
    binrwir_program_t *program = NULL;
    binrwir_execution_frame_t frame;
    binrwir_frame_bindings_t *bindings = NULL;
    binrwir_execution_exit_t exit_state;
    binrwir_registry_t registry;
    binrwir_registry_entry_t entry;
    uint8_t left_literal[8];
    uint8_t right_literal[8];
    binrwir_argument_t reads[2];
    binrwir_argument_t write_argument;
    test_instruction_t instruction;
    test_program_source_t source;

    test_value_initialize(&destination, sizeof(int64_t));
    test_int64_bytes(40, left_literal);
    test_int64_bytes(2, right_literal);
    RUNTIMEC_EXPECT(binrwir_argument_create(1U, BINRWIR_REFERENCE_LITERAL, 0U,
                                            0U, &reads[0]) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_argument_create(2U, BINRWIR_REFERENCE_LITERAL, 0U,
                                            1U, &reads[1]) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_argument_create(3U, BINRWIR_REFERENCE_STATIC, 0U,
                                            test_value_body_address(&destination),
                                            &write_argument) ==
                    RUNTIMEC_STATUS_SUCCESS);
    memset(&instruction, 0, sizeof(instruction));
    instruction.operation_identifier = 0x0002U; /* catalog: add */
    instruction.source_program_counter = 1U;
    memcpy(instruction.reads, reads, sizeof(reads));
    instruction.read_count = 2U;
    instruction.writes[0] = write_argument;
    instruction.write_count = 1U;
    memset(&source, 0, sizeof(source));
    source.symbols[0] = "/t/sum";
    source.symbol_count = 1U;
    source.literal_types[0] = "int64";
    source.literal_bytes[0] = left_literal;
    source.literal_sizes[0] = sizeof(left_literal);
    source.literal_types[1] = "int64";
    source.literal_bytes[1] = right_literal;
    source.literal_sizes[1] = sizeof(right_literal);
    source.literal_count = 2U;
    source.instructions = &instruction;
    source.instruction_count = 1U;

    RUNTIMEC_EXPECT(test_program_build(&allocator, &source, &program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(test_frame_initialize(&frame, &bindings, program,
                                          &allocator) ==
                    RUNTIMEC_STATUS_SUCCESS);
    entry.operation_identifier = instruction.operation_identifier;
    entry.flags = BINRWIR_REGISTRY_FLAG_NONE;
    entry.handler = test_add_handler;
    RUNTIMEC_EXPECT(binrwir_registry_initialize(&registry) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_registry_append(&registry, "test", &entry,
                                                  1U, NULL) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_execute(program, NULL, &frame, &registry, NULL, NULL,
                                  &exit_state) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(exit_state.kind == BINRWIR_EXECUTION_EXIT_DONE);
    RUNTIMEC_EXPECT(test_value_int64(&destination) == 42);

    RUNTIMEC_EXPECT(binrwir_frame_bindings_destroy(bindings) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_execution_frame_destroy(&frame) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_destroy(program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

/*
 * Program 里出现 registry 没有的 opid = 编译期与执行期的表不一致 = 内部不一致，
 * 报 INVALID。外部能力不走这条路，它编成 FOREIGN 标记。
 */
static void test_unknown_operation_is_internal_inconsistency(void) {
    test_allocator_context_t counters = {0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    binrwir_program_t *program = NULL;
    binrwir_execution_frame_t frame;
    binrwir_frame_bindings_t *bindings = NULL;
    binrwir_execution_exit_t exit_state;
    binrwir_registry_t registry;
    test_instruction_t instruction;
    test_program_source_t source;

    memset(&instruction, 0, sizeof(instruction));
    instruction.operation_identifier = 0x00F0U; /* registry 里没有 */
    instruction.source_program_counter = 6U;
    memset(&source, 0, sizeof(source));
    source.instructions = &instruction;
    source.instruction_count = 1U;

    RUNTIMEC_EXPECT(test_program_build(&allocator, &source, &program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(test_frame_initialize(&frame, &bindings, program,
                                          &allocator) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_registry_initialize(&registry) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_execute(program, NULL, &frame, &registry, NULL, NULL,
                                  &exit_state) == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(exit_state.kind == BINRWIR_EXECUTION_EXIT_ERROR);
    RUNTIMEC_EXPECT(exit_state.status == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT((exit_state.flags & TEST_COMMITTED_FLAG) == 0U);
    RUNTIMEC_EXPECT(frame.logical_program_counter == 0U);

    RUNTIMEC_EXPECT(binrwir_frame_bindings_destroy(bindings) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_execution_frame_destroy(&frame) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_destroy(program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

/* FOREIGN 标记：不执行、不写、不推进游标，出口给出标记的 source pc。 */
static void test_foreign_marker_hands_off(void) {
    test_allocator_context_t counters = {0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    test_value_t destination;
    binrwir_program_t *program = NULL;
    binrwir_execution_frame_t frame;
    binrwir_frame_bindings_t *bindings = NULL;
    binrwir_execution_exit_t exit_state;
    binrwir_registry_t registry;
    uint8_t literal[8];
    binrwir_argument_t write_argument;
    test_instruction_t instructions[3];
    test_program_source_t source;

    test_value_initialize(&destination, sizeof(int64_t));
    test_int64_bytes(7, literal);
    /* 写位置在标记之后，必须一个字节都没被写。 */
    RUNTIMEC_EXPECT(binrwir_argument_create(1U, BINRWIR_REFERENCE_STATIC, 0U,
                                            test_value_body_address(&destination),
                                            &write_argument) ==
                    RUNTIMEC_STATUS_SUCCESS);
    memset(instructions, 0, sizeof(instructions));
    instructions[0].operation_identifier = TEST_COPY_OPERATION;
    instructions[0].source_program_counter = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_create(2U, BINRWIR_REFERENCE_LITERAL, 0U,
                                            0U, &instructions[0].reads[0]) ==
                    RUNTIMEC_STATUS_SUCCESS);
    instructions[0].read_count = 1U;
    instructions[0].writes[0] = write_argument;
    instructions[0].write_count = 1U;
    /* [1] 外来标记，source pc 42。 */
    instructions[1].operation_identifier = BINRWIR_OPERATION_IDENTIFIER_FOREIGN;
    instructions[1].source_program_counter = 42U;
    /* [2] 标记之后：执行到这里就说明标记没起作用。 */
    instructions[2].operation_identifier = TEST_COPY_OPERATION;
    instructions[2].source_program_counter = 43U;
    instructions[2].reads[0] = instructions[0].reads[0];
    instructions[2].read_count = 1U;
    instructions[2].writes[0] = write_argument;
    instructions[2].write_count = 1U;
    memset(&source, 0, sizeof(source));
    source.symbols[0] = "/t/foreign";
    source.symbol_count = 1U;
    source.literal_types[0] = "int64";
    source.literal_bytes[0] = literal;
    source.literal_sizes[0] = sizeof(literal);
    source.literal_count = 1U;
    source.instructions = instructions;
    source.instruction_count = 3U;

    RUNTIMEC_EXPECT(test_program_build(&allocator, &source, &program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(test_frame_initialize(&frame, &bindings, program,
                                          &allocator) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_registry_initialize(&registry) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_execute(program, NULL, &frame, &registry, NULL, NULL,
                                  &exit_state) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(exit_state.kind == BINRWIR_EXECUTION_EXIT_HANDOFF);
    RUNTIMEC_EXPECT(exit_state.status == RUNTIMEC_STATUS_UNSUPPORTED);
    /* 出口位置是标记自己的 source pc。 */
    RUNTIMEC_EXPECT(exit_state.logical_program_counter == 42U);
    RUNTIMEC_EXPECT(exit_state.resume_logical_program_counter == 42U);
    /* 标记一个字节都没写：既有提交标志不能置。 */
    RUNTIMEC_EXPECT((exit_state.flags & TEST_COMMITTED_FLAG) == 0U);
    /* 标记之后的指令没有执行，值仍是最初的 0。 */
    RUNTIMEC_EXPECT(frame.logical_program_counter != 3U);

    RUNTIMEC_EXPECT(binrwir_frame_bindings_destroy(bindings) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_execution_frame_destroy(&frame) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_destroy(program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

/*
 * opid 0 不是「未知算子」，是非法指令头——**在写侧就被拒**。
 *
 * 这条用例原来断言的是「Program 能建成、执行到它才失败」，那是把「写侧放行、
 * 读侧才拦」的错误行为钉成了规格。0 是 `memset(0)` 的默认值，漏填就会进来；
 * 让它通过校验，等于把编码期的 bug 推迟到执行期才暴露，那时已经离开了出错点。
 * 现在判据在 `binrwir_instruction_validate_build_request`，本用例守它。
 */
static void test_zero_operation_identifier_is_invalid(void) {
    test_allocator_context_t counters = {0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    binrwir_program_t *program = NULL;
    test_instruction_t instruction;
    test_program_source_t source;

    memset(&instruction, 0, sizeof(instruction));
    instruction.operation_identifier = 0U;
    instruction.source_program_counter = 1U;
    memset(&source, 0, sizeof(source));
    source.instructions = &instruction;
    source.instruction_count = 1U;

    /* 构建期拒绝：不产生 Program，也不分配任何内存。 */
    RUNTIMEC_EXPECT(test_program_build(&allocator, &source, &program) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

/* 绑定表里没有这个槽：解析失败即 INVALID，不猜地址。 */
static void test_unbound_parameter_is_invalid(void) {
    test_allocator_context_t counters = {0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    binrwir_program_t *program = NULL;
    binrwir_execution_frame_t frame;
    binrwir_frame_bindings_t *bindings = NULL;
    binrwir_execution_exit_t exit_state;
    binrwir_registry_t registry;
    binrwir_argument_t read_argument;
    binrwir_argument_t write_argument;
    test_parameter_t parameters[2];
    test_instruction_t instruction;
    test_program_source_t source;

    RUNTIMEC_EXPECT(binrwir_argument_create(
                        1U, BINRWIR_REFERENCE_PARAMETER_READ, 0U, 0U,
                        &read_argument) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        2U, BINRWIR_REFERENCE_PARAMETER_WRITE, 0U, 0U,
                        &write_argument) == RUNTIMEC_STATUS_SUCCESS);
    parameters[0].direction = BINRWIR_PARAMETER_DIRECTION_READ;
    parameters[0].argument_index = 1U;
    parameters[0].type_name = "int64";
    parameters[1].direction = BINRWIR_PARAMETER_DIRECTION_WRITE;
    parameters[1].argument_index = 2U;
    parameters[1].type_name = "int64";
    memset(&instruction, 0, sizeof(instruction));
    instruction.operation_identifier = VMBIN_OPERATION_IDENTIFIER_COPY;
    instruction.source_program_counter = 1U;
    instruction.reads[0] = read_argument;
    instruction.read_count = 1U;
    instruction.writes[0] = write_argument;
    instruction.write_count = 1U;
    memset(&source, 0, sizeof(source));
    source.parameters = parameters;
    source.parameter_count = 2U;
    source.instructions = &instruction;
    source.instruction_count = 1U;

    RUNTIMEC_EXPECT(test_program_build(&allocator, &source, &program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(test_frame_initialize(&frame, &bindings, program,
                                          &allocator) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_registry_initialize(&registry) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_execute(program, NULL, &frame, &registry, NULL, NULL,
                                  &exit_state) == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(exit_state.kind == BINRWIR_EXECUTION_EXIT_ERROR);
    RUNTIMEC_EXPECT(exit_state.status == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(frame.logical_program_counter == 0U);

    RUNTIMEC_EXPECT(binrwir_frame_bindings_destroy(bindings) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_execution_frame_destroy(&frame) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_destroy(program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

/* 字面量不是可写位置：写入必须以 INVALID 失败。 */
static void test_literal_is_not_writable(void) {
    test_allocator_context_t counters = {0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    binrwir_program_t *program = NULL;
    binrwir_execution_frame_t frame;
    binrwir_frame_bindings_t *bindings = NULL;
    binrwir_execution_exit_t exit_state;
    binrwir_registry_t registry;
    uint8_t literal[8];
    binrwir_argument_t read_argument;
    binrwir_argument_t write_argument;
    test_instruction_t instruction;
    test_program_source_t source;

    test_int64_bytes(1, literal);
    RUNTIMEC_EXPECT(binrwir_argument_create(1U, BINRWIR_REFERENCE_LITERAL, 0U,
                                            0U, &read_argument) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_argument_create(2U, BINRWIR_REFERENCE_LITERAL, 0U,
                                            0U, &write_argument) ==
                    RUNTIMEC_STATUS_SUCCESS);
    memset(&instruction, 0, sizeof(instruction));
    instruction.operation_identifier = VMBIN_OPERATION_IDENTIFIER_COPY;
    instruction.source_program_counter = 1U;
    instruction.reads[0] = read_argument;
    instruction.read_count = 1U;
    instruction.writes[0] = write_argument;
    instruction.write_count = 1U;
    memset(&source, 0, sizeof(source));
    source.literal_types[0] = "int64";
    source.literal_bytes[0] = literal;
    source.literal_sizes[0] = sizeof(literal);
    source.literal_count = 1U;
    source.instructions = &instruction;
    source.instruction_count = 1U;

    RUNTIMEC_EXPECT(test_program_build(&allocator, &source, &program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(test_frame_initialize(&frame, &bindings, program,
                                          &allocator) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_registry_initialize(&registry) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_execute(program, NULL, &frame, &registry, NULL, NULL,
                                  &exit_state) == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(exit_state.kind == BINRWIR_EXECUTION_EXIT_ERROR);
    RUNTIMEC_EXPECT(frame.logical_program_counter == 0U);

    RUNTIMEC_EXPECT(binrwir_frame_bindings_destroy(bindings) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_execution_frame_destroy(&frame) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_destroy(program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

/*
 * 长度不一致失败封闭：值体容量就是当前长度，写更长的值必须 ERROR，
 * 不截断、不静默扩容、不回退按 key。
 */
static void test_size_mismatch_is_error(void) {
    test_allocator_context_t counters = {0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    test_value_t destination;
    binrwir_program_t *program = NULL;
    binrwir_execution_frame_t frame;
    binrwir_frame_bindings_t *bindings = NULL;
    binrwir_execution_exit_t exit_state;
    binrwir_registry_t registry;
    uint8_t wide_literal[16];
    binrwir_argument_t read_argument;
    binrwir_argument_t write_argument;
    test_instruction_t instruction;
    test_program_source_t source;

    test_value_initialize(&destination, sizeof(int64_t));
    memset(wide_literal, 0x5a, sizeof(wide_literal));
    RUNTIMEC_EXPECT(binrwir_argument_create(1U, BINRWIR_REFERENCE_LITERAL, 0U,
                                            0U, &read_argument) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_argument_create(2U, BINRWIR_REFERENCE_STATIC, 0U,
                                            test_value_body_address(&destination),
                                            &write_argument) ==
                    RUNTIMEC_STATUS_SUCCESS);
    memset(&instruction, 0, sizeof(instruction));
    instruction.operation_identifier = VMBIN_OPERATION_IDENTIFIER_COPY;
    instruction.source_program_counter = 1U;
    instruction.reads[0] = read_argument;
    instruction.read_count = 1U;
    instruction.writes[0] = write_argument;
    instruction.write_count = 1U;
    memset(&source, 0, sizeof(source));
    source.symbols[0] = "/t/too_small";
    source.symbol_count = 1U;
    source.literal_types[0] = "[]int64";
    source.literal_bytes[0] = wide_literal;
    source.literal_sizes[0] = sizeof(wide_literal);
    source.literal_count = 1U;
    source.instructions = &instruction;
    source.instruction_count = 1U;

    RUNTIMEC_EXPECT(test_program_build(&allocator, &source, &program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(test_frame_initialize(&frame, &bindings, program,
                                          &allocator) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_registry_initialize(&registry) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_execute(program, NULL, &frame, &registry, NULL, NULL,
                                  &exit_state) == RUNTIMEC_STATUS_ERROR);
    RUNTIMEC_EXPECT(exit_state.kind == BINRWIR_EXECUTION_EXIT_ERROR);
    RUNTIMEC_EXPECT(exit_state.status == RUNTIMEC_STATUS_ERROR);
    RUNTIMEC_EXPECT((exit_state.flags & TEST_COMMITTED_FLAG) == 0U);
    RUNTIMEC_EXPECT(frame.logical_program_counter == 0U);
    /* 值体一个字节都没被改。 */
    RUNTIMEC_EXPECT(test_value_int64(&destination) == 0);

    RUNTIMEC_EXPECT(binrwir_frame_bindings_destroy(bindings) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_execution_frame_destroy(&frame) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_destroy(program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

/* 写已完成但 handler 报错：指令不算完成，游标不推进，不置提交标志。 */
static runtimec_status_t test_failing_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    runtimec_error_t error;
    uint8_t value[8];

    (void)output_exit;
    runtimec_error_reset(&error);
    test_int64_bytes(5, value);
    if (vmbin_value_access_write(
            frame, &instruction->write_arguments.data[0],
            runtimec_bytes_view_make(value, sizeof(value)), &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return error.status;
    }
    return RUNTIMEC_STATUS_ERROR;
}

static void test_handler_failure_keeps_cursor(void) {
    test_allocator_context_t counters = {0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    test_value_t destination;
    binrwir_program_t *program = NULL;
    binrwir_execution_frame_t frame;
    binrwir_frame_bindings_t *bindings = NULL;
    binrwir_execution_exit_t exit_state;
    binrwir_registry_t registry;
    binrwir_registry_entry_t entry;
    binrwir_argument_t write_argument;
    test_instruction_t instruction;
    test_program_source_t source;

    test_value_initialize(&destination, sizeof(int64_t));
    RUNTIMEC_EXPECT(binrwir_argument_create(1U, BINRWIR_REFERENCE_STATIC, 0U,
                                            test_value_body_address(&destination),
                                            &write_argument) ==
                    RUNTIMEC_STATUS_SUCCESS);
    memset(&instruction, 0, sizeof(instruction));
    instruction.operation_identifier = 0x0002U;
    instruction.source_program_counter = 9U;
    instruction.writes[0] = write_argument;
    instruction.write_count = 1U;
    memset(&source, 0, sizeof(source));
    source.symbols[0] = "/t/partial";
    source.symbol_count = 1U;
    source.instructions = &instruction;
    source.instruction_count = 1U;

    RUNTIMEC_EXPECT(test_program_build(&allocator, &source, &program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(test_frame_initialize(&frame, &bindings, program,
                                          &allocator) ==
                    RUNTIMEC_STATUS_SUCCESS);
    entry.operation_identifier = instruction.operation_identifier;
    entry.flags = BINRWIR_REGISTRY_FLAG_EFFECT;
    entry.handler = test_failing_handler;
    RUNTIMEC_EXPECT(binrwir_registry_initialize(&registry) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_registry_append(&registry, "test", &entry,
                                                  1U, NULL) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_execute(program, NULL, &frame, &registry, NULL, NULL,
                                  &exit_state) == RUNTIMEC_STATUS_ERROR);
    RUNTIMEC_EXPECT(exit_state.kind == BINRWIR_EXECUTION_EXIT_ERROR);
    /* 值体确实被改了，但指令没有完成：游标不推进、不置提交标志。 */
    RUNTIMEC_EXPECT(test_value_int64(&destination) == 5);
    RUNTIMEC_EXPECT(frame.logical_program_counter == 0U);
    RUNTIMEC_EXPECT(exit_state.resume_logical_program_counter == 9U);
    RUNTIMEC_EXPECT((exit_state.flags & TEST_COMMITTED_FLAG) == 0U);

    RUNTIMEC_EXPECT(binrwir_frame_bindings_destroy(bindings) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_execution_frame_destroy(&frame) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_destroy(program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

static void test_execute_rejects_invalid_requests(void) {
    binrwir_execution_exit_t exit_state;
    binrwir_registry_t registry;

    RUNTIMEC_EXPECT(binrwir_registry_initialize(&registry) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(vmbin_execute(NULL, NULL, NULL, &registry, NULL, NULL,
                                  &exit_state) == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(exit_state.kind == BINRWIR_EXECUTION_EXIT_ERROR);
}

int main(void) {
    test_core_operation_identifiers_match_catalog();
    test_copy_writes_into_body();
    test_parameter_read_uses_binding_address();
    test_goto_skips_instruction();
    test_branch_selects_target(1);
    test_branch_selects_target(0);
    test_call_reports_target_symbol();
    test_return_completes_frame();
    test_add_builtin_handler();
    test_unknown_operation_is_internal_inconsistency();
    test_foreign_marker_hands_off();
    test_zero_operation_identifier_is_invalid();
    test_unbound_parameter_is_invalid();
    test_literal_is_not_writable();
    test_size_mismatch_is_error();
    test_handler_failure_keeps_cursor();
    test_execute_rejects_invalid_requests();
    if (failures != 0) {
        fprintf(stderr, "vmbin_execute_test: %d failure(s)\n", failures);
        return 1;
    }
    return 0;
}
