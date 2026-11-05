/*
 * 组件：vmbin/frame_target_key 的实现
 * 作用：写参 → 存储绝对 key（`<frame_path>/<符号名>`）。
 * 约束：不猜、不回退：帧路径无效、符号取不到、引用种类不是 LOCAL，一律失败。
 */

#include "frame_target_key.h"

#include <string.h>

#include "binrwir/argument.h"
#include "binrwir/program.h"
#include "contract/view.h"

runtimec_status_t vmbin_frame_target_key(
    const binrwir_execution_frame_t *frame,
    const binrwir_argument_t *argument, char *buffer, size_t capacity) {
    runtimec_string_view_t name;
    runtimec_string_view_t langtype;
    size_t length;

    if (frame == NULL || frame->program == NULL || argument == NULL ||
        buffer == NULL || capacity == 0U) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (argument->reference_kind != BINRWIR_REFERENCE_LOCAL ||
        !runtimec_key_view_is_valid(frame->frame_path) ||
        frame->frame_path.size == 0U ||
        binrwir_program_get_symbol(frame->program, argument->slot, &name,
                                   &langtype) != RUNTIMEC_STATUS_SUCCESS ||
        name.size == 0U) {
        return RUNTIMEC_STATUS_INVALID;
    }
    length = frame->frame_path.size + 1U + name.size;
    if (length + 1U > capacity) {
        return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
    }
    memcpy(buffer, frame->frame_path.data, frame->frame_path.size);
    buffer[frame->frame_path.size] = '/';
    memcpy(buffer + frame->frame_path.size + 1U, name.data, name.size);
    buffer[length] = '\0';
    return RUNTIMEC_STATUS_SUCCESS;
}
