/* Address-shape-independent value types: instruction views and signature alias policy. */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "binrwir/execution_exit.h"
#include "binrwir/instruction.h"
#include "binrwir/signature.h"
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

static binrwir_argument_t argument(uint32_t argument_index,
                                   uint64_t body_address) {
    binrwir_argument_t result = {
        .argument_index = argument_index,
        .reference_kind = BINRWIR_REFERENCE_STATIC,
        .dereference = 0U,
        .slot = 0U,
        .payload = body_address,
    };
    return result;
}

static binrwir_instruction_build_request_t simple_request(void) {
    static binrwir_argument_t arguments[2];
    binrwir_instruction_build_request_t request;

    arguments[0] = argument(0U, UINT64_C(0x1008));
    arguments[1] = argument(1U, UINT64_C(0x2008));
    request.source_logical_program_counter = 7U;
    request.operation_identifier = 42U;
    request.read_arguments.data = &arguments[0];
    request.read_arguments.count = 1U;
    request.write_arguments.data = &arguments[1];
    request.write_arguments.count = 1U;
    return request;
}

static void test_instruction_request_accepts_consistent_content(void) {
    binrwir_instruction_build_request_t request = simple_request();
    binrwir_instruction_build_request_t bare = {
        .source_logical_program_counter = 0U,
        .operation_identifier = 1U,
        .read_arguments = {.data = NULL, .count = 0U},
        .write_arguments = {.data = NULL, .count = 0U},
    };

    RUNTIMEC_EXPECT(binrwir_instruction_validate_build_request(&request) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_instruction_validate_build_request(&bare) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_instruction_validate_build_request(NULL) ==
                    RUNTIMEC_STATUS_INVALID);
}

static void test_instruction_request_rejects_absent_identity(void) {
    binrwir_instruction_build_request_t request = simple_request();

    request.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_NONE;
    RUNTIMEC_EXPECT(binrwir_instruction_validate_build_request(&request) ==
                    RUNTIMEC_STATUS_INVALID);

    request = simple_request();
    request.source_logical_program_counter =
        BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE;
    RUNTIMEC_EXPECT(binrwir_instruction_validate_build_request(&request) ==
                    RUNTIMEC_STATUS_INVALID);
}

static void test_instruction_request_rejects_inconsistent_spans(void) {
    binrwir_instruction_build_request_t request = simple_request();
    binrwir_argument_t bad_argument;

    request.read_arguments.data = NULL;
    RUNTIMEC_EXPECT(binrwir_instruction_validate_build_request(&request) ==
                    RUNTIMEC_STATUS_INVALID);

    request = simple_request();
    request.write_arguments.count = UINT32_MAX;
    request.write_arguments.data = NULL;
    RUNTIMEC_EXPECT(binrwir_instruction_validate_build_request(&request) ==
                    RUNTIMEC_STATUS_INVALID);

    request = simple_request();
    request.read_arguments.count = UINT32_MAX;
    request.write_arguments.count = 1U;
    RUNTIMEC_EXPECT(binrwir_instruction_validate_build_request(&request) ==
                    RUNTIMEC_STATUS_INVALID);

    /*
     * STATIC + payload NONE 现在是**结构合法**的：写目标的 key 允许在编码期
     * 尚不存在，由执行期解析/创建。读目标不存在由 vmbin 值访问层拒绝，
     * 不在「参数形状」这一层判（形状只管字段自洽，不管语义）。
     */
    bad_argument = argument(0U, UINT64_C(0x1008));
    bad_argument.payload = BINRWIR_BODY_ADDRESS_NONE;
    request = simple_request();
    request.read_arguments.data = &bad_argument;
    RUNTIMEC_EXPECT(binrwir_instruction_validate_build_request(&request) ==
                    RUNTIMEC_STATUS_SUCCESS);

    bad_argument = argument(0U, UINT64_C(0x1008));
    bad_argument.reference_kind = BINRWIR_REFERENCE_PARAMETER_READ;
    request = simple_request();
    request.read_arguments.data = &bad_argument;
    RUNTIMEC_EXPECT(binrwir_instruction_validate_build_request(&request) ==
                    RUNTIMEC_STATUS_INVALID);

    bad_argument = argument(BINRWIR_ARGUMENT_INDEX_NONE, UINT64_C(0x1008));
    request = simple_request();
    request.read_arguments.data = &bad_argument;
    RUNTIMEC_EXPECT(binrwir_instruction_validate_build_request(&request) ==
                    RUNTIMEC_STATUS_INVALID);

    bad_argument = argument(0U, UINT64_C(0x1008));
    bad_argument.dereference = 2U;  /* 0/1 之外的值才非法 */
    request = simple_request();
    request.write_arguments.data = &bad_argument;
    RUNTIMEC_EXPECT(binrwir_instruction_validate_build_request(&request) ==
                    RUNTIMEC_STATUS_INVALID);
}

