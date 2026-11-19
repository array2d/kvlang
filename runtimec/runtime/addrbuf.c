/*
 * 组件：runtime/addrbuf 的实现
 * 作用：线性地址缓冲；帧 = [mark, count)。所有越界路径直接 abort（p0）。
 */

#include "runtime/addrbuf.h"

#include <stdlib.h>
#include <string.h>

void runtimec_addrbuf_initialize(runtimec_addrbuf_t *buffer) {
    if (buffer == NULL) {
        abort();
    }
    memset(buffer, 0, sizeof(*buffer));
}

uint32_t runtimec_addrbuf_mark(const runtimec_addrbuf_t *buffer) {
    if (buffer == NULL) {
        abort();
    }
    return buffer->count;
}

void runtimec_addrbuf_push(runtimec_addrbuf_t *buffer, uint64_t address) {
    if (buffer == NULL || buffer->count >= RUNTIMEC_ADDRBUF_CAPACITY) {
        abort();
    }
    buffer->addresses[buffer->count] = address;
    buffer->count += 1U;
}

void runtimec_addrbuf_release(runtimec_addrbuf_t *buffer, uint32_t mark) {
    if (buffer == NULL || mark > buffer->count) {
        abort();
    }
    memset(&buffer->addresses[mark], 0,
           (size_t)(buffer->count - mark) * sizeof(buffer->addresses[0]));
    buffer->count = mark;
}

uint64_t runtimec_addrbuf_address(const runtimec_addrbuf_t *buffer,
                                  uint32_t mark, uint32_t index) {
    if (buffer == NULL || mark > buffer->count ||
        index >= buffer->count - mark) {
        abort();
    }
    return buffer->addresses[mark + index];
}
