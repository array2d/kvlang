/* Freeze only compact cross-component contracts selected by the design. */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kvlang/runtimec.h>

#include "binrwir/argument.h"
#include "binrwir/execution_exit.h"
#include "binrwir/execution_frame.h"
#include "binrwir/instruction.h"
#include "binrwir/program.h"
#include "binrwir/signature.h"
#include "contract/host_services.h"
#include "kvspace/xvalue_head.h"

_Static_assert(RUNTIMEC_STATUS_SUCCESS == 0, "success must remain zero");
_Static_assert(RUNTIMEC_STATUS_YIELD == 1, "yield status changed");
_Static_assert(RUNTIMEC_STATUS_WAIT == 2, "wait status changed");
_Static_assert(RUNTIMEC_STATUS_UNSUPPORTED == 3, "unsupported status changed");
_Static_assert(RUNTIMEC_STATUS_NOT_IMPLEMENTED == 4,
               "not-implemented status changed");
_Static_assert(RUNTIMEC_STATUS_INVALID == 5, "invalid status changed");
_Static_assert(RUNTIMEC_STATUS_OUT_OF_MEMORY == 6,
               "out-of-memory status changed");
_Static_assert(RUNTIMEC_STATUS_ERROR == 7, "error status changed");
_Static_assert(RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY == 8,
               "insufficient-capacity status changed");

/* 版本 pin：接口每次变更都必须在这里显式改一次，其余断言从宏推导。 */
_Static_assert(RUNTIMEC_HOST_SERVICES_BINARY_INTERFACE_VERSION == 5U,
               "host services binary interface must remain version 5");
_Static_assert(RUNTIMEC_XVALUE_BODY_OFFSET_BYTES == 8U,
               "V.body must remain eight bytes after the XValue base");
_Static_assert(sizeof(runtimec_xvalue_address_t) == 8U,
               "XValue addresses must remain 64 bits");
_Static_assert(sizeof(runtimec_value_body_address_t) == 8U,
               "V.body addresses must remain 64 bits");
_Static_assert(sizeof(binrwir_argument_t) == 16U,
               "BinRWIR arguments must remain sixteen bytes");
_Static_assert(offsetof(binrwir_argument_t, argument_index) == 0U,
               "BinRWIR argidx offset changed");
_Static_assert(offsetof(binrwir_argument_t, reference_kind) == 4U,
               "BinRWIR reference kind offset changed");
_Static_assert(offsetof(binrwir_argument_t, reserved_8_bits) == 5U,
               "BinRWIR reserved byte offset changed");
_Static_assert(offsetof(binrwir_argument_t, slot) == 6U,
               "BinRWIR slot offset changed");
_Static_assert(offsetof(binrwir_argument_t, payload) == 8U,
               "BinRWIR payload offset changed");
_Static_assert(BINRWIR_BINARY_INTERFACE_GENERATION == 4U,
               "BinRWIR binary interface generation must be 4");
_Static_assert(sizeof(binrwir_alias_rule_t) == 12U,
               "alias rules must remain twelve bytes");
_Static_assert(sizeof(binrwir_instruction_t) == 4U,
               "BinRWIR instruction heads must contain only opid");
_Static_assert(offsetof(binrwir_instruction_t, operation_identifier) == 0U,
               "BinRWIR opid offset changed");
_Static_assert(sizeof(binrwir_program_identity_t) == 32U,
               "program identities must remain thirty-two bytes");
_Static_assert(sizeof(binrwir_execution_exit_t) == 24U,
               "structured exits must contain only control state");

static int failures;

#define RUNTIMEC_EXPECT(condition)                                                \
    do {                                                                          \
        if (!(condition)) {                                                        \
            ++failures;                                                            \
        }                                                                          \
    } while (0)

static void test_plain_data_copy_round_trip(void) {
    const binrwir_argument_t initial_argument = {
        .argument_index = 7U,
        .reference_kind = BINRWIR_REFERENCE_STATIC,
        .reserved_8_bits = 0U,
        .slot = 0U,
        .payload = UINT64_C(0x1000),
    };
    const binrwir_execution_exit_t initial_exit = {0};
    binrwir_argument_t copied_argument;
    binrwir_execution_exit_t copied_exit;

    memcpy(&copied_argument, &initial_argument, sizeof(copied_argument));
    memcpy(&copied_exit, &initial_exit, sizeof(copied_exit));
    RUNTIMEC_EXPECT(memcmp(&copied_argument, &initial_argument,
                           sizeof(copied_argument)) == 0);
    RUNTIMEC_EXPECT(memcmp(&copied_exit, &initial_exit,
                           sizeof(copied_exit)) == 0);
}

