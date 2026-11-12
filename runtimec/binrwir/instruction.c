
#include "binrwir/instruction.h"

#include <string.h>

#include "binrwir/execution_exit.h"

static int binrwir_argument_span_is_invalid(
    const binrwir_argument_span_t *arguments) {
    size_t argument_ordinal;

    for (argument_ordinal = 0U; argument_ordinal < arguments->count;
         ++argument_ordinal) {
        if (binrwir_argument_validate(&arguments->data[argument_ordinal]) !=
            RUNTIMEC_STATUS_SUCCESS) {
            return 1;
        }
    }
    return 0;
}

static runtimec_status_t binrwir_instruction_validate_content(
    uint32_t source_logical_program_counter,
    binrwir_operation_identifier_t operation_identifier,
    binrwir_argument_span_t read_arguments,
    binrwir_argument_span_t write_arguments) {
    
    if (operation_identifier == BINRWIR_OPERATION_IDENTIFIER_FOREIGN &&
        (read_arguments.count != 0U || write_arguments.count != 0U)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    
    if (operation_identifier == 0U ||
        operation_identifier == BINRWIR_OPERATION_IDENTIFIER_NONE ||
        source_logical_program_counter ==
            BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE ||
        read_arguments.count > UINT32_MAX ||
        write_arguments.count > UINT32_MAX ||
        read_arguments.count > UINT32_MAX - write_arguments.count ||
        (read_arguments.count != 0U && read_arguments.data == NULL) ||
        (write_arguments.count != 0U && write_arguments.data == NULL) ||
        binrwir_argument_span_is_invalid(&read_arguments) ||
        binrwir_argument_span_is_invalid(&write_arguments)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_instruction_validate_build_request(
    const binrwir_instruction_build_request_t *request) {
    if (request == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return binrwir_instruction_validate_content(
        request->source_logical_program_counter, request->operation_identifier,
        request->read_arguments, request->write_arguments);
}

runtimec_status_t binrwir_instruction_get_view(
    const binrwir_instruction_t *instruction,
    uint32_t source_logical_program_counter,
    binrwir_argument_span_t read_arguments,
    binrwir_argument_span_t write_arguments,
    binrwir_instruction_view_t *output_view) {
    if (output_view != NULL) {
        memset(output_view, 0, sizeof(*output_view));
    }
    if (instruction == NULL || output_view == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    {
        runtimec_status_t status = binrwir_instruction_validate_content(
            source_logical_program_counter, instruction->operation_identifier,
            read_arguments, write_arguments);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
    }
    output_view->instruction = instruction;
    output_view->source_logical_program_counter = source_logical_program_counter;
    output_view->reserved_32_bits = 0U;
    output_view->read_arguments = read_arguments;
    output_view->write_arguments = write_arguments;
    return RUNTIMEC_STATUS_SUCCESS;
}
