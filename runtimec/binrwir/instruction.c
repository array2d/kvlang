/*
 * 组件：binrwir/instruction 的实现
 * 作用：构建请求校验与只读视图生成；当前明确返回未实现。
 */

#include "binrwir/instruction.h"

#include <string.h>

runtimec_status_t binrwir_instruction_validate_build_request(
    const binrwir_instruction_build_request_t *request) {
    if (request == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (request->operation_code == BINRWIR_OPERATION_CODE_NONE ||
        request->form < BINRWIR_INSTRUCTION_FORM_NARROW ||
        request->form > BINRWIR_INSTRUCTION_FORM_WIDE ||
        (request->reads.count != 0U && request->reads.data == NULL) ||
        (request->writes.count != 0U && request->writes.data == NULL)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_instruction_get_view(
    const binrwir_instruction_t *instruction,
    uint32_t source_logical_program_counter,
    const binrwir_reference_t *references, size_t reference_count,
    binrwir_instruction_view_t *output_view) {
    (void)instruction;
    (void)source_logical_program_counter;
    (void)references;
    (void)reference_count;
    if (output_view != NULL) {
        memset(output_view, 0, sizeof(*output_view));
    }
    if (instruction == NULL || output_view == NULL ||
        (reference_count != 0U && references == NULL)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
