/*
 * 组件：vmbin/vthread_status 的实现
 * 作用：`/vthread/{vid}/‥status` 的唯一写入点。
 * 约束：只调 kvspace C ABI；除调用方提供的缓冲外不分配。
 */

#include "vmbin/vthread_status.h"

#include <string.h>

#include "contract/view.h"
#include "kvspace/kvspace.h"

runtimec_status_t runtimec_vthread_status_handle(
    const binrwir_execution_frame_t *frame, void **output_handle) {
    if (output_handle != NULL) {
        *output_handle = NULL;
    }
    if (frame == NULL || output_handle == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    *output_handle = frame->kvspace_handle;
    /* 空句柄 = 本 session 不接存储，是「没有能力」而不是「请求错了」。 */
    return frame->kvspace_handle != NULL ? RUNTIMEC_STATUS_SUCCESS
                                         : RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t runtimec_vthread_status_key(
    runtimec_key_view_t frame_path, runtimec_string_view_t suffix, char *buffer,
    size_t capacity) {
    size_t root_length = 0U;
    size_t slash_count = 0U;
    size_t ordinal;

    if (buffer == NULL || capacity == 0U ||
        !runtimec_string_view_is_valid(suffix) || suffix.size == 0U ||
        !runtimec_key_view_is_valid(frame_path) || frame_path.size == 0U) {
        return RUNTIMEC_STATUS_INVALID;
    }
    buffer[0] = '\0';
    /*
     * 取到 `/vthread/{vid}` 为止：第二个斜杠（不含首字符那个）之前。
     * 帧路径形如 `/vthread/2/[1]`，帧内坐标 `[1]` 不参与系统变量键。
     */
    for (ordinal = 0U; ordinal < frame_path.size; ++ordinal) {
        if (frame_path.data[ordinal] == '/' && ordinal != 0U) {
            slash_count += 1U;
            if (slash_count == 2U) {
                root_length = ordinal;
                break;
            }
        }
    }
    if (root_length == 0U) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (root_length + suffix.size + 1U > capacity) {
        return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
    }
    memcpy(buffer, frame_path.data, root_length);
    memcpy(buffer + root_length, suffix.data, suffix.size);
    buffer[root_length + suffix.size] = '\0';
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_vthread_status_write(
    void *kvspace_handle, const char *status_key, runtimec_bytes_view_t status) {
    char message[256];
    uint8_t *body = NULL;

    if (kvspace_handle == NULL || status_key == NULL ||
        status_key[0] == '\0') {
        return RUNTIMEC_STATUS_INVALID;
    }
    /* 规范上限，不截断：超了就是错，截断会让状态文本静默变成另一个词。 */
    if (status.size > RUNTIMEC_VTHREAD_STATUS_CONTENT_LIMIT) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (status.size != 0U && status.data == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    message[0] = '\0';
    /*
     * 用 `kvspaceWriteNewPlace` 而不是 `kvspaceSetValue`：前者能直接指定
     * ref/storetype/body_cap，正是「固定档」要求的那几项；后者按值自算形状，
     * 算出来的 headlen 未必是规范要求的 8。
     */
    if (kvspaceWriteNewPlace(kvspace_handle, status_key, KVSPACE_REF_INLINE,
                             KVSPACE_STORETYPE_SLACK, (uint32_t)status.size,
                             RUNTIMEC_VTHREAD_STATUS_CAPACITY,
                             RUNTIMEC_VTHREAD_STATUS_LANGTYPE, &body, message,
                             sizeof(message)) != 0) {
        return RUNTIMEC_STATUS_ERROR;
    }
    if (status.size != 0U) {
        memcpy(body, status.data, status.size);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}
