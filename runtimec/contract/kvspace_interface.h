#ifndef RUNTIMEC_CONTRACT_KVSPACE_INTERFACE_H
#define RUNTIMEC_CONTRACT_KVSPACE_INTERFACE_H

/*
 * 组件：contract/kvspace_interface
 * 作用：runtimec 对**当前** kvspace 存储接口的唯一引入点——转发到
 *       `kvspace/include/kvspace/kvspace.h`，并提供读写 wire 的两个共用入口。
 *
 * 为什么不直接在各处 include 上游头：runtimec 正处于「新旧两代调用点并存」的
 * 迁移期。旧那批已从上游删除，只剩 `legacy_storage_interface.h` 记账「还有哪些
 * 没迁完」；本文件是**新**那侧的单一入口。迁移完成后删掉转发即可。
 *
 * 为什么不照抄上游声明（这曾是 `kvspace_interface.h` 的写法，已改）：`KVSPACE_*`
 * 返回码来自 errno，抄一份就是第二份真相——上游改一个值这边不同步也没人发现，
 * 症状是「同一个错误码在两层被解释成不同含义」。
 *
 * 读写两个入口放在 contract 层而不是 vmbin：engine 也要写（槽绑定），而 engine
 * 不依赖 vmbin。两个方向各一个入口，handler 与 engine 共用同一份拆段算术。
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <kvspace/kvspace.h>
#include <kvspace/kvspace_xvalue.h>

/*
 * 写一个 key：`value` 必须是**完整 wire**（head + body + attr）。
 *
 * 拆段只在这里：`Realloc` 只收 body/attr 两段长度并写 head，body 与 attr 由本
 * 函数填。head 绝不写第二遍——写两遍会出现「head 说 bodylen=8、实际拷了 12」。
 */
static inline bool runtimec_kvspace_write_wire(void *handle, const char *key,
                                               const uint8_t *value,
                                               uint32_t value_size,
                                               char *message,
                                               uint32_t message_capacity) {
    if (handle == NULL || key == NULL || value == NULL ||
        value_size < KVSPACE_XVALUE_HEAD_LEN) {
        return false;
    }
    const kvspaceXvalueHead *head = (const kvspaceXvalueHead *)(const void *)value;
    const uint64_t body_length = kvspaceXvalueHeadLen(head);
    const uint64_t attr_length = (uint64_t)kvspaceXvalueHeadAttrsLen(head);
    if (KVSPACE_XVALUE_HEAD_LEN + body_length + attr_length !=
        (uint64_t)value_size) {
        return false;
    }
    if (message != NULL && message_capacity != 0U) {
        message[0] = '\0';
    }
    uint64_t value_offset = 0U;
    if (kvspaceRealloc(handle, key, body_length, attr_length, &value_offset,
                       message, message_capacity) != 0) {
        return false;
    }
    uint8_t *base = kvspaceXvalueInProcessAddress(handle);
    if (base == NULL) {
        return false;
    }
    uint8_t *slot = base + value_offset;
    if (body_length != 0U) {
        memcpy(slot + KVSPACE_XVALUE_HEAD_LEN, value + KVSPACE_XVALUE_HEAD_LEN,
               (size_t)body_length);
    }
    if (attr_length != 0U) {
        memcpy(slot + KVSPACE_XVALUE_HEAD_LEN + body_length,
               value + KVSPACE_XVALUE_HEAD_LEN + body_length,
               (size_t)attr_length);
    }
    return true;
}

/*
 * 读一个 key 的**整条 wire**。返回的指针是借用——只在「下一次对该 key 的
 * Realloc/Free 之前」有效（`kvspace.h` 头部条款）。
 *
 * 键不存在 → 返回 true 且 `*output_wire = NULL`（「没有」不是「坏」），与旧
 * `kvspaceGet` 的 0 + NULL 同语义。
 */
static inline bool runtimec_kvspace_read_wire(void *handle, const char *key,
                                              const uint8_t **output_wire,
                                              uint32_t *output_size) {
    if (output_wire != NULL) {
        *output_wire = NULL;
    }
    if (output_size != NULL) {
        *output_size = 0U;
    }
    if (handle == NULL || key == NULL) {
        return false;
    }
    char message[128];
    message[0] = '\0';
    uint64_t value_offset = 0U;
    const int status = kvspaceResolveOffset(handle, key, 0U, &value_offset, message,
                                      (uint32_t)sizeof(message));
    if (status == KVSPACE_MISSING) {
        return true;
    }
    if (status != 0) {
        return false;
    }
    const uint8_t *base = kvspaceXvalueInProcessAddress(handle);
    if (base == NULL) {
        return false;
    }
    const uint8_t *wire = base + value_offset;
    const uint64_t total =
        kvspaceXvalueHeadTotalLen((const kvspaceXvalueHead *)(const void *)wire);
    if (total == 0U || total > UINT32_MAX) {
        return false;
    }
    if (output_wire != NULL) {
        *output_wire = wire;
    }
    if (output_size != NULL) {
        *output_size = (uint32_t)total;
    }
    return true;
}

#endif
