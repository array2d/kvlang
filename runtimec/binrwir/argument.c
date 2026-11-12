
#include "binrwir/argument.h"

#include <stddef.h>
#include <string.h>

static int binrwir_reference_kind_is_known(uint8_t reference_kind) {
    return reference_kind == BINRWIR_REFERENCE_PARAMETER_READ ||
           reference_kind == BINRWIR_REFERENCE_PARAMETER_WRITE ||
           reference_kind == BINRWIR_REFERENCE_LOCAL ||
           reference_kind == BINRWIR_REFERENCE_STATIC ||
           reference_kind == BINRWIR_REFERENCE_LITERAL ||
           reference_kind == BINRWIR_REFERENCE_FUNCTION;
}

static int binrwir_argument_shape_is_invalid(
    uint8_t reference_kind, uint16_t slot, uint64_t payload) {
    switch (reference_kind) {
    case BINRWIR_REFERENCE_PARAMETER_READ:
    case BINRWIR_REFERENCE_PARAMETER_WRITE:
    case BINRWIR_REFERENCE_LOCAL:
    case BINRWIR_REFERENCE_FUNCTION:
        return payload != 0U;
    case BINRWIR_REFERENCE_STATIC:
        
        return 0;
    case BINRWIR_REFERENCE_LITERAL:
        return slot != 0U;
    default:
        return 1;
    }
}

runtimec_status_t binrwir_argument_create(
    uint32_t argument_index, binrwir_reference_kind_t reference_kind,
    uint16_t slot, uint64_t payload,
    binrwir_argument_t *output_argument) {
    if (output_argument == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    memset(output_argument, 0, sizeof(*output_argument));
    if (argument_index == BINRWIR_ARGUMENT_INDEX_NONE ||
        !binrwir_reference_kind_is_known((uint8_t)reference_kind) ||
        binrwir_argument_shape_is_invalid((uint8_t)reference_kind, slot,
                                          payload)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    output_argument->argument_index = argument_index;
    output_argument->reference_kind = (uint8_t)reference_kind;
    output_argument->dereference = 0U;
    output_argument->slot = slot;
    output_argument->payload = payload;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_argument_validate(
    const binrwir_argument_t *argument) {
    if (argument == NULL ||
        argument->argument_index == BINRWIR_ARGUMENT_INDEX_NONE ||
        argument->dereference > 1U ||
        !binrwir_reference_kind_is_known(argument->reference_kind) ||
        binrwir_argument_shape_is_invalid(argument->reference_kind,
                                          argument->slot,
                                          argument->payload)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}
