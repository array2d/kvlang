/*
 * 组件：binrwir/binvthread_frame 的实现
 * 作用：安全初始化和重置只保存控制状态的 Frame，并借用调用入口的绑定表。
 * 约束：Frame 不拥有 Program，也不拥有绑定表；不保存 V.body 地址或内容。
 */

#include "binrwir/binvthread_frame.h"

#include <string.h>

runtimec_status_t binrwir_binvthread_frame_initialize(
    binrwir_binvthread_frame_t *frame, const binrwir_binfunc_t *program,
    binrwir_binvthread_frame_t *parent, uint64_t snapshot_generation,
    uint64_t program_generation, uint32_t logical_program_counter) {
    if (frame == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    memset(frame, 0, sizeof(*frame));
    if (program == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }

    frame->program = program;
    frame->parent = parent;
    frame->bindings = NULL;
    frame->kvspace_handle = NULL;
    frame->snapshot_generation = snapshot_generation;
    frame->program_generation = program_generation;
    frame->logical_program_counter = logical_program_counter;
    frame->flags = BINRWIR_BINVTHREAD_FRAME_FLAG_INITIALIZED;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_binvthread_frame_reset(
    binrwir_binvthread_frame_t *frame) {
    if (frame == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    memset(frame, 0, sizeof(*frame));
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_binvthread_frame_destroy(
    binrwir_binvthread_frame_t *frame) {
    return binrwir_binvthread_frame_reset(frame);
}
