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
    const binrwir_binvthread_frame_t *frame, void **output_handle) {
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

/* 统一失败：写 error 并返回状态。本模块只此一处，读与写共用。 */
static runtimec_status_t runtimec_vthread_status_fail(
    runtimec_error_t *error, runtimec_status_t status, int64_t code,
    const char *message) {
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT, code,
                       runtimec_string_view_from_c_string(message),
                       RUNTIMEC_PROGRAM_COUNTER_NONE);
    return status;
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
     * ref 与 body_cap（物理容量下界 80），正是「固定档」要求的那几项；
     * 后者按值自算容量，算出来的可能小于规范下限。
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

/*
 * 读入口：形状校验 + 借用视图。契约见头注释。
 *
 * 顺序是**先形状、后内容**：形状不符就是坏槽，此时内容是什么都不该给出去——
 * 先读内容再判形状，会让「坏槽」的字节短暂地成为一个看似合法的状态。
 */
runtimec_status_t runtimec_vthread_status_read(
    void *kvspace_handle, const char *status_key,
    runtimec_string_view_t *output_text, runtimec_error_t *error) {
    uint8_t *wire = NULL;
    uint32_t wire_length = 0U;
    const uint8_t *body = NULL;
    uint64_t effective_length = 0U;

    if (output_text != NULL) {
        *output_text = runtimec_string_view_make(NULL, 0U);
    }
    if (kvspace_handle == NULL || status_key == NULL ||
        status_key[0] == '\0' || output_text == NULL) {
        return runtimec_vthread_status_fail(
            error, RUNTIMEC_STATUS_INVALID, 1,
            "invalid status read request");
    }
    /*
     * `kvspaceGet` 对 `‥status` 这个 volatile key **每次重读后端**（spec 同一节），
     * 不走快照——所以这里拿到的是当前值，不是某次快照的值。
     *
     * **头一律自己解**：库已不再提供取头函数（那种「解码结构体」是
     * 第二份字段真相），布局判据只有 `kvspace/xvalue_head.h` 一份。
     */
    if (kvspaceGet(kvspace_handle, status_key, 0, &wire, &wire_length) != 0) {
        return runtimec_vthread_status_fail(
            error, RUNTIMEC_STATUS_ERROR, 4, "status read failed");
    }
    if (wire == NULL || wire_length == 0U) {
        /* 键不存在是「没有」，不是「坏」——两者修法不同，状态要分开。 */
        return runtimec_vthread_status_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 2,
            "status key is absent");
    }
    {
        kvspaceXvalueHead head;
        if (wire_length < KVSPACE_XVALUE_HEAD_LEN) {
            return runtimec_vthread_status_fail(
                error, RUNTIMEC_STATUS_ERROR, 5, "status wire is truncated");
        }
        memcpy(&head, wire, sizeof(head));
        /*
         * 形状判据照抄 spec `02-kvspace模型/04-CABI.kv`：头长恒 16、`padding` 为零、
         * 有效长度 ≤ LV。状态槽走容量形态（物理容量 ≥80），而「bodylen 是容量还是
         * 有效长度」由 langtype 判、不在 head 里——head 侧只校验这两个字段。
         *
         * 旧实现先后比对过 `storetype == SLACK` 与 `layout == 0/1`；两个字段都已从
         * head 删除——storage class 是后端写侧的概念，不是 wire 上的属性。
         *
         * **本入口只校验与后端无关的项；物理容量由 slot view 把关**（见下方注释）。
         */
        if (!kvspaceXvalueHeadPaddingIsZero(&head) ||
            kvspaceXvalueHeadTotalLen(&head) == 0U ||
            kvspaceXvalueHeadTotalLen(&head) > (uint64_t)wire_length ||
            kvspaceXvalueHeadEffectiveLen(wire, (uint64_t)wire_length,
                                          &effective_length) != 0) {
            return runtimec_vthread_status_fail(
                error, RUNTIMEC_STATUS_INVALID, 3,
                "status slot does not match the fixed shape");
        }
    }
    if (effective_length > RUNTIMEC_VTHREAD_STATUS_CONTENT_LIMIT) {
        return runtimec_vthread_status_fail(
            error, RUNTIMEC_STATUS_INVALID, 3,
            "status slot does not match the fixed shape");
    }
    body = wire + KVSPACE_XVALUE_HEAD_LEN;
    *output_text = runtimec_string_view_make((const char *)body,
                                             (size_t)effective_length);
    return RUNTIMEC_STATUS_SUCCESS;
}
