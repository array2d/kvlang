/*
 * 组件：binrwir/execution_frame 的实现
 * 作用：安全初始化和重置 Frame，其余入口保持失败关闭。
 * 约束：Frame 不拥有 Program、Cell 或 Binding 存储。
 */

#include "binrwir/execution_frame.h"

#include <string.h>

runtimec_status_t binrwir_execution_frame_initialize(
    binrwir_execution_frame_t *frame, const binrwir_program_t *program,
    binrwir_execution_frame_t *parent, binrwir_cell_t *cells,
    size_t cell_count,
    binrwir_binding_t *bindings, size_t binding_count,
    uint64_t snapshot_generation, uint64_t program_generation,
    uint32_t logical_program_counter) {
    if (frame == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    memset(frame, 0, sizeof(*frame));
    if (program == NULL ||
        (cell_count != 0U && cells == NULL) ||
        (binding_count != 0U && bindings == NULL)) {
        return RUNTIMEC_STATUS_INVALID;
    }

    frame->program = program;
    frame->parent = parent;
    frame->cells = cells;
    frame->cell_count = cell_count;
    frame->bindings = bindings;
    frame->binding_count = binding_count;
    frame->snapshot_generation = snapshot_generation;
    frame->program_generation = program_generation;
    frame->logical_program_counter = logical_program_counter;
    frame->flags = BINRWIR_EXECUTION_FRAME_FLAG_INITIALIZED;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_execution_frame_reset(
    binrwir_execution_frame_t *frame) {
    if (frame == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    memset(frame, 0, sizeof(*frame));
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_execution_frame_destroy(
    binrwir_execution_frame_t *frame) {
    return binrwir_execution_frame_reset(frame);
}

runtimec_status_t binrwir_execution_frame_lookup_cell(
    binrwir_execution_frame_t *frame, uint32_t cell_identifier,
    binrwir_cell_t **output_cell) {
    if (output_cell != NULL) {
        *output_cell = NULL;
    }
    if (frame == NULL || cell_identifier == BINRWIR_CELL_IDENTIFIER_NONE ||
        output_cell == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_execution_frame_lookup_binding(
    binrwir_execution_frame_t *frame, uint32_t register_identifier,
    binrwir_binding_t **output_binding) {
    if (output_binding != NULL) {
        *output_binding = NULL;
    }
    if (frame == NULL ||
        register_identifier == BINRWIR_REGISTER_IDENTIFIER_NONE ||
        output_binding == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_execution_frame_bind_register(
    binrwir_execution_frame_t *frame, uint32_t register_identifier,
    uint32_t cell_identifier,
    binrwir_binding_access_t access) {
    if (frame == NULL ||
        register_identifier == BINRWIR_REGISTER_IDENTIFIER_NONE ||
        cell_identifier == BINRWIR_CELL_IDENTIFIER_NONE ||
        (access != BINRWIR_BINDING_ACCESS_READ &&
         access != BINRWIR_BINDING_ACCESS_WRITE)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_execution_frame_materialize_register(
    binrwir_execution_frame_t *frame, uint32_t register_identifier,
    binrwir_value_slot_t *output_value) {
    if (output_value != NULL) {
        memset(output_value, 0, sizeof(*output_value));
    }
    if (frame == NULL ||
        register_identifier == BINRWIR_REGISTER_IDENTIFIER_NONE ||
        output_value == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
