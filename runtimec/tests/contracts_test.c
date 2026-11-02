/* Freeze only compact cross-component contracts selected by the design. */

#include <stddef.h>
#include <string.h>

#include <kvlang/runtimec.h>

#include "binrwir/execution_binding.h"
#include "binrwir/execution_exit.h"
#include "binrwir/instruction.h"
#include "binrwir/program.h"
#include "binrwir/reference.h"
#include "binrwir/signature.h"
#include "binrwir/value.h"

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

_Static_assert(sizeof(binrwir_reference_t) == 4U,
               "BinRWIR references must remain four bytes");
_Static_assert(sizeof(binrwir_alias_rule_t) == 8U,
               "alias rules must remain eight bytes");
_Static_assert(sizeof(binrwir_instruction_t) == 16U,
               "BinRWIR instructions must remain sixteen bytes");
_Static_assert(sizeof(binrwir_register_descriptor_t) == 16U,
               "register descriptors must remain sixteen bytes");
_Static_assert(sizeof(binrwir_literal_descriptor_t) == 16U,
               "literal descriptors must remain sixteen bytes");
_Static_assert(sizeof(binrwir_callsite_t) == 16U,
               "callsites must remain sixteen bytes");
_Static_assert(sizeof(binrwir_call_alias_mapping_t) == 8U,
               "call alias mappings must remain eight bytes");
_Static_assert(sizeof(binrwir_value_slot_t) == 32U,
               "value slots must remain thirty-two bytes");
_Static_assert(sizeof(binrwir_cell_t) == 64U,
               "canonical cells must remain sixty-four bytes");
_Static_assert(sizeof(binrwir_execution_exit_t) == 48U,
               "structured exits must remain forty-eight bytes");

static int plain_data_copy_round_trip(void) {
    const binrwir_value_slot_t initial_value = {0};
    const binrwir_execution_exit_t initial_exit = {0};
    binrwir_value_slot_t copied_value;
    binrwir_execution_exit_t copied_exit;

    memcpy(&copied_value, &initial_value, sizeof(copied_value));
    memcpy(&copied_exit, &initial_exit, sizeof(copied_exit));
    return memcmp(&copied_value, &initial_value, sizeof(copied_value)) == 0 &&
                   memcmp(&copied_exit, &initial_exit, sizeof(copied_exit)) == 0
               ? 0
               : 1;
}

int main(void) {
    kvlang_runtime_t *opaque_runtime = NULL;
    (void)opaque_runtime;
    return plain_data_copy_round_trip();
}
