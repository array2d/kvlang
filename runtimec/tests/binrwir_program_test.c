/* Program builder, immutability and read-only queries. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "binrwir/program.h"
#include "contract/view.h"

static int failures;

#define RUNTIMEC_EXPECT(condition)                                                \
    do {                                                                          \
        if (!(condition)) {                                                        \
            fprintf(stderr, "%s:%d: expectation failed: %s\n", __FILE__,         \
                    __LINE__, #condition);                                        \
            ++failures;                                                            \
        }                                                                          \
    } while (0)

/* RUNTIMEC_STRING_VIEW_LITERAL 是初始化器形状，不能当函数实参用。 */
static runtimec_string_view_t type_view(const char *text) {
    return runtimec_string_view_make(text, strlen(text));
}

#define TEST_READ_ARGUMENT_INDEX 7U
#define TEST_SECOND_READ_ARGUMENT_INDEX 9U
#define TEST_WRITE_ARGUMENT_INDEX 3U
#define TEST_OPERATION_IDENTIFIER 0x42U
#define TEST_SOURCE_PROGRAM_COUNTER 5U
#define TEST_STATIC_BODY_ADDRESS UINT64_C(0x1008)
/* finalize 总共做十次分配：Program 头、指令头、framing、参数池、四张参数数组、
 * 类型文本、别名规则。逐次制造失败即可覆盖每一条清理路径。 */
#define TEST_FINALIZE_ALLOCATION_COUNT 10U
/* 带符号池、字面量池与两个池块时，定稿是九次分配：Program 头、指令头、framing、
 * 参数池、符号池、字面量池、文本池、字节池、符号排序临时数组。 */
#define TEST_POOL_FINALIZE_ALLOCATION_COUNT 9U

typedef struct test_allocator_context {
    size_t live_allocations;
    bool fail_allocations;
    size_t remaining_successful_allocations;
} test_allocator_context_t;

