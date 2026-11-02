/*
 * 组件：vmbin/frame 的实现
 * 作用：安全初始化和重置 Frame，其余入口保持失败关闭。
 * 约束：Frame 不拥有 Program、Cell 或 Binding 存储。
 */

#include "vmbin/frame.h"

#include <string.h>

runtimec_status_t vmbin_frame_initialize(
    vmbin_frame_t *frame, const vmbin_program_t *program,
    vmbin_frame_t *parent, vmbin_cell_t *cells, size_t cell_count,
    vmbin_binding_t *bindings, size_t binding_count,
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
    frame->flags = VMBIN_FRAME_FLAG_INITIALIZED;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_frame_reset(vmbin_frame_t *frame) {
    if (frame == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    memset(frame, 0, sizeof(*frame));
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_frame_destroy(vmbin_frame_t *frame) {
    return vmbin_frame_reset(frame);
}

runtimec_status_t vmbin_frame_lookup_cell(vmbin_frame_t *frame,
                                          uint32_t cell_identifier,
                                          vmbin_cell_t **output_cell) {
    if (output_cell != NULL) {
        *output_cell = NULL;
    }
    if (frame == NULL || cell_identifier == VMBIN_CELL_IDENTIFIER_NONE ||
        output_cell == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmbin_frame_lookup_binding(
    vmbin_frame_t *frame, uint32_t register_identifier,
    vmbin_binding_t **output_binding) {
    if (output_binding != NULL) {
        *output_binding = NULL;
    }
    if (frame == NULL ||
        register_identifier == VMBIN_REGISTER_IDENTIFIER_NONE ||
        output_binding == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmbin_frame_bind_register(
    vmbin_frame_t *frame, uint32_t register_identifier,
    uint32_t cell_identifier,
    vmbin_binding_access_t access) {
    if (frame == NULL ||
        register_identifier == VMBIN_REGISTER_IDENTIFIER_NONE ||
        cell_identifier == VMBIN_CELL_IDENTIFIER_NONE ||
        (access != VMBIN_BINDING_ACCESS_READ &&
         access != VMBIN_BINDING_ACCESS_WRITE)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmbin_frame_materialize_register(
    vmbin_frame_t *frame, uint32_t register_identifier,
    vmbin_value_slot_t *output_value) {
    if (output_value != NULL) {
        memset(output_value, 0, sizeof(*output_value));
    }
    if (frame == NULL ||
        register_identifier == VMBIN_REGISTER_IDENTIFIER_NONE ||
        output_value == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
