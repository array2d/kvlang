/*
 * 组件：contract/view 的实现
 * 作用：视图/缓冲的零分配构造与形状校验。
 * 联系：被 host 与 vmbin/vmkv/BinRWIR Compiler 的公共契约共同使用；本文件不
 *       依赖任何上层组件，也不触碰 kvspace。
 */

#include "contract/view.h"

#include <string.h>

runtimec_bytes_view_t runtimec_bytes_view_make(const void *data, size_t size) {
    runtimec_bytes_view_t view;
    view.data = (const uint8_t *)data;
    view.size = size;
    return view;
}

runtimec_string_view_t runtimec_string_view_make(const char *data, size_t size) {
    runtimec_string_view_t view;
    view.data = data;
    view.size = size;
    return view;
}

runtimec_string_view_t runtimec_string_view_from_c_string(const char *value) {
    return runtimec_string_view_make(value, value == NULL ? 0U : strlen(value));
}

runtimec_key_view_t runtimec_key_view_make(const char *data, size_t size) {
    runtimec_key_view_t view;
    view.data = data;
    view.size = size;
    return view;
}

runtimec_key_view_t runtimec_key_view_from_c_string(const char *value) {
    return runtimec_key_view_make(value, value == NULL ? 0U : strlen(value));
}

void runtimec_bytes_buffer_reset(runtimec_bytes_buffer_t *buffer) {
    if (buffer != NULL) {
        buffer->size = 0U;
    }
}

bool runtimec_bytes_view_is_valid(runtimec_bytes_view_t view) {
    return view.size == 0U || view.data != NULL;
}

bool runtimec_bytes_buffer_is_valid(const runtimec_bytes_buffer_t *buffer) {
    return buffer != NULL && buffer->size <= buffer->capacity &&
           (buffer->capacity == 0U || buffer->data != NULL);
}

bool runtimec_string_view_is_valid(runtimec_string_view_t view) {
    return view.size == 0U || view.data != NULL;
}

bool runtimec_key_view_is_valid(runtimec_key_view_t view) {
    return view.size == 0U || view.data != NULL;
}
