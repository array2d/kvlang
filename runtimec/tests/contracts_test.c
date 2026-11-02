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

_Static_assert(RUNTIMEC_HOST_SERVICES_BINARY_INTERFACE_VERSION == 2U,
               "host services binary interface must remain version 2");
_Static_assert(sizeof(runtimec_value_body_address_t) == 8U,
               "V.body addresses must remain 64 bits");
_Static_assert(sizeof(binrwir_argument_t) == 16U,
               "BinRWIR arguments must remain sixteen bytes");
_Static_assert(offsetof(binrwir_argument_t, argument_index) == 0U,
               "BinRWIR argidx offset changed");
_Static_assert(offsetof(binrwir_argument_t, body_address) == 8U,
               "BinRWIR argaddr offset changed");
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
        .body_address = UINT64_C(0x1000),
        .argument_index = 7U,
        .reserved_32_bits = 0U,
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

static void test_addressed_argument_failure_shape(void) {
    binrwir_argument_t argument = {
        .body_address = UINT64_MAX,
        .argument_index = UINT32_MAX,
        .reserved_32_bits = UINT32_MAX,
    };

    RUNTIMEC_EXPECT(binrwir_argument_create(
        3U, BINRWIR_BODY_ADDRESS_NONE, &argument) == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(argument.body_address == BINRWIR_BODY_ADDRESS_NONE);
    RUNTIMEC_EXPECT(argument.argument_index == 0U);
    RUNTIMEC_EXPECT(argument.reserved_32_bits == 0U);
    RUNTIMEC_EXPECT(binrwir_argument_validate(&argument) ==
                    RUNTIMEC_STATUS_INVALID);

    RUNTIMEC_EXPECT(binrwir_argument_create(
        BINRWIR_ARGUMENT_INDEX_NONE, UINT64_C(0x1000), &argument) ==
                    RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(argument.argument_index == 0U);
    RUNTIMEC_EXPECT(argument.reserved_32_bits == 0U);
    RUNTIMEC_EXPECT(argument.body_address == BINRWIR_BODY_ADDRESS_NONE);

    argument.body_address = UINT64_C(0x1000);
    argument.argument_index = BINRWIR_ARGUMENT_INDEX_NONE;
    argument.reserved_32_bits = 0U;
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

static void test_host_services_version(void) {
    runtimec_host_services_t services;

    runtimec_host_services_initialize(&services);
    RUNTIMEC_EXPECT(services.binary_interface_version == 2U);
    services.binary_interface_version = 1U;
    RUNTIMEC_EXPECT(!runtimec_host_services_is_valid(&services));
}

int main(void) {
    kvlang_runtime_t *opaque_runtime = NULL;

    (void)opaque_runtime;
    test_plain_data_copy_round_trip();
    test_addressed_argument_failure_shape();
    test_instruction_argument_count_overflow();
    test_host_services_version();
    return failures == 0 ? 0 : 1;
}