static void *test_allocate(void *context, size_t size, size_t alignment) {
    test_allocator_context_t *counters = (test_allocator_context_t *)context;
    size_t effective_alignment =
        alignment < sizeof(void *) ? sizeof(void *) : alignment;
    size_t rounded_size = size;
    void *allocation;

    if (counters->fail_allocations) {
        if (counters->remaining_successful_allocations == 0U) {
            return NULL;
        }
        counters->remaining_successful_allocations -= 1U;
    }
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

static binrwir_program_identity_t fixture_identity(void) {
    binrwir_program_identity_t identity = {
        .snapshot_generation = 1U,
        .program_generation = 2U,
        .schema_version = 1U,
        .rwir_manifest_version = 1U,
        .binrwir_binary_interface_generation =
            BINRWIR_BINARY_INTERFACE_GENERATION,
        .canonical_encoding_revision = 1U,
    };

    return identity;
}

/* 两读参（int64、[]char/utf8）、一写参（float64），外加一条 (0,0)=MAY_ALIAS。 */
static runtimec_status_t fixture_register_signature(
    binrwir_program_builder_t *builder) {
    binrwir_alias_rule_t rule = {
        .read_parameter_index = 0U,
        .write_parameter_index = 0U,
        .policy = BINRWIR_ALIAS_MAY_ALIAS,
        .flags = BINRWIR_ALIAS_RULE_FLAG_NONE,
        .reserved_16_bits = 0U,
    };
    uint32_t parameter_index = 0U;
    uint32_t rule_identifier = 0U;
    runtimec_status_t status;

    status = binrwir_program_builder_add_parameter(
        builder, BINRWIR_PARAMETER_DIRECTION_READ, TEST_READ_ARGUMENT_INDEX,
        type_view("int64"), &parameter_index);
    if (status == RUNTIMEC_STATUS_SUCCESS && parameter_index != 0U) {
        status = RUNTIMEC_STATUS_INVALID;
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_program_builder_add_parameter(
            builder, BINRWIR_PARAMETER_DIRECTION_READ,
            TEST_SECOND_READ_ARGUMENT_INDEX,
            type_view("[]char/utf8"), &parameter_index);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS && parameter_index != 1U) {
        status = RUNTIMEC_STATUS_INVALID;
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_program_builder_add_parameter(
            builder, BINRWIR_PARAMETER_DIRECTION_WRITE,
            TEST_WRITE_ARGUMENT_INDEX, type_view("float64"),
            &parameter_index);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS && parameter_index != 0U) {
        status = RUNTIMEC_STATUS_INVALID;
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_program_builder_add_alias_rule(builder, &rule,
                                                        &rule_identifier);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS && rule_identifier != 0U) {
        status = RUNTIMEC_STATUS_INVALID;
    }
    return status;
}

static runtimec_status_t fixture_builder_create(
    const runtimec_allocator_t *allocator,
    binrwir_program_builder_t **output_builder) {
    binrwir_program_identity_t identity = fixture_identity();
    binrwir_program_builder_t *builder = NULL;
    runtimec_status_t status;

    status = binrwir_program_builder_create(allocator, &identity, &builder);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = fixture_register_signature(builder);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        (void)binrwir_program_builder_destroy(builder);
        return status;
    }
    *output_builder = builder;
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 一条指令：opid 0x42、source pc 5，读两个参数（参数引用 + 静态 key），写一个。 */
static runtimec_status_t fixture_add_instruction(
    binrwir_program_builder_t *builder) {
    binrwir_argument_t read_arguments[2];
    binrwir_argument_t write_arguments[1];
    binrwir_instruction_build_request_t request;
    uint32_t instruction_identifier = BINRWIR_IDENTIFIER_NONE;
    runtimec_status_t status;

    status = binrwir_argument_create(TEST_READ_ARGUMENT_INDEX,
                                     BINRWIR_REFERENCE_PARAMETER_READ, 0U, 0U,
                                     &read_arguments[0]);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_argument_create(TEST_STATIC_BODY_ADDRESS,
                                         BINRWIR_REFERENCE_STATIC, 0U,
                                         TEST_STATIC_BODY_ADDRESS,
                                         &read_arguments[1]);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_argument_create(
            TEST_WRITE_ARGUMENT_INDEX, BINRWIR_REFERENCE_PARAMETER_WRITE, 0U,
            0U, &write_arguments[0]);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    memset(&request, 0, sizeof(request));
    request.source_logical_program_counter = TEST_SOURCE_PROGRAM_COUNTER;
    request.operation_identifier = TEST_OPERATION_IDENTIFIER;
    request.read_arguments.data = read_arguments;
    request.read_arguments.count = 2U;
    request.write_arguments.data = write_arguments;
    request.write_arguments.count = 1U;
    if (binrwir_program_builder_add_instruction(builder, &request,
                                                &instruction_identifier) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (instruction_identifier != 0U) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t fixture_program_build(
    const runtimec_allocator_t *allocator,
    binrwir_program_builder_t **output_builder,
    binrwir_program_t **output_program) {
    binrwir_program_builder_t *builder = NULL;
    runtimec_status_t status;

    status = fixture_builder_create(allocator, &builder);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = fixture_add_instruction(builder);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = binrwir_program_builder_finalize(builder, output_program);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        (void)binrwir_program_builder_destroy(builder);
        return status;
    }
    *output_builder = builder;
    return RUNTIMEC_STATUS_SUCCESS;
}

static void test_builder_creation_shape(void) {
    test_allocator_context_t counters = {0U, false, 0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    binrwir_program_identity_t identity = fixture_identity();
    binrwir_program_identity_t invalid_identity = fixture_identity();
    binrwir_program_builder_t *builder =
        (binrwir_program_builder_t *)(uintptr_t)1U;

    RUNTIMEC_EXPECT(binrwir_program_builder_create(NULL, &identity, &builder) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(builder == NULL);

    builder = (binrwir_program_builder_t *)(uintptr_t)1U;
    RUNTIMEC_EXPECT(binrwir_program_builder_create(&allocator, NULL,
                                                   &builder) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(builder == NULL);

    /* identity 的版本字段取 UINT32_MAX 就是「无效」的唯一表示。 */
    invalid_identity.schema_version = UINT32_MAX;
    RUNTIMEC_EXPECT(binrwir_program_builder_create(
                        &allocator, &invalid_identity, &builder) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(builder == NULL);

    /* 二进制接口代次不是当前代次的 Program 不可消费，builder 也不接受。 */
    invalid_identity = fixture_identity();
    invalid_identity.binrwir_binary_interface_generation =
        BINRWIR_BINARY_INTERFACE_GENERATION + 1U;
    RUNTIMEC_EXPECT(binrwir_program_builder_create(
                        &allocator, &invalid_identity, &builder) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(builder == NULL);

    RUNTIMEC_EXPECT(binrwir_program_builder_create(&allocator, &identity,
                                                   &builder) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(builder != NULL);
    RUNTIMEC_EXPECT(binrwir_program_builder_destroy(NULL) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(binrwir_program_builder_destroy(builder) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

static void test_builder_rejects_invalid_parameter(void) {
    test_allocator_context_t counters = {0U, false, 0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    binrwir_program_builder_t *builder = NULL;
    uint32_t parameter_index = 0U;

    RUNTIMEC_EXPECT(fixture_builder_create(&allocator, &builder) ==
                    RUNTIMEC_STATUS_SUCCESS);
    parameter_index = 0U;
    RUNTIMEC_EXPECT(binrwir_program_builder_add_parameter(
                        builder, BINRWIR_PARAMETER_DIRECTION_READ, 11U,
                        type_view("int64|float64"),
                        &parameter_index) == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(parameter_index == BINRWIR_ARGUMENT_INDEX_NONE);

    parameter_index = 0U;
    RUNTIMEC_EXPECT(binrwir_program_builder_add_parameter(
                        builder, BINRWIR_PARAMETER_DIRECTION_READ, 11U,
                        runtimec_string_view_make(NULL, 0U),
                        &parameter_index) == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(parameter_index == BINRWIR_ARGUMENT_INDEX_NONE);

    parameter_index = 0U;
    RUNTIMEC_EXPECT(binrwir_program_builder_add_parameter(
                        builder, BINRWIR_PARAMETER_DIRECTION_READ,
                        BINRWIR_ARGUMENT_INDEX_NONE,
                        type_view("int64"),
                        &parameter_index) == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(parameter_index == BINRWIR_ARGUMENT_INDEX_NONE);

    parameter_index = 0U;
    RUNTIMEC_EXPECT(binrwir_program_builder_add_parameter(
                        builder, BINRWIR_PARAMETER_DIRECTION_INVALID, 11U,
                        type_view("int64"),
                        &parameter_index) == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(parameter_index == BINRWIR_ARGUMENT_INDEX_NONE);

    RUNTIMEC_EXPECT(binrwir_program_builder_destroy(builder) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

static void test_builder_rejects_invalid_alias_rule(void) {
    test_allocator_context_t counters = {0U, false, 0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    binrwir_program_builder_t *builder = NULL;
    binrwir_alias_rule_t rule = {
        .read_parameter_index = 0U,
        .write_parameter_index = 0U,
        .policy = BINRWIR_ALIAS_MAY_ALIAS,
        .flags = BINRWIR_ALIAS_RULE_FLAG_NONE,
        .reserved_16_bits = 0U,
    };
    uint32_t rule_identifier = 0U;

    RUNTIMEC_EXPECT(fixture_builder_create(&allocator, &builder) ==
                    RUNTIMEC_STATUS_SUCCESS);
    rule.policy = BINRWIR_ALIAS_POLICY_INVALID;
    RUNTIMEC_EXPECT(binrwir_program_builder_add_alias_rule(
                        builder, &rule, &rule_identifier) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(rule_identifier == BINRWIR_IDENTIFIER_NONE);

    rule.policy = BINRWIR_ALIAS_MAY_ALIAS;
    rule.reserved_16_bits = 1U;
    rule_identifier = 0U;
    RUNTIMEC_EXPECT(binrwir_program_builder_add_alias_rule(
                        builder, &rule, &rule_identifier) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(rule_identifier == BINRWIR_IDENTIFIER_NONE);

    RUNTIMEC_EXPECT(binrwir_program_builder_add_alias_rule(builder, NULL,
                                                           &rule_identifier) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(binrwir_program_builder_destroy(builder) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

static void test_finalize_rejects_empty_builder(void) {
    test_allocator_context_t counters = {0U, false, 0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    binrwir_program_builder_t *builder = NULL;
    binrwir_program_t *program = (binrwir_program_t *)(uintptr_t)1U;

    RUNTIMEC_EXPECT(fixture_builder_create(&allocator, &builder) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_builder_finalize(builder, &program) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(program == NULL);
    RUNTIMEC_EXPECT(binrwir_program_builder_destroy(builder) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

static void test_program_round_trip(void) {
    test_allocator_context_t counters = {0U, false, 0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    binrwir_program_builder_t *builder = NULL;
    binrwir_program_t *program = NULL;
    binrwir_program_identity_t identity;
    binrwir_program_view_t view;
    binrwir_instruction_view_t instruction_view;
    binrwir_alias_policy_t policy = BINRWIR_ALIAS_POLICY_INVALID;
    runtimec_string_view_t int64_literal = type_view("int64");
    runtimec_string_view_t char_literal =
        type_view("[]char/utf8");
    runtimec_string_view_t float_literal =
        type_view("float64");

    RUNTIMEC_EXPECT(fixture_program_build(&allocator, &builder, &program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(program != NULL);
    RUNTIMEC_EXPECT(binrwir_program_validate(program) ==
                    RUNTIMEC_STATUS_SUCCESS);

    memset(&identity, 0xff, sizeof(identity));
    RUNTIMEC_EXPECT(binrwir_program_get_identity(program, &identity) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(identity.snapshot_generation == 1U);
    RUNTIMEC_EXPECT(identity.program_generation == 2U);
    RUNTIMEC_EXPECT(identity.schema_version == 1U);
    RUNTIMEC_EXPECT(identity.binrwir_binary_interface_generation ==
                    BINRWIR_BINARY_INTERFACE_GENERATION);

    memset(&view, 0xff, sizeof(view));
    RUNTIMEC_EXPECT(binrwir_program_get_view(program, &view) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(view.instruction_count == 1U);
    RUNTIMEC_EXPECT(view.signature.read_parameter_count == 2U);
    RUNTIMEC_EXPECT(view.signature.write_parameter_count == 1U);
    RUNTIMEC_EXPECT(view.signature.alias_rule_count == 1U);
    RUNTIMEC_EXPECT(view.signature.read_parameter_argument_indices[0] ==
                    TEST_READ_ARGUMENT_INDEX);
    RUNTIMEC_EXPECT(view.signature.read_parameter_argument_indices[1] ==
                    TEST_SECOND_READ_ARGUMENT_INDEX);
    RUNTIMEC_EXPECT(view.signature.write_parameter_argument_indices[0] ==
                    TEST_WRITE_ARGUMENT_INDEX);

    /* 类型文本是深拷贝：指针不指向测试里的字面量，销毁 builder 后仍然可读。 */
    RUNTIMEC_EXPECT(view.signature.read_parameter_types[0].data !=
                    int64_literal.data);
    RUNTIMEC_EXPECT(binrwir_program_builder_destroy(builder) ==
                    RUNTIMEC_STATUS_SUCCESS);
    {
        runtimec_string_view_t type = view.signature.read_parameter_types[0];

        RUNTIMEC_EXPECT(type.size == int64_literal.size);
        RUNTIMEC_EXPECT(memcmp(type.data, int64_literal.data, type.size) == 0);
        type = view.signature.read_parameter_types[1];
        RUNTIMEC_EXPECT(type.size == char_literal.size);
        RUNTIMEC_EXPECT(memcmp(type.data, char_literal.data, type.size) == 0);
        type = view.signature.write_parameter_types[0];
        RUNTIMEC_EXPECT(type.size == float_literal.size);
        RUNTIMEC_EXPECT(memcmp(type.data, float_literal.data, type.size) == 0);
    }

    RUNTIMEC_EXPECT(binrwir_signature_get_alias_policy(&view.signature, 0U, 0U,
                                                       &policy) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(policy == BINRWIR_ALIAS_MAY_ALIAS);

    memset(&instruction_view, 0xff, sizeof(instruction_view));
    RUNTIMEC_EXPECT(binrwir_program_get_instruction(program, 0U,
                                                    &instruction_view) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(instruction_view.instruction->operation_identifier ==
                    TEST_OPERATION_IDENTIFIER);
    RUNTIMEC_EXPECT(instruction_view.source_logical_program_counter ==
                    TEST_SOURCE_PROGRAM_COUNTER);
    RUNTIMEC_EXPECT(instruction_view.reserved_32_bits == 0U);
    RUNTIMEC_EXPECT(instruction_view.read_arguments.count == 2U);
    RUNTIMEC_EXPECT(instruction_view.write_arguments.count == 1U);
    RUNTIMEC_EXPECT(instruction_view.read_arguments.data[0].argument_index ==
                    TEST_READ_ARGUMENT_INDEX);
    RUNTIMEC_EXPECT(instruction_view.read_arguments.data[0].reference_kind ==
                    BINRWIR_REFERENCE_PARAMETER_READ);
    RUNTIMEC_EXPECT(instruction_view.read_arguments.data[1].payload ==
                    TEST_STATIC_BODY_ADDRESS);
    RUNTIMEC_EXPECT(instruction_view.write_arguments.data[0].argument_index ==
                    TEST_WRITE_ARGUMENT_INDEX);

    /* 标识符是稠密序号：越界与 NONE 都不是合法指令。 */
    RUNTIMEC_EXPECT(binrwir_program_get_instruction(program, 1U,
                                                    &instruction_view) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(instruction_view.instruction == NULL);
    RUNTIMEC_EXPECT(binrwir_program_get_instruction(
                        program, BINRWIR_IDENTIFIER_NONE,
                        &instruction_view) == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(binrwir_program_get_instruction(program, 0U, NULL) ==
                    RUNTIMEC_STATUS_INVALID);

    RUNTIMEC_EXPECT(binrwir_program_destroy(program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

static void test_finalize_rejects_unbound_parameter(void) {
    test_allocator_context_t counters = {0U, false, 0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    binrwir_program_builder_t *builder = NULL;
    binrwir_program_t *program = (binrwir_program_t *)(uintptr_t)1U;
    binrwir_argument_t read_argument;
    binrwir_instruction_build_request_t request;
    uint32_t instruction_identifier = BINRWIR_IDENTIFIER_NONE;
    uint32_t parameter_index = 0U;

    memset(&request, 0, sizeof(request));
    request.source_logical_program_counter = TEST_SOURCE_PROGRAM_COUNTER;
    request.operation_identifier = TEST_OPERATION_IDENTIFIER;
    request.read_arguments.data = &read_argument;
    request.read_arguments.count = 1U;

    /* 参数引用指向签名里不存在的读参序号。 */
    RUNTIMEC_EXPECT(fixture_builder_create(&allocator, &builder) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        TEST_READ_ARGUMENT_INDEX,
                        BINRWIR_REFERENCE_PARAMETER_READ, 5U, 0U,
                        &read_argument) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_builder_add_instruction(
                        builder, &request, &instruction_identifier) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_builder_finalize(builder, &program) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(program == NULL);
    RUNTIMEC_EXPECT(binrwir_program_builder_destroy(builder) ==
                    RUNTIMEC_STATUS_SUCCESS);

    /* slot 与 argidx 各说一套：同一个参数就有了第二处真相。 */
    RUNTIMEC_EXPECT(fixture_builder_create(&allocator, &builder) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_argument_create(
                        TEST_SECOND_READ_ARGUMENT_INDEX,
                        BINRWIR_REFERENCE_PARAMETER_READ, 0U, 0U,
                        &read_argument) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_builder_add_instruction(
                        builder, &request, &instruction_identifier) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_builder_finalize(builder, &program) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(program == NULL);
    RUNTIMEC_EXPECT(binrwir_program_builder_destroy(builder) ==
                    RUNTIMEC_STATUS_SUCCESS);

    /* 同一方向内重复登记 argidx：签名只有一份 argidx，重复即非法。 */
    RUNTIMEC_EXPECT(fixture_builder_create(&allocator, &builder) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_builder_add_parameter(
                        builder, BINRWIR_PARAMETER_DIRECTION_READ,
                        TEST_READ_ARGUMENT_INDEX, type_view("int64"),
                        &parameter_index) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(fixture_add_instruction(builder) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_builder_finalize(builder, &program) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(program == NULL);
    RUNTIMEC_EXPECT(binrwir_program_builder_destroy(builder) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

static void test_builder_is_one_shot(void) {
    test_allocator_context_t counters = {0U, false, 0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    binrwir_program_builder_t *builder = NULL;
    binrwir_program_t *program = NULL;
    binrwir_program_t *second_program = (binrwir_program_t *)(uintptr_t)1U;
    uint32_t parameter_index = 0U;

    RUNTIMEC_EXPECT(fixture_program_build(&allocator, &builder, &program) ==
                    RUNTIMEC_STATUS_SUCCESS);

    /* 定稿后一切写入都被拒绝，第二次 finalize 也不给第二个 Program。 */
    RUNTIMEC_EXPECT(binrwir_program_builder_add_parameter(
                        builder, BINRWIR_PARAMETER_DIRECTION_READ, 21U,
                        type_view("int64"),
                        &parameter_index) == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(parameter_index == BINRWIR_ARGUMENT_INDEX_NONE);
    RUNTIMEC_EXPECT(binrwir_program_builder_finalize(builder,
                                                     &second_program) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(second_program == NULL);

    /* reset 回到空的可写状态：空 builder 不能定稿，重新登记后可以。 */
    RUNTIMEC_EXPECT(binrwir_program_builder_reset(builder) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_builder_finalize(builder,
                                                     &second_program) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(second_program == NULL);
    RUNTIMEC_EXPECT(fixture_register_signature(builder) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(fixture_add_instruction(builder) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_builder_finalize(builder,
                                                     &second_program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(second_program != NULL);
    RUNTIMEC_EXPECT(binrwir_program_validate(second_program) ==
                    RUNTIMEC_STATUS_SUCCESS);

    RUNTIMEC_EXPECT(binrwir_program_validate(program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_destroy(program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_destroy(second_program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_builder_destroy(builder) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

/* 定稿的每一次分配失败都必须失败封闭，且 builder 状态原封不动。 */
static void test_builder_reports_out_of_memory(void) {
    test_allocator_context_t counters = {0U, false, 0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    binrwir_program_builder_t *builder = NULL;
    binrwir_program_t *program = NULL;
    size_t successful_allocations;

    for (successful_allocations = 0U;
         successful_allocations < TEST_FINALIZE_ALLOCATION_COUNT;
         ++successful_allocations) {
        RUNTIMEC_EXPECT(fixture_builder_create(&allocator, &builder) ==
                        RUNTIMEC_STATUS_SUCCESS);
        RUNTIMEC_EXPECT(fixture_add_instruction(builder) ==
                        RUNTIMEC_STATUS_SUCCESS);
        counters.fail_allocations = true;
        counters.remaining_successful_allocations = successful_allocations;
        RUNTIMEC_EXPECT(binrwir_program_builder_finalize(builder, &program) ==
                        RUNTIMEC_STATUS_OUT_OF_MEMORY);
        RUNTIMEC_EXPECT(program == NULL);
        counters.fail_allocations = false;

        /* 失败不改 builder：同样的内容再定稿一次就成功。 */
        RUNTIMEC_EXPECT(binrwir_program_builder_finalize(builder, &program) ==
                        RUNTIMEC_STATUS_SUCCESS);
        RUNTIMEC_EXPECT(program != NULL);
        RUNTIMEC_EXPECT(binrwir_program_destroy(program) ==
                        RUNTIMEC_STATUS_SUCCESS);
        program = NULL;
        RUNTIMEC_EXPECT(binrwir_program_builder_destroy(builder) ==
                        RUNTIMEC_STATUS_SUCCESS);
        RUNTIMEC_EXPECT(counters.live_allocations == 0U);
    }
}

/* 带三个池的定稿：每个池的分配失败也要失败封闭、不留半个 Program。 */
static void test_pool_finalize_reports_out_of_memory(void) {
    test_allocator_context_t counters = {0U, false, 0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    binrwir_program_identity_t identity = fixture_identity();
    unsigned char encoded[4] = {0x01U, 0x02U, 0x03U, 0x04U};
    size_t successful_allocations;
    runtimec_status_t status;

    for (successful_allocations = 0U;
         successful_allocations < TEST_POOL_FINALIZE_ALLOCATION_COUNT;
         ++successful_allocations) {
        binrwir_program_builder_t *builder = NULL;
        binrwir_program_t *program = NULL;
        binrwir_argument_t local_argument;
        binrwir_instruction_build_request_t request;
        uint16_t symbol_index = BINRWIR_SYMBOL_INDEX_NONE;
        uint32_t literal_index = BINRWIR_LITERAL_INDEX_NONE;
        uint32_t instruction_identifier = BINRWIR_IDENTIFIER_NONE;

        RUNTIMEC_EXPECT(binrwir_program_builder_create(&allocator, &identity,
                                                       &builder) ==
                        RUNTIMEC_STATUS_SUCCESS);
        RUNTIMEC_EXPECT(binrwir_program_builder_add_symbol(
                            builder, type_view("alpha"), &symbol_index) ==
                        RUNTIMEC_STATUS_SUCCESS);
        RUNTIMEC_EXPECT(binrwir_program_builder_add_literal(
                            builder,
                            runtimec_bytes_view_make(encoded, sizeof(encoded)),
                            type_view("int64"), &literal_index) ==
                        RUNTIMEC_STATUS_SUCCESS);
        RUNTIMEC_EXPECT(binrwir_argument_create(1U, BINRWIR_REFERENCE_LOCAL,
                                                symbol_index, 0U,
                                                &local_argument) ==
                        RUNTIMEC_STATUS_SUCCESS);
        memset(&request, 0, sizeof(request));
        request.source_logical_program_counter = TEST_SOURCE_PROGRAM_COUNTER;
        request.operation_identifier = TEST_OPERATION_IDENTIFIER;
        request.read_arguments.data = &local_argument;
        request.read_arguments.count = 1U;
        RUNTIMEC_EXPECT(binrwir_program_builder_add_instruction(
                            builder, &request, &instruction_identifier) ==
                        RUNTIMEC_STATUS_SUCCESS);

        counters.fail_allocations = true;
        counters.remaining_successful_allocations = successful_allocations;
        status = binrwir_program_builder_finalize(builder, &program);
        RUNTIMEC_EXPECT(status == RUNTIMEC_STATUS_OUT_OF_MEMORY);
        RUNTIMEC_EXPECT(program == NULL);
        counters.fail_allocations = false;

        RUNTIMEC_EXPECT(binrwir_program_builder_finalize(builder, &program) ==
                        RUNTIMEC_STATUS_SUCCESS);
        RUNTIMEC_EXPECT(program != NULL);
        RUNTIMEC_EXPECT(binrwir_program_destroy(program) ==
                        RUNTIMEC_STATUS_SUCCESS);
        RUNTIMEC_EXPECT(binrwir_program_builder_destroy(builder) ==
                        RUNTIMEC_STATUS_SUCCESS);
        RUNTIMEC_EXPECT(counters.live_allocations == 0U);
    }
}

/* 登记阶段的分配失败同样明确报错，输出回到无效值。 */
static void test_registration_reports_out_of_memory(void) {
    test_allocator_context_t counters = {0U, false, 0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    binrwir_program_builder_t *builder = NULL;
    binrwir_program_t *program = (binrwir_program_t *)(uintptr_t)1U;
    uint32_t parameter_index = 0U;
    uint32_t instruction_identifier = BINRWIR_IDENTIFIER_NONE;
    binrwir_argument_t read_argument;
    binrwir_instruction_build_request_t request;
    binrwir_program_identity_t identity = fixture_identity();

    RUNTIMEC_EXPECT(binrwir_program_builder_create(&allocator, &identity,
                                                   &builder) ==
                    RUNTIMEC_STATUS_SUCCESS);
    counters.fail_allocations = true;
    RUNTIMEC_EXPECT(binrwir_program_builder_add_parameter(
                        builder, BINRWIR_PARAMETER_DIRECTION_READ, 31U,
                        type_view("int64"),
                        &parameter_index) == RUNTIMEC_STATUS_OUT_OF_MEMORY);
    RUNTIMEC_EXPECT(parameter_index == BINRWIR_ARGUMENT_INDEX_NONE);

    RUNTIMEC_EXPECT(binrwir_argument_create(
                        TEST_READ_ARGUMENT_INDEX,
                        BINRWIR_REFERENCE_PARAMETER_READ, 0U, 0U,
                        &read_argument) == RUNTIMEC_STATUS_SUCCESS);
    memset(&request, 0, sizeof(request));
    request.source_logical_program_counter = TEST_SOURCE_PROGRAM_COUNTER;
    request.operation_identifier = TEST_OPERATION_IDENTIFIER;
    request.read_arguments.data = &read_argument;
    request.read_arguments.count = 1U;
    RUNTIMEC_EXPECT(binrwir_program_builder_add_instruction(
                        builder, &request, &instruction_identifier) ==
                    RUNTIMEC_STATUS_OUT_OF_MEMORY);
    RUNTIMEC_EXPECT(instruction_identifier == BINRWIR_IDENTIFIER_NONE);
    RUNTIMEC_EXPECT(binrwir_program_builder_finalize(builder, &program) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(program == NULL);
    counters.fail_allocations = false;
    RUNTIMEC_EXPECT(binrwir_program_builder_destroy(builder) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

/*
 * 同一个函数体：符号按 insertion_names 的顺序登记，但三条指令引用的名字固定是
 * alpha / beta / sum。登记顺序不同、引用的符号相同，产出的 Program 就必须相同。
 */
static runtimec_status_t build_symbol_program(
    const runtimec_allocator_t *allocator, const char *const *insertion_names,
    binrwir_program_t **output_program) {
    static const char *const argument_names[3] = {"alpha", "beta", "sum"};
    binrwir_program_identity_t identity = fixture_identity();
    binrwir_program_builder_t *builder = NULL;
    binrwir_argument_t arguments[3];
    binrwir_instruction_build_request_t request;
    uint16_t symbol_index = BINRWIR_SYMBOL_INDEX_NONE;
    uint32_t instruction_identifier = BINRWIR_IDENTIFIER_NONE;
    size_t ordinal;
    size_t argument_ordinal;
    runtimec_status_t status;

    status = binrwir_program_builder_create(allocator, &identity, &builder);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    for (ordinal = 0U; ordinal < 3U; ++ordinal) {
        status = binrwir_program_builder_add_symbol(
            builder, type_view(insertion_names[ordinal]), &symbol_index);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            break;
        }
    }
    for (argument_ordinal = 0U;
         status == RUNTIMEC_STATUS_SUCCESS && argument_ordinal < 3U;
         ++argument_ordinal) {
        status = binrwir_program_builder_add_symbol(
            builder, type_view(argument_names[argument_ordinal]),
            &symbol_index);
        if (status == RUNTIMEC_STATUS_SUCCESS) {
            status = binrwir_argument_create(
                (uint32_t)argument_ordinal + 1U, BINRWIR_REFERENCE_LOCAL,
                symbol_index, 0U, &arguments[argument_ordinal]);
        }
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        memset(&request, 0, sizeof(request));
        request.source_logical_program_counter = TEST_SOURCE_PROGRAM_COUNTER;
        request.operation_identifier = TEST_OPERATION_IDENTIFIER;
        request.read_arguments.data = arguments;
        request.read_arguments.count = 3U;
        status = binrwir_program_builder_add_instruction(
            builder, &request, &instruction_identifier);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_program_builder_finalize(builder, output_program);
    }
    (void)binrwir_program_builder_destroy(builder);
    return status;
}

static void test_symbol_pool_is_canonical(void) {
    test_allocator_context_t counters = {0U, false, 0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    const char *const forward_names[3] = {"sum", "alpha", "beta"};
    const char *const reverse_names[3] = {"beta", "alpha", "sum"};
    const char *const expected_names[3] = {"alpha", "beta", "sum"};
    binrwir_program_t *forward_program = NULL;
    binrwir_program_t *reverse_program = NULL;
    binrwir_instruction_view_t forward_view;
    binrwir_instruction_view_t reverse_view;
    runtimec_string_view_t name;
    size_t ordinal;

    RUNTIMEC_EXPECT(build_symbol_program(&allocator, forward_names,
                                         &forward_program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(build_symbol_program(&allocator, reverse_names,
                                         &reverse_program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_validate(forward_program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_validate(reverse_program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    if (forward_program == NULL || reverse_program == NULL) {
        if (forward_program != NULL) {
            (void)binrwir_program_destroy(forward_program);
        }
        if (reverse_program != NULL) {
            (void)binrwir_program_destroy(reverse_program);
        }
        return;
    }

    for (ordinal = 0U; ordinal < 3U; ++ordinal) {
        name = runtimec_string_view_make(NULL, 0U);
        RUNTIMEC_EXPECT(binrwir_program_get_symbol(forward_program,
                                                   (uint16_t)ordinal,
                                                   &name) ==
                        RUNTIMEC_STATUS_SUCCESS);
        RUNTIMEC_EXPECT(name.size == strlen(expected_names[ordinal]));
        RUNTIMEC_EXPECT(memcmp(name.data, expected_names[ordinal],
                               name.size) == 0);
    }

    /* 登记期下标与 Program 下标不同：两个 builder 的插入顺序相反，但参数槽都指向
       同一个规范位置，所以指令视图逐项相同。 */
    RUNTIMEC_EXPECT(binrwir_program_get_instruction(forward_program, 0U,
                                                    &forward_view) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_get_instruction(reverse_program, 0U,
                                                    &reverse_view) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(forward_view.read_arguments.count == 3U);
    for (ordinal = 0U; ordinal < 3U; ++ordinal) {
        const binrwir_argument_t *forward_argument =
            &forward_view.read_arguments.data[ordinal];
        const binrwir_argument_t *reverse_argument =
            &reverse_view.read_arguments.data[ordinal];

        RUNTIMEC_EXPECT(forward_argument->reference_kind ==
                        BINRWIR_REFERENCE_LOCAL);
        RUNTIMEC_EXPECT(forward_argument->slot == reverse_argument->slot);
        RUNTIMEC_EXPECT(forward_argument->argument_index ==
                        reverse_argument->argument_index);
    }
    /* forward 的 "sum" 排在第 2 位，reverse 的 "beta" 也排在第 2 位。 */
    RUNTIMEC_EXPECT(forward_view.read_arguments.data[0].slot == 0U);
    RUNTIMEC_EXPECT(forward_view.read_arguments.data[2].slot == 2U);

    RUNTIMEC_EXPECT(binrwir_program_destroy(forward_program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_destroy(reverse_program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

static void test_symbol_pool_rejects_invalid_content(void) {
    test_allocator_context_t counters = {0U, false, 0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    binrwir_program_identity_t identity = fixture_identity();
    binrwir_program_builder_t *builder = NULL;
    binrwir_program_t *program = (binrwir_program_t *)(uintptr_t)1U;
    binrwir_argument_t local_argument;
    binrwir_instruction_build_request_t request;
    uint16_t symbol_index = 0U;
    uint32_t instruction_identifier = BINRWIR_IDENTIFIER_NONE;

    RUNTIMEC_EXPECT(binrwir_program_builder_create(&allocator, &identity,
                                                   &builder) ==
                    RUNTIMEC_STATUS_SUCCESS);
    symbol_index = 0U;
    RUNTIMEC_EXPECT(binrwir_program_builder_add_symbol(
                        builder, runtimec_string_view_make(NULL, 0U),
                        &symbol_index) == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(symbol_index == BINRWIR_SYMBOL_INDEX_NONE);

    /* 同名重复登记是幂等的，返回同一个下标。 */
    RUNTIMEC_EXPECT(binrwir_program_builder_add_symbol(builder,
                                                       type_view("alpha"),
                                                       &symbol_index) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(symbol_index == 0U);
    RUNTIMEC_EXPECT(binrwir_program_builder_add_symbol(builder,
                                                       type_view("alpha"),
                                                       &symbol_index) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(symbol_index == 0U);

    /* 没有符号池却有 LOCAL 引用：不查表、不猜，finalize 直接 INVALID。 */
    RUNTIMEC_EXPECT(binrwir_argument_create(1U, BINRWIR_REFERENCE_LOCAL, 7U, 0U,
                                            &local_argument) ==
                    RUNTIMEC_STATUS_SUCCESS);
    memset(&request, 0, sizeof(request));
    request.source_logical_program_counter = TEST_SOURCE_PROGRAM_COUNTER;
    request.operation_identifier = TEST_OPERATION_IDENTIFIER;
    request.read_arguments.data = &local_argument;
    request.read_arguments.count = 1U;
    RUNTIMEC_EXPECT(binrwir_program_builder_add_instruction(
                        builder, &request, &instruction_identifier) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_builder_finalize(builder, &program) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(program == NULL);
    RUNTIMEC_EXPECT(binrwir_program_builder_destroy(builder) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

static void test_literal_pool_round_trip(void) {
    test_allocator_context_t counters = {0U, false, 0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    binrwir_program_identity_t identity = fixture_identity();
    binrwir_program_builder_t *builder = NULL;
    binrwir_program_t *program = NULL;
    binrwir_instruction_view_t instruction_view;
    binrwir_argument_t literal_argument;
    binrwir_instruction_build_request_t request;
    runtimec_bytes_view_t source_bytes;
    runtimec_bytes_view_t wire_bytes;
    runtimec_string_view_t langtype;
    unsigned char encoded[8] = {0x01U, 0x00U, 0x00U, 0x00U,
                                0x02U, 0x00U, 0x00U, 0x00U};
    uint32_t literal_index = BINRWIR_LITERAL_INDEX_NONE;
    uint32_t instruction_identifier = BINRWIR_IDENTIFIER_NONE;

    source_bytes = runtimec_bytes_view_make(encoded, sizeof(encoded));
    RUNTIMEC_EXPECT(binrwir_program_builder_create(&allocator, &identity,
                                                   &builder) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_builder_add_literal(
                        builder, source_bytes, type_view("int64"),
                        &literal_index) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(literal_index == 0U);
    RUNTIMEC_EXPECT(binrwir_argument_create(1U, BINRWIR_REFERENCE_LITERAL, 0U,
                                            literal_index, &literal_argument) ==
                    RUNTIMEC_STATUS_SUCCESS);
    memset(&request, 0, sizeof(request));
    request.source_logical_program_counter = TEST_SOURCE_PROGRAM_COUNTER;
    request.operation_identifier = TEST_OPERATION_IDENTIFIER;
    request.read_arguments.data = &literal_argument;
    request.read_arguments.count = 1U;
    RUNTIMEC_EXPECT(binrwir_program_builder_add_instruction(
                        builder, &request, &instruction_identifier) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_builder_finalize(builder, &program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_validate(program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_builder_destroy(builder) ==
                    RUNTIMEC_STATUS_SUCCESS);
    if (program == NULL) {
        return;
    }

    /* 字节与类型都是深拷贝：改掉调用方缓冲之后 Program 里的字面量不变。 */
    memset(encoded, 0xff, sizeof(encoded));
    wire_bytes = runtimec_bytes_view_make(NULL, 0U);
    langtype = runtimec_string_view_make(NULL, 0U);
    RUNTIMEC_EXPECT(binrwir_program_get_literal(program, 0U, &wire_bytes,
                                                &langtype) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(wire_bytes.size == 8U);
    RUNTIMEC_EXPECT(wire_bytes.data != encoded);
    RUNTIMEC_EXPECT(wire_bytes.data[0] == 0x01U);
    RUNTIMEC_EXPECT(wire_bytes.data[4] == 0x02U);
    RUNTIMEC_EXPECT(langtype.size == 5U);
    RUNTIMEC_EXPECT(memcmp(langtype.data, "int64", langtype.size) == 0);

    RUNTIMEC_EXPECT(binrwir_program_get_instruction(program, 0U,
                                                    &instruction_view) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(instruction_view.read_arguments.data[0].reference_kind ==
                    BINRWIR_REFERENCE_LITERAL);
    RUNTIMEC_EXPECT(instruction_view.read_arguments.data[0].payload == 0U);

    /* 越界与空输入：失败封闭，输出回到无效值。 */
    wire_bytes = runtimec_bytes_view_make(encoded, sizeof(encoded));
    langtype = type_view("int64");
    RUNTIMEC_EXPECT(binrwir_program_get_literal(program, 1U, &wire_bytes,
                                                &langtype) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(wire_bytes.data == NULL);
    RUNTIMEC_EXPECT(langtype.data == NULL);

    RUNTIMEC_EXPECT(binrwir_program_destroy(program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

static void test_literal_pool_rejects_invalid_content(void) {
    test_allocator_context_t counters = {0U, false, 0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    binrwir_program_identity_t identity = fixture_identity();
    binrwir_program_builder_t *builder = NULL;
    binrwir_program_t *program = (binrwir_program_t *)(uintptr_t)1U;
    binrwir_argument_t literal_argument;
    binrwir_instruction_build_request_t request;
    unsigned char encoded[4] = {0x01U, 0x02U, 0x03U, 0x04U};
    uint32_t literal_index = 0U;
    uint32_t instruction_identifier = BINRWIR_IDENTIFIER_NONE;

    RUNTIMEC_EXPECT(binrwir_program_builder_create(&allocator, &identity,
                                                   &builder) ==
                    RUNTIMEC_STATUS_SUCCESS);
    literal_index = 0U;
    RUNTIMEC_EXPECT(binrwir_program_builder_add_literal(
                        builder, runtimec_bytes_view_make(NULL, 0U),
                        type_view("int64"), &literal_index) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(literal_index == BINRWIR_LITERAL_INDEX_NONE);
    RUNTIMEC_EXPECT(binrwir_program_builder_add_literal(
                        builder, runtimec_bytes_view_make(encoded,
                                                          sizeof(encoded)),
                        type_view("int64|float64"), &literal_index) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(literal_index == BINRWIR_LITERAL_INDEX_NONE);

    /* 字面量池里没有这一项：payload 越界必须被 finalize 拒绝。 */
    RUNTIMEC_EXPECT(binrwir_argument_create(1U, BINRWIR_REFERENCE_LITERAL, 0U,
                                            3U, &literal_argument) ==
                    RUNTIMEC_STATUS_SUCCESS);
    memset(&request, 0, sizeof(request));
    request.source_logical_program_counter = TEST_SOURCE_PROGRAM_COUNTER;
    request.operation_identifier = TEST_OPERATION_IDENTIFIER;
    request.read_arguments.data = &literal_argument;
    request.read_arguments.count = 1U;
    RUNTIMEC_EXPECT(binrwir_program_builder_add_instruction(
                        builder, &request, &instruction_identifier) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_builder_finalize(builder, &program) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(program == NULL);
    RUNTIMEC_EXPECT(binrwir_program_builder_destroy(builder) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

/* FUNCTION 引用：slot 是符号池下标、payload 必须为零。 */
static void test_function_reference_round_trip(void) {
    test_allocator_context_t counters = {0U, false, 0U};
    runtimec_allocator_t allocator = test_allocator(&counters);
    binrwir_program_identity_t identity = fixture_identity();
    binrwir_program_builder_t *builder = NULL;
    binrwir_program_t *program = NULL;
    binrwir_instruction_view_t instruction_view;
    binrwir_argument_t function_argument;
    binrwir_instruction_build_request_t request;
    runtimec_string_view_t name;
    uint16_t symbol_index = BINRWIR_SYMBOL_INDEX_NONE;
    uint32_t instruction_identifier = BINRWIR_IDENTIFIER_NONE;

    RUNTIMEC_EXPECT(binrwir_argument_create(1U, BINRWIR_REFERENCE_FUNCTION, 3U,
                                            UINT64_C(1), &function_argument) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(function_argument.reference_kind ==
                    BINRWIR_REFERENCE_INVALID);

    RUNTIMEC_EXPECT(binrwir_program_builder_create(&allocator, &identity,
                                                   &builder) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_builder_add_symbol(
                        builder, type_view("string.concat"), &symbol_index) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_argument_create(1U, BINRWIR_REFERENCE_FUNCTION,
                                            symbol_index, 0U,
                                            &function_argument) ==
                    RUNTIMEC_STATUS_SUCCESS);
    memset(&request, 0, sizeof(request));
    request.source_logical_program_counter = TEST_SOURCE_PROGRAM_COUNTER;
    request.operation_identifier = TEST_OPERATION_IDENTIFIER;
    request.read_arguments.data = &function_argument;
    request.read_arguments.count = 1U;
    RUNTIMEC_EXPECT(binrwir_program_builder_add_instruction(
                        builder, &request, &instruction_identifier) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_builder_finalize(builder, &program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_validate(program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    if (program == NULL) {
        (void)binrwir_program_builder_destroy(builder);
        return;
    }

    name = runtimec_string_view_make(NULL, 0U);
    RUNTIMEC_EXPECT(binrwir_program_get_symbol(program, 0U, &name) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(name.size == strlen("string.concat"));
    RUNTIMEC_EXPECT(memcmp(name.data, "string.concat", name.size) == 0);
    RUNTIMEC_EXPECT(binrwir_program_get_symbol(program, 1U, &name) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(name.data == NULL);

    RUNTIMEC_EXPECT(binrwir_program_get_instruction(program, 0U,
                                                    &instruction_view) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(instruction_view.read_arguments.data[0].reference_kind ==
                    BINRWIR_REFERENCE_FUNCTION);
    RUNTIMEC_EXPECT(instruction_view.read_arguments.data[0].slot == 0U);

    RUNTIMEC_EXPECT(binrwir_program_destroy(program) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_program_builder_destroy(builder) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(counters.live_allocations == 0U);
}

static void test_program_query_failure_shape(void) {
    binrwir_program_identity_t identity;
    binrwir_program_view_t view;
    binrwir_instruction_view_t instruction_view;

    memset(&identity, 0xff, sizeof(identity));
    RUNTIMEC_EXPECT(binrwir_program_get_identity(NULL, &identity) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(identity.schema_version == UINT32_MAX);
    RUNTIMEC_EXPECT(identity.binrwir_binary_interface_generation == UINT32_MAX);

    memset(&view, 0xff, sizeof(view));
    RUNTIMEC_EXPECT(binrwir_program_get_view(NULL, &view) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(view.instruction_count == 0U);
    RUNTIMEC_EXPECT(view.signature.read_parameter_types == NULL);

    memset(&instruction_view, 0xff, sizeof(instruction_view));
    RUNTIMEC_EXPECT(binrwir_program_get_instruction(NULL, 0U,
                                                    &instruction_view) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(instruction_view.instruction == NULL);
    RUNTIMEC_EXPECT(binrwir_program_validate(NULL) == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(binrwir_program_destroy(NULL) == RUNTIMEC_STATUS_INVALID);
}

int main(void) {
    test_builder_creation_shape();
    test_builder_rejects_invalid_parameter();
    test_builder_rejects_invalid_alias_rule();
    test_finalize_rejects_empty_builder();
    test_program_round_trip();
    test_finalize_rejects_unbound_parameter();
    test_builder_is_one_shot();
    test_builder_reports_out_of_memory();
    test_registration_reports_out_of_memory();
    test_pool_finalize_reports_out_of_memory();
    test_symbol_pool_is_canonical();
    test_symbol_pool_rejects_invalid_content();
    test_literal_pool_round_trip();
    test_literal_pool_rejects_invalid_content();
    test_function_reference_round_trip();
    test_program_query_failure_shape();
    if (failures != 0) {
        fprintf(stderr, "binrwir_program_test: %d failure(s)\n", failures);
        return 1;
    }
    return 0;
}