static void test_instruction_view_round_trip(void) {
    binrwir_argument_t read_arguments[1] = {argument(0U, UINT64_C(0x1008))};
    binrwir_argument_t write_arguments[2] = {
        argument(1U, UINT64_C(0x2008)),
        argument(2U, UINT64_C(0x3008)),
    };
    binrwir_instruction_t instruction = {.operation_identifier = 42U};
    binrwir_instruction_view_t view;
    binrwir_argument_span_t read_span = {read_arguments, 1U};
    binrwir_argument_span_t write_span = {write_arguments, 2U};

    memset(&view, 0xff, sizeof(view));
    RUNTIMEC_EXPECT(binrwir_instruction_get_view(
                        &instruction, 7U, read_span, write_span, &view) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(view.instruction == &instruction);
    RUNTIMEC_EXPECT(view.source_logical_program_counter == 7U);
    RUNTIMEC_EXPECT(view.reserved_32_bits == 0U);
    RUNTIMEC_EXPECT(view.read_arguments.count == 1U);
    RUNTIMEC_EXPECT(view.read_arguments.data == read_arguments);
    RUNTIMEC_EXPECT(view.write_arguments.count == 2U);
    RUNTIMEC_EXPECT(view.write_arguments.data == write_arguments);
}

static void test_instruction_view_failure_clears_output(void) {
    binrwir_instruction_t instruction = {
        .operation_identifier = BINRWIR_OPERATION_IDENTIFIER_NONE,
    };
    binrwir_instruction_view_t view;
    binrwir_argument_span_t empty_span = {NULL, 0U};

    memset(&view, 0xff, sizeof(view));
    RUNTIMEC_EXPECT(binrwir_instruction_get_view(
                        &instruction, 3U, empty_span, empty_span, &view) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(view.instruction == NULL);
    RUNTIMEC_EXPECT(view.source_logical_program_counter == 0U);
    RUNTIMEC_EXPECT(view.reserved_32_bits == 0U);
    RUNTIMEC_EXPECT(view.read_arguments.data == NULL);
    RUNTIMEC_EXPECT(view.read_arguments.count == 0U);

    memset(&view, 0xff, sizeof(view));
    instruction.operation_identifier = 9U;
    RUNTIMEC_EXPECT(binrwir_instruction_get_view(
                        &instruction,
                        BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE, empty_span,
                        empty_span, &view) == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(view.instruction == NULL);
    RUNTIMEC_EXPECT(view.source_logical_program_counter == 0U);

    RUNTIMEC_EXPECT(binrwir_instruction_get_view(
                        NULL, 1U, empty_span, empty_span, &view) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(view.instruction == NULL);
    RUNTIMEC_EXPECT(binrwir_instruction_get_view(
                        &instruction, 1U, empty_span, empty_span, NULL) ==
                    RUNTIMEC_STATUS_INVALID);
}

typedef struct signature_fixture {
    uint32_t read_indices[2];
    uint32_t write_indices[2];
    runtimec_string_view_t read_types[2];
    runtimec_string_view_t write_types[2];
    binrwir_alias_rule_t rules[2];
    binrwir_signature_view_t view;
} signature_fixture_t;

static runtimec_string_view_t type_literal(const char *text) {
    return runtimec_string_view_make(text, strlen(text));
}

/* 两读参 {0,4}、两写参 {1,9}，以及规则 (0,0)=MAY_ALIAS、(1,1)=MUST_ALIAS。 */
static void signature_fixture_initialize(signature_fixture_t *fixture) {
    fixture->read_indices[0] = 0U;
    fixture->read_indices[1] = 4U;
    fixture->write_indices[0] = 1U;
    fixture->write_indices[1] = 9U;
    fixture->read_types[0] = type_literal("int64");
    fixture->read_types[1] = type_literal("[]char/utf8");
    fixture->write_types[0] = type_literal("float64");
    fixture->write_types[1] = type_literal("[]int32");
    fixture->rules[0].read_parameter_index = 0U;
    fixture->rules[0].write_parameter_index = 0U;
    fixture->rules[0].policy = BINRWIR_ALIAS_MAY_ALIAS;
    fixture->rules[0].flags = BINRWIR_ALIAS_RULE_FLAG_NONE;
    fixture->rules[0].reserved_16_bits = 0U;
    fixture->rules[1].read_parameter_index = 1U;
    fixture->rules[1].write_parameter_index = 1U;
    fixture->rules[1].policy = BINRWIR_ALIAS_MUST_ALIAS;
    fixture->rules[1].flags = BINRWIR_ALIAS_RULE_FLAG_PROJECTION;
    fixture->rules[1].reserved_16_bits = 0U;
    fixture->view.read_parameter_argument_indices = fixture->read_indices;
    fixture->view.read_parameter_count = 2U;
    fixture->view.write_parameter_argument_indices = fixture->write_indices;
    fixture->view.write_parameter_count = 2U;
    fixture->view.read_parameter_types = fixture->read_types;
    fixture->view.write_parameter_types = fixture->write_types;
    fixture->view.alias_rules = fixture->rules;
    fixture->view.alias_rule_count = 2U;
}

static void test_signature_accepts_consistent_content(void) {
    signature_fixture_t fixture;
    binrwir_signature_view_t empty = {
        .read_parameter_argument_indices = NULL,
        .read_parameter_count = 0U,
        .write_parameter_argument_indices = NULL,
        .write_parameter_count = 0U,
        .read_parameter_types = NULL,
        .write_parameter_types = NULL,
        .alias_rules = NULL,
        .alias_rule_count = 0U,
    };

    signature_fixture_initialize(&fixture);
    RUNTIMEC_EXPECT(binrwir_signature_validate(&fixture.view) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_signature_validate(&empty) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_signature_validate(NULL) ==
                    RUNTIMEC_STATUS_INVALID);
}

static void test_signature_rejects_duplicate_argument_index(void) {
    uint32_t read_indices[2] = {3U, 3U};
    uint32_t write_indices[1] = {7U};
    runtimec_string_view_t read_types[2] = {
        RUNTIMEC_STRING_VIEW_LITERAL("int64"),
        RUNTIMEC_STRING_VIEW_LITERAL("int64"),
    };
    runtimec_string_view_t write_types[1] = {
        RUNTIMEC_STRING_VIEW_LITERAL("int64"),
    };
    binrwir_signature_view_t signature = {
        .read_parameter_argument_indices = read_indices,
        .read_parameter_count = 2U,
        .write_parameter_argument_indices = write_indices,
        .write_parameter_count = 1U,
        .read_parameter_types = read_types,
        .write_parameter_types = write_types,
        .alias_rules = NULL,
        .alias_rule_count = 0U,
    };

    RUNTIMEC_EXPECT(binrwir_signature_validate(&signature) ==
                    RUNTIMEC_STATUS_INVALID);

    read_indices[0] = 3U;
    read_indices[1] = 4U;
    write_indices[0] = 4U;
    RUNTIMEC_EXPECT(binrwir_signature_validate(&signature) ==
                    RUNTIMEC_STATUS_SUCCESS);

    read_indices[1] = UINT32_MAX;
    RUNTIMEC_EXPECT(binrwir_signature_validate(&signature) ==
                    RUNTIMEC_STATUS_INVALID);
}

/* 每个参数恰好一个具体 langtype：空类型、union、类型表缺失都必须失败。 */
static void test_signature_rejects_absent_parameter_type(void) {
    signature_fixture_t fixture;

    signature_fixture_initialize(&fixture);
    fixture.view.read_parameter_types = NULL;
    RUNTIMEC_EXPECT(binrwir_signature_validate(&fixture.view) ==
                    RUNTIMEC_STATUS_INVALID);

    signature_fixture_initialize(&fixture);
    fixture.view.write_parameter_types = NULL;
    RUNTIMEC_EXPECT(binrwir_signature_validate(&fixture.view) ==
                    RUNTIMEC_STATUS_INVALID);

    signature_fixture_initialize(&fixture);
    fixture.read_types[1] = runtimec_string_view_make(NULL, 0U);
    RUNTIMEC_EXPECT(binrwir_signature_validate(&fixture.view) ==
                    RUNTIMEC_STATUS_INVALID);

    signature_fixture_initialize(&fixture);
    fixture.write_types[1] = runtimec_string_view_make(NULL, 3U);
    RUNTIMEC_EXPECT(binrwir_signature_validate(&fixture.view) ==
                    RUNTIMEC_STATUS_INVALID);
}

static void test_signature_rejects_union_parameter_type(void) {
    signature_fixture_t fixture;

    signature_fixture_initialize(&fixture);
    fixture.read_types[0] = type_literal("int64|float64");
    RUNTIMEC_EXPECT(binrwir_signature_validate(&fixture.view) ==
                    RUNTIMEC_STATUS_INVALID);

    signature_fixture_initialize(&fixture);
    fixture.write_types[1] = type_literal("[]char/utf8|[]char/utf32");
    RUNTIMEC_EXPECT(binrwir_signature_validate(&fixture.view) ==
                    RUNTIMEC_STATUS_INVALID);

    /* 分隔符出现在任意位置都算 union，不能只挡住第一种。 */
    signature_fixture_initialize(&fixture);
    fixture.read_types[1] = type_literal("|[4]int32");
    RUNTIMEC_EXPECT(binrwir_signature_validate(&fixture.view) ==
                    RUNTIMEC_STATUS_INVALID);
}

static void test_signature_rejects_out_of_range_alias_rule(void) {
    signature_fixture_t fixture;

    signature_fixture_initialize(&fixture);
    fixture.rules[0].read_parameter_index = 2U;
    RUNTIMEC_EXPECT(binrwir_signature_validate(&fixture.view) ==
                    RUNTIMEC_STATUS_INVALID);

    signature_fixture_initialize(&fixture);
    fixture.rules[1].write_parameter_index = 2U;
    RUNTIMEC_EXPECT(binrwir_signature_validate(&fixture.view) ==
                    RUNTIMEC_STATUS_INVALID);
}

static void test_signature_rejects_invalid_alias_rule_content(void) {
    signature_fixture_t fixture;

    signature_fixture_initialize(&fixture);
    fixture.rules[0].policy = BINRWIR_ALIAS_POLICY_INVALID;
    RUNTIMEC_EXPECT(binrwir_signature_validate(&fixture.view) ==
                    RUNTIMEC_STATUS_INVALID);

    signature_fixture_initialize(&fixture);
    fixture.rules[0].flags = 0x80U;
    RUNTIMEC_EXPECT(binrwir_signature_validate(&fixture.view) ==
                    RUNTIMEC_STATUS_INVALID);

    signature_fixture_initialize(&fixture);
    fixture.rules[0].reserved_16_bits = 1U;
    RUNTIMEC_EXPECT(binrwir_signature_validate(&fixture.view) ==
                    RUNTIMEC_STATUS_INVALID);

    /* 同一对 (read, write) 不得重复声明。 */
    signature_fixture_initialize(&fixture);
    fixture.rules[1].read_parameter_index = 0U;
    fixture.rules[1].write_parameter_index = 0U;
    RUNTIMEC_EXPECT(binrwir_signature_validate(&fixture.view) ==
                    RUNTIMEC_STATUS_INVALID);
}

static void test_signature_alias_policy_lookup(void) {
    signature_fixture_t fixture;
    const binrwir_signature_view_t *signature;
    binrwir_alias_policy_t policy;

    signature_fixture_initialize(&fixture);
    signature = &fixture.view;

    policy = BINRWIR_ALIAS_NO_ALIAS;
    RUNTIMEC_EXPECT(binrwir_signature_get_alias_policy(signature, 0U, 0U,
                                                       &policy) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(policy == BINRWIR_ALIAS_MAY_ALIAS);

    policy = BINRWIR_ALIAS_NO_ALIAS;
    RUNTIMEC_EXPECT(binrwir_signature_get_alias_policy(signature, 1U, 1U,
                                                       &policy) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(policy == BINRWIR_ALIAS_MUST_ALIAS);

    /* 未声明的 (read, write) 关系不得被当作不重叠：UNSUPPORTED 且策略保持 INVALID。 */
    policy = BINRWIR_ALIAS_NO_ALIAS;
    RUNTIMEC_EXPECT(binrwir_signature_get_alias_policy(signature, 0U, 1U,
                                                       &policy) ==
                    RUNTIMEC_STATUS_UNSUPPORTED);
    RUNTIMEC_EXPECT(policy == BINRWIR_ALIAS_POLICY_INVALID);

    policy = BINRWIR_ALIAS_NO_ALIAS;
    RUNTIMEC_EXPECT(binrwir_signature_get_alias_policy(signature, 3U, 0U,
                                                       &policy) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(policy == BINRWIR_ALIAS_POLICY_INVALID);

    policy = BINRWIR_ALIAS_NO_ALIAS;
    RUNTIMEC_EXPECT(binrwir_signature_get_alias_policy(signature, 0U, 5U,
                                                       &policy) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(policy == BINRWIR_ALIAS_POLICY_INVALID);

    /* 签名本身不合法时，查询传回同一失败状态，而不是把非法签名当成「无答案」。 */
    policy = BINRWIR_ALIAS_NO_ALIAS;
    fixture.rules[0].policy = BINRWIR_ALIAS_POLICY_INVALID;
    RUNTIMEC_EXPECT(binrwir_signature_get_alias_policy(signature, 1U, 1U,
                                                       &policy) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(policy == BINRWIR_ALIAS_POLICY_INVALID);
    fixture.rules[0].policy = BINRWIR_ALIAS_MAY_ALIAS;

    policy = BINRWIR_ALIAS_NO_ALIAS;
    RUNTIMEC_EXPECT(binrwir_signature_get_alias_policy(NULL, 0U, 0U, &policy) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(policy == BINRWIR_ALIAS_POLICY_INVALID);

    RUNTIMEC_EXPECT(binrwir_signature_get_alias_policy(signature, 0U, 0U,
                                                       NULL) ==
                    RUNTIMEC_STATUS_INVALID);
}

int main(void) {
    test_instruction_request_accepts_consistent_content();
    test_instruction_request_rejects_absent_identity();
    test_instruction_request_rejects_inconsistent_spans();
    test_instruction_view_round_trip();
    test_instruction_view_failure_clears_output();
    test_signature_accepts_consistent_content();
    test_signature_rejects_duplicate_argument_index();
    test_signature_rejects_absent_parameter_type();
    test_signature_rejects_union_parameter_type();
    test_signature_rejects_out_of_range_alias_rule();
    test_signature_rejects_invalid_alias_rule_content();
    test_signature_alias_policy_lookup();
    if (failures != 0) {
        fprintf(stderr, "binrwir_value_types_test: %d failure(s)\n", failures);
        return 1;
    }
    return 0;
}
