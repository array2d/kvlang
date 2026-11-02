/*
 * 组件：binrwir/instruction 的实现
 * 作用：地址化指令构建请求校验与只读视图生成；当前明确返回未实现。
 */

#include "binrwir/instruction.h"

#include <string.h>

static int binrwir_argument_span_is_invalid(
    const binrwir_argument_span_t *arguments) {
    size_t argument_ordinal;

    for (argument_ordinal = 0U; argument_ordinal < arguments->count;
         ++argument_ordinal) {
        if (arguments->data[argument_ordinal].argument_index ==
                BINRWIR_ARGUMENT_INDEX_NONE ||
            arguments->data[argument_ordinal].body_address ==
                BINRWIR_BODY_ADDRESS_NONE ||
            arguments->data[argument_ordinal].reserved_32_bits != 0U) {
            return 1;
        }
    }
    return 0;
}

runtimec_status_t binrwir_instruction_validate_build_request(
    const binrwir_instruction_build_request_t *request) {
    if (request == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (request->operation_identifier == BINRWIR_OPERATION_IDENTIFIER_NONE ||
        request->read_arguments.count > UINT32_MAX ||
        request->write_arguments.count > UINT32_MAX ||
        request->read_arguments.count >
            UINT32_MAX - request->write_arguments.count ||
        (request->read_arguments.count != 0U &&
         request->read_arguments.data == NULL) ||
        (request->write_arguments.count != 0U &&
         request->write_arguments.data == NULL) ||
        binrwir_argument_span_is_invalid(&request->read_arguments) ||
        binrwir_argument_span_is_invalid(&request->write_arguments)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
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
    if (instruction == NULL || output_view == NULL ||
        instruction->operation_identifier == BINRWIR_OPERATION_IDENTIFIER_NONE ||
        read_arguments.count > UINT32_MAX ||
        write_arguments.count > UINT32_MAX ||
        read_arguments.count > UINT32_MAX - write_arguments.count ||
        (read_arguments.count != 0U && read_arguments.data == NULL) ||
        (write_arguments.count != 0U && write_arguments.data == NULL) ||
        binrwir_argument_span_is_invalid(&read_arguments) ||
        binrwir_argument_span_is_invalid(&write_arguments)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    (void)source_logical_program_counter;
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