/* 构造失败必须清空输出；只有 STATIC 的 payload 可以当 body 地址。 */
static void test_reference_argument_failure_shape(void) {
    binrwir_argument_t argument = {
        .argument_index = UINT32_MAX,
        .reference_kind = UINT8_MAX,
        .reserved_8_bits = UINT8_MAX,
        .slot = UINT16_MAX,
        .payload = UINT64_MAX,
    };

    RUNTIMEC_EXPECT(binrwir_argument_create(
        3U, BINRWIR_REFERENCE_STATIC, 0U, BINRWIR_BODY_ADDRESS_NONE,
        &argument) == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(argument.argument_index == 0U);
    RUNTIMEC_EXPECT(argument.reference_kind == BINRWIR_REFERENCE_INVALID);
    RUNTIMEC_EXPECT(argument.reserved_8_bits == 0U);
    RUNTIMEC_EXPECT(argument.slot == 0U);
    RUNTIMEC_EXPECT(argument.payload == BINRWIR_BODY_ADDRESS_NONE);

    RUNTIMEC_EXPECT(binrwir_argument_create(
        BINRWIR_ARGUMENT_INDEX_NONE, BINRWIR_REFERENCE_PARAMETER_READ, 0U, 0U,
        &argument) == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(argument.argument_index == 0U);

    /* 参数引用带 payload 视为把 payload 当地址用，必须拒绝。 */
    RUNTIMEC_EXPECT(binrwir_argument_create(
        3U, BINRWIR_REFERENCE_PARAMETER_READ, 0U, UINT64_C(0x1008),
        &argument) == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(argument.argument_index == 0U);

    RUNTIMEC_EXPECT(binrwir_argument_create(
        3U, BINRWIR_REFERENCE_PARAMETER_READ, 1U, 0U, &argument) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_argument_validate(&argument) ==
                    RUNTIMEC_STATUS_SUCCESS);

    argument.argument_index = BINRWIR_ARGUMENT_INDEX_NONE;
    RUNTIMEC_EXPECT(binrwir_argument_validate(&argument) ==
                    RUNTIMEC_STATUS_INVALID);

    argument.argument_index = 3U;
    argument.reference_kind = BINRWIR_REFERENCE_INVALID;
    RUNTIMEC_EXPECT(binrwir_argument_validate(&argument) ==
                    RUNTIMEC_STATUS_INVALID);

    argument.reference_kind = BINRWIR_REFERENCE_STATIC;
    argument.slot = 0U;
    argument.payload = UINT64_C(0x1008);
    RUNTIMEC_EXPECT(binrwir_argument_validate(&argument) ==
                    RUNTIMEC_STATUS_SUCCESS);

    argument.reserved_8_bits = 1U;
    RUNTIMEC_EXPECT(binrwir_argument_validate(&argument) ==
                    RUNTIMEC_STATUS_INVALID);
}

static void test_instruction_argument_count_overflow(void) {
    binrwir_instruction_build_request_t request = {
        .operation_identifier = 1U,
        .read_arguments = {.data = NULL, .count = UINT32_MAX},
        .write_arguments = {.data = NULL, .count = 1U},
    };

    RUNTIMEC_EXPECT(binrwir_instruction_validate_build_request(&request) ==
                    RUNTIMEC_STATUS_INVALID);
}

/* V.body 地址推导是全系统唯一的 +8：零基址与溢出都必须失败，不回绕。 */
static void test_xvalue_body_address(void) {
    RUNTIMEC_EXPECT(runtimec_xvalue_body_address(RUNTIMEC_XVALUE_ADDRESS_NONE) ==
                    RUNTIMEC_VALUE_BODY_ADDRESS_NONE);
    RUNTIMEC_EXPECT(runtimec_xvalue_body_address(UINT64_C(0x1000)) ==
                    UINT64_C(0x1008));
    RUNTIMEC_EXPECT(runtimec_xvalue_body_address(UINT64_MAX) ==
                    RUNTIMEC_VALUE_BODY_ADDRESS_NONE);
    RUNTIMEC_EXPECT(runtimec_xvalue_body_address(
                        UINT64_MAX - RUNTIMEC_XVALUE_BODY_OFFSET_BYTES + 1U) ==
                    RUNTIMEC_VALUE_BODY_ADDRESS_NONE);
    /* 最大可表示基址：+8 恰好落在 UINT64_MAX，仍合法。 */
    RUNTIMEC_EXPECT(runtimec_xvalue_body_address(
                        UINT64_MAX - RUNTIMEC_XVALUE_BODY_OFFSET_BYTES) ==
                    UINT64_MAX);
}

/* 逆运算：从值体地址读回长度；零地址、下溢、坏 storetype 都返回 0。 */
static void test_value_body_length(void) {
    uint8_t storage[KVSPACE_XVALUE_HEAD_LEN + 8U + 2U];
    memset(storage, 0, sizeof(storage));
    kvspaceXvalueHead head;
    memset(&head, 0, sizeof(head));
    head.storetype = KVSPACE_XVALUE_ST_SHORT;
    kvspaceXvalueHeadSetLen(&head, 8U);
    memcpy(storage, &head, sizeof(head));
    storage[KVSPACE_XVALUE_HEAD_LEN + 8U] = 2U;
    const runtimec_value_body_address_t body =
        (runtimec_value_body_address_t)(uintptr_t)storage +
        RUNTIMEC_XVALUE_BODY_OFFSET_BYTES;
    RUNTIMEC_EXPECT(runtimec_value_body_length(body) == 8U);

    /* 零地址与下溢：不能算出长度。 */
    RUNTIMEC_EXPECT(runtimec_value_body_length(RUNTIMEC_VALUE_BODY_ADDRESS_NONE) ==
                    0U);
    RUNTIMEC_EXPECT(runtimec_value_body_length(
                        RUNTIMEC_XVALUE_BODY_OFFSET_BYTES - 1U) == 0U);

    /* storetype 不可信：地址前面不是值头。 */
    storage[6] = 0x7fU;
    RUNTIMEC_EXPECT(runtimec_value_body_length(body) == 0U);
    storage[6] = KVSPACE_XVALUE_ST_SLACK;
    RUNTIMEC_EXPECT(runtimec_value_body_length(body) == 8U);

    /* 与 +8 互逆：基址 → 值体 → 长度，长度就是值头声明的 len。 */
    const runtimec_xvalue_address_t base = (runtimec_xvalue_address_t)(uintptr_t)storage;
    RUNTIMEC_EXPECT(runtimec_value_body_length(
                        runtimec_xvalue_body_address(base)) == 8U);
}

/* 逆入口：值体地址退回首部即基址；零地址与下溢都失败，绝不回绕到高地址。 */
static void test_xvalue_address_from_value_body(void) {
    RUNTIMEC_EXPECT(runtimec_value_body_xvalue_address(
                        RUNTIMEC_VALUE_BODY_ADDRESS_NONE) ==
                    RUNTIMEC_XVALUE_ADDRESS_NONE);
    RUNTIMEC_EXPECT(runtimec_value_body_xvalue_address(
                        RUNTIMEC_XVALUE_BODY_OFFSET_BYTES - 1U) ==
                    RUNTIMEC_XVALUE_ADDRESS_NONE);
    RUNTIMEC_EXPECT(runtimec_value_body_xvalue_address(
                        RUNTIMEC_XVALUE_BODY_OFFSET_BYTES) ==
                    RUNTIMEC_XVALUE_ADDRESS_NONE);
    RUNTIMEC_EXPECT(runtimec_value_body_xvalue_address(UINT64_C(0x1008)) ==
                    UINT64_C(0x1000));

    /* 与 +8 互逆：基址 → 值体 → 基址回到原值。 */
    const runtimec_xvalue_address_t base = UINT64_C(0x2000);
    RUNTIMEC_EXPECT(runtimec_value_body_xvalue_address(
                        runtimec_xvalue_body_address(base)) == base);
}

static void test_host_services_version(void) {
    runtimec_host_services_t services;

    runtimec_host_services_initialize(&services);
    RUNTIMEC_EXPECT(services.binary_interface_version ==
                    RUNTIMEC_HOST_SERVICES_BINARY_INTERFACE_VERSION);
    services.binary_interface_version = 2U;
    RUNTIMEC_EXPECT(!runtimec_host_services_is_valid(&services));
}

int main(void) {
    kvlang_runtime_t *opaque_runtime = NULL;

    (void)opaque_runtime;
    test_plain_data_copy_round_trip();
    test_reference_argument_failure_shape();
    test_instruction_argument_count_overflow();
    test_xvalue_body_address();
    test_value_body_length();
    test_xvalue_address_from_value_body();
    test_host_services_version();
    return failures == 0 ? 0 : 1;
}
