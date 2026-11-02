/*
 * 组件：vmbin/interpreter 内部派生 dispatch 的失败桩
 * 作用：保留派生能力边界，但不实现处理函数选择或执行语义。
 */

#include "vmbin/interpreter_dispatch.h"

#include <string.h>

runtimec_status_t vmbin_interpreter_dispatch_table_create(
    const vmbin_program_t *program,
    const runtimec_allocator_t *allocator,
    vmbin_interpreter_dispatch_table_t **output_table) {
    if (output_table != NULL) {
        *output_table = NULL;
    }
    if (program == NULL || allocator == NULL || output_table == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmbin_interpreter_dispatch_table_destroy(
    vmbin_interpreter_dispatch_table_t *table) {
    if (table == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmbin_interpreter_dispatch_table_get(
    const vmbin_interpreter_dispatch_table_t *table,
    uint32_t instruction_identifier,
    vmbin_interpreter_dispatch_entry_t *output_entry) {
    if (output_entry != NULL) {
        memset(output_entry, 0, sizeof(*output_entry));
        output_entry->handler_identifier = UINT16_MAX;
    }
    if (table == NULL || instruction_identifier == VMBIN_IDENTIFIER_NONE ||
        output_entry == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
