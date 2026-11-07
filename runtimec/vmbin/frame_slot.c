/*
 * 组件：vmbin/frame_slot 的实现
 * 作用：把参数项换算成绑定表的唯一槽号。
 * 约束：槽号超出 16 位或参数表本身不合法时一律 INVALID，不做截断。
 */

#include "frame_slot.h"

#include <string.h>

static runtimec_status_t vmbin_frame_slot_counts(
    const binrwir_binfunc_t *program, size_t *output_read_count,
    size_t *output_write_count) {
    binrwir_binfunc_view_t view;

    *output_read_count = 0U;
    *output_write_count = 0U;
    if (program == NULL ||
        binrwir_binfunc_get_view(program, &view) != RUNTIMEC_STATUS_SUCCESS) {
        return RUNTIMEC_STATUS_INVALID;
    }
    *output_read_count = view.signature.read_parameter_count;
    *output_write_count = view.signature.write_parameter_count;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_frame_slot_capacity(const binrwir_binfunc_t *program,
                                            size_t *output_capacity) {
    size_t read_count = 0U;
    size_t write_count = 0U;
    size_t symbol_count = 0U;
    runtimec_status_t status;

    if (output_capacity != NULL) {
        *output_capacity = 0U;
    }
    if (output_capacity == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    status = vmbin_frame_slot_counts(program, &read_count, &write_count);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    /* 局部符号排在两个参数区之后，容量必须把它们算进去，否则一绑就越界。 */
    if (binrwir_binfunc_get_symbol_count(program, &symbol_count) !=
            RUNTIMEC_STATUS_SUCCESS ||
        read_count > UINT16_MAX ||
        write_count > (size_t)UINT16_MAX - read_count ||
        symbol_count > (size_t)UINT16_MAX - read_count - write_count) {
        return RUNTIMEC_STATUS_INVALID;
    }
    *output_capacity = read_count + write_count + symbol_count;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_frame_slot_of_parameter(
    const binrwir_binfunc_t *program, binrwir_parameter_direction_t direction,
    uint32_t parameter_index, uint16_t *output_slot) {
    size_t read_count = 0U;
    size_t write_count = 0U;
    size_t slot = 0U;
    runtimec_status_t status;

    if (output_slot != NULL) {
        *output_slot = BINRWIR_FRAME_BINDING_SLOT_NONE;
    }
    if (output_slot == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    status = vmbin_frame_slot_counts(program, &read_count, &write_count);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (direction == BINRWIR_PARAMETER_DIRECTION_READ) {
        if ((size_t)parameter_index >= read_count) {
            return RUNTIMEC_STATUS_INVALID;
        }
        slot = parameter_index;
    } else if (direction == BINRWIR_PARAMETER_DIRECTION_WRITE) {
        if ((size_t)parameter_index >= write_count) {
            return RUNTIMEC_STATUS_INVALID;
        }
        slot = read_count + parameter_index;
    } else {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (slot > UINT16_MAX) {
        return RUNTIMEC_STATUS_INVALID;
    }
    *output_slot = (uint16_t)slot;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_frame_slot_of_local_symbol(
    const binrwir_binfunc_t *program, uint16_t symbol_index,
    uint16_t *output_slot) {
    size_t read_count = 0U;
    size_t write_count = 0U;
    size_t slot = 0U;
    runtimec_status_t status;

    if (output_slot != NULL) {
        *output_slot = BINRWIR_FRAME_BINDING_SLOT_NONE;
    }
    if (program == NULL || output_slot == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    status = vmbin_frame_slot_counts(program, &read_count, &write_count);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    slot = read_count + write_count + (size_t)symbol_index;
    if (slot > UINT16_MAX) {
        return RUNTIMEC_STATUS_INVALID;
    }
    *output_slot = (uint16_t)slot;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_frame_slot_of_argument(
    const binrwir_binfunc_t *program, const binrwir_argument_t *argument,
    uint16_t *output_slot) {
    size_t read_count = 0U;
    size_t write_count = 0U;
    size_t slot = 0U;
    runtimec_status_t status;

    if (output_slot != NULL) {
        *output_slot = BINRWIR_FRAME_BINDING_SLOT_NONE;
    }
    if (program == NULL || argument == NULL || output_slot == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    status = vmbin_frame_slot_counts(program, &read_count, &write_count);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    switch (argument->reference_kind) {
    case BINRWIR_REFERENCE_PARAMETER_READ:
        return vmbin_frame_slot_of_parameter(program,
                                             BINRWIR_PARAMETER_DIRECTION_READ,
                                             argument->slot, output_slot);
    case BINRWIR_REFERENCE_PARAMETER_WRITE:
        return vmbin_frame_slot_of_parameter(program,
                                             BINRWIR_PARAMETER_DIRECTION_WRITE,
                                             argument->slot, output_slot);
    case BINRWIR_REFERENCE_LOCAL:
        /* 局部符号排在两个参数区之后，互不覆盖。 */
        slot = read_count + write_count + (size_t)argument->slot;
        break;
    default:
        /* LITERAL / STATIC / FUNCTION 不占绑定表。 */
        return RUNTIMEC_STATUS_INVALID;
    }
    if (slot > UINT16_MAX) {
        return RUNTIMEC_STATUS_INVALID;
    }
    *output_slot = (uint16_t)slot;
    return RUNTIMEC_STATUS_SUCCESS;
}
