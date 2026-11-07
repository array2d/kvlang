/*
 * 组件：vmbin/frame_target_key 的实现
 * 作用：写参 → 存储绝对 key（`<frame_path>/<符号名>`）。
 * 约束：不猜、不回退：帧路径无效、符号取不到、引用种类不是 LOCAL，一律失败。
 */

#include "frame_target_key.h"

#include <string.h>

#include "binrwir/argument.h"
#include "contract/error.h"
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

/*
 * 写目标的 key 文本。LOCAL 走 `<frame_path>/<符号名>`，STATIC 直接是符号名
 * （绝对路径本身就是完整 key），其余种类 INVALID。
 *
 * **不创建、不触碰存储**：只回答「这条写指令的 key 是什么」。创建的语义归
 * `vmbin_value_access_write_*` 那几条（#632）。
 */
runtimec_status_t vmbin_value_access_argument_key(
    const binrwir_execution_frame_t *frame,
    const binrwir_argument_t *argument, char *buffer, size_t capacity,
    runtimec_error_t *error) {
    runtimec_string_view_t name;
    runtimec_string_view_t langtype;

    if (frame == NULL || frame->program == NULL || argument == NULL ||
        buffer == NULL || capacity == 0U || error == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (argument->reference_kind == BINRWIR_REFERENCE_LOCAL) {
        return vmbin_frame_target_key(frame, argument, buffer, capacity);
    }
    if (argument->reference_kind != BINRWIR_REFERENCE_STATIC) {
        /* **失败必须带原因**：`runtimec_error_reset` 把 status 置 0（SUCCESS），
         * 而下游若直接读 `error.status` 就会拿到 0——那是「失败却没有原因」。
         * 这里显式填，让 `error` 在任何返回路径上都是可读的。 */
        runtimec_error_set(error, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_ERROR_DOMAIN_CONTRACT, 0,
                           runtimec_string_view_from_c_string(
                               "write target is neither a local slot nor a "
                               "static key"),
                           RUNTIMEC_PROGRAM_COUNTER_NONE);
        return RUNTIMEC_STATUS_INVALID;
    }
    /* STATIC：符号池里的名字就是**完整 key**（`/tmp/adk` 这类绝对路径）。
     * 拼 `frame_path` 是错的——那会得到 `/vthread/.../\/tmp/adk`。 */
    if (binrwir_program_get_symbol(frame->program, argument->slot, &name,
                                   &langtype) != RUNTIMEC_STATUS_SUCCESS ||
        name.size == 0U) {
        runtimec_error_set(error, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_ERROR_DOMAIN_CONTRACT, 0,
                           runtimec_string_view_from_c_string(
                               "static write target has no symbol name"),
                           RUNTIMEC_PROGRAM_COUNTER_NONE);
        return RUNTIMEC_STATUS_INVALID;
    }
    if (name.size + 1U > capacity) {
        runtimec_error_set(error, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                           RUNTIMEC_ERROR_DOMAIN_CONTRACT, 0,
                           runtimec_string_view_from_c_string(
                               "write target key does not fit the buffer"),
                           RUNTIMEC_PROGRAM_COUNTER_NONE);
        return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
    }
    memcpy(buffer, name.data, name.size);
    buffer[name.size] = '\0';
    return RUNTIMEC_STATUS_SUCCESS;
}
