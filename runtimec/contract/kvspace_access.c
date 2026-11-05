/*
 * 组件：contract/kvspace_access 的实现
 * 作用：runtimec 访问 kvspace 的**唯一入口**。直接调用 kvspace 仓
 *       `include/kvspace/kvspace.h` 里的 C 函数，句柄是 `void *`——没有函数
 *       指针表，也没有可替换的实现抽象层。
 * 联系：engine 的 session 拥有一个实例并按指针下传；vmkv / vmbin / engine 都只
 *       认这一个类型。body 偏移算术也只在本文件出现一次。
 * 约束：只借用句柄（不连接、不关闭）；动态拼出的 key 不解析成地址；每个失败
 *       路径先清零输出再返回明确状态。
 */

#include "contract/kvspace_access.h"

#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kvspace/kvspace.h>
#include <kvspace/xvalue_head.h>

/* kvspace 拒绝长度 >= 1024 的 key；超过即无法表达，不是可重试错误。 */
#define RUNTIMEC_KVSPACE_KEY_CAPACITY 1024U

/* `‥` U+2025：kvspace 的运行期成员分隔符（与 include/kvspace/const.h 一致）。 */
#define RUNTIMEC_KVSPACE_RUNTIME_MEMBER_SEPARATOR "\xE2\x80\xA5"

/* 成员名 `pc` 不能用独立字符串字面量书写：命名检查把独立的 pc 判为缩写。
 * 用字符数组给出同一字节序列，语义与 kvspace 的 `‥pc` 完全一致。 */
static const char runtimec_kvspace_member_program_counter[] = {'p', 'c', 0};
static const char runtimec_kvspace_member_status[] = "status";
static const char runtimec_kvspace_member_message[] = "msg";
/* 帧执行哪个函数；与 pc 的 stackid/s0 是两个事实，不是两处真相。 */
static const char runtimec_kvspace_member_library[] = "lib";

/* 函数重写版本键：layout 每次重写函数都最后写它，是唯一单调的源事实。 */
static const char runtimec_kvspace_member_source_revision[] = "ctime";

/* 无 langtype 的系统键 wire（dispatch 侧 kvlangSlackCharToWire 的同形编码）。 */
#define RUNTIMEC_KVSPACE_SYSTEM_LANGTYPE NULL


static runtimec_status_t runtimec_kvspace_access_fail(
    runtimec_error_t *error, runtimec_status_t status, int64_t code,
    const char *message) {
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_STORAGE, code,
                       runtimec_string_view_from_c_string(message),
                       RUNTIMEC_PROGRAM_COUNTER_NONE);
    return status;
}

/* key 是视图，转成 kvspace 要求的 NUL 结尾字符串；超长或空即非法。 */
static bool runtimec_kvspace_access_key_text(runtimec_key_view_t key,
                                              char *buffer, size_t capacity,
                                              size_t *output_size) {
    if (!runtimec_key_view_is_valid(key) || key.size == 0U ||
        key.size + 1U > capacity || key.data[0] != '/') {
        return false;
    }
    memcpy(buffer, key.data, key.size);
    buffer[key.size] = '\0';
    if (output_size != NULL) {
        *output_size = key.size;
    }
    return true;
}

runtimec_status_t runtimec_kvspace_access_read(
    runtimec_kvspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_bytes_buffer_t *output,
    runtimec_error_t *error) {
    (void)snapshot;
    runtimec_kvspace_access_t *adapter = access;
    if (adapter == NULL || !runtimec_bytes_buffer_is_valid(output) ||
        error == NULL) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 1, "invalid read request");
    }
    char key_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    if (!runtimec_kvspace_access_key_text(key, key_text, sizeof(key_text),
                                           NULL)) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 2, "invalid key view");
    }
    uint8_t *value = NULL;
    uint32_t value_size = 0U;
    if (kvspaceGet(adapter->handle, key_text, 1, &value,
                                      &value_size) != 0) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 3, "kvspace read failed");
    }
    /*
     * 两件事必须分开报，**判据是 value 是不是 NULL**：
     *   value == NULL           —— key 不存在（kvspace 对缺失 key 返回 0 + NULL）；
     *   value != NULL, size == 0 —— key 存在，但读到的是空字节（wire 畸形）。
     * 合成一条会让「没有这个 key」与「这个 key 写坏了」不可区分，而两者的修法
     * 完全不同（前者是调用方写错名字，后者是写侧编码错）。
     */
    if (value == NULL) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 4, "key is missing");
    }
    if (value_size == 0U) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 6, "key holds no bytes");
    }
    output->size = value_size;
    if (output->capacity < (size_t)value_size || output->data == NULL) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY, 5,
            "value does not fit the caller buffer");
    }
    memcpy(output->data, value, value_size);
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_kvspace_access_read_header(
    runtimec_kvspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_kvspace_value_header_t *output,
    runtimec_error_t *error) {
    (void)snapshot;
    runtimec_kvspace_access_t *adapter = access;
    runtimec_kvspace_value_header_reset(output);
    if (adapter == NULL || output == NULL || error == NULL) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 6, "invalid header request");
    }
    char key_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    if (!runtimec_kvspace_access_key_text(key, key_text, sizeof(key_text),
                                           NULL)) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 7, "invalid key view");
    }
    uint8_t *value = NULL;
    uint32_t value_size = 0U;
    if (kvspaceGet(adapter->handle, key_text, 1, &value,
                                      &value_size) != 0) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 8, "kvspace read failed");
    }
    /* 缺少该 key 是真实观察：value_size = 0，由调用方判定为畸形或缺失。 */
    output->value_size = (value == NULL) ? 0U : (uint64_t)value_size;
    /* kvspace 二进制接口不暴露逐值版本号；这里如实报 0（未知），不伪造。 */
    output->value_revision = 0U;
    output->flags = 0U;
    output->reserved = 0U;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_kvspace_access_write(
    runtimec_kvspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_bytes_view_t value,
    runtimec_error_t *error) {
    (void)snapshot;
    runtimec_kvspace_access_t *adapter = access;
    if (adapter == NULL || !runtimec_bytes_view_is_valid(value) ||
        error == NULL) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 9, "invalid write request");
    }
    char key_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    if (!runtimec_kvspace_access_key_text(key, key_text, sizeof(key_text),
                                           NULL)) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 10, "invalid key view");
    }
    if (value.size > UINT32_MAX) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 11, "value is too large");
    }
    char error_text[256];
    error_text[0] = '\0';
    if (kvspaceSetValue(
            adapter->handle, key_text, value.data, (uint32_t)value.size,
            error_text, (uint32_t)sizeof(error_text)) != 0) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 12,
            error_text[0] != '\0' ? error_text : "kvspace write failed");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_kvspace_access_write_partial(
    runtimec_kvspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, uint64_t offset, runtimec_bytes_view_t value,
    runtimec_error_t *error) {
    (void)snapshot;
    runtimec_kvspace_access_t *adapter = access;
    if (adapter == NULL || !runtimec_bytes_view_is_valid(value) ||
        error == NULL) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 13, "invalid partial write request");
    }
    char key_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    if (!runtimec_kvspace_access_key_text(key, key_text, sizeof(key_text),
                                           NULL)) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 14, "invalid key view");
    }
    if (offset > UINT32_MAX || value.size > UINT32_MAX) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 15,
            "partial write is outside the addressable range");
    }
    char error_text[256];
    error_text[0] = '\0';
    if (kvspaceSetPart(
            adapter->handle, key_text, (uint32_t)offset, value.data,
            (uint32_t)value.size, error_text,
            (uint32_t)sizeof(error_text)) != 0) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 16,
            error_text[0] != '\0' ? error_text : "kvspace partial write failed");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_kvspace_access_remove(
    runtimec_kvspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_error_t *error) {
    (void)snapshot;
    runtimec_kvspace_access_t *adapter = access;
    if (adapter == NULL || error == NULL) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 17, "invalid remove request");
    }
    char key_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    if (!runtimec_kvspace_access_key_text(key, key_text, sizeof(key_text),
                                           NULL)) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 18, "invalid key view");
    }
    const char *keys[1];
    keys[0] = key_text;
    char error_text[256];
    error_text[0] = '\0';
    if (kvspaceDel(adapter->handle, keys, 1U, error_text,
                                      (uint32_t)sizeof(error_text)) != 0) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 19,
            error_text[0] != '\0' ? error_text : "kvspace remove failed");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 一个目录最多递归的层数：超过即视为损坏的 key 树，直接失败。 */
#define RUNTIMEC_KVSPACE_LIST_DEPTH_LIMIT 32U

/* 把一个目录前缀下的直接子项交给访问者；遇到子目录（名字带尾斜杠）继续下钻。 */
static runtimec_status_t runtimec_kvspace_access_list_directory(
    runtimec_kvspace_access_t *adapter, const char *directory,
    size_t directory_size, const char *prefix, size_t prefix_size,
    runtimec_kvspace_list_visitor_function_t visitor, void *visitor_context,
    unsigned depth, runtimec_error_t *error) {
    if (depth > RUNTIMEC_KVSPACE_LIST_DEPTH_LIMIT) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 20, "key tree is too deep");
    }
    int32_t entry_count = 0;
    if (kvspaceListLen(adapter->handle, directory, 0, 1,
                                            &entry_count) != 0) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 21, "kvspace list failed");
    }
    for (int32_t index = 0; index < entry_count; index++) {
        uint8_t name[256];
        uint32_t name_size = 0U;
        if (kvspaceListAt(
                adapter->handle, directory, 0, 1, index, name,
                (uint32_t)sizeof(name), &name_size) != 0) {
            return runtimec_kvspace_access_fail(
                error, RUNTIMEC_STATUS_ERROR, 22, "kvspace list entry failed");
        }
        if (directory_size + (size_t)name_size >= RUNTIMEC_KVSPACE_KEY_CAPACITY) {
            return runtimec_kvspace_access_fail(
                error, RUNTIMEC_STATUS_ERROR, 23, "listed key is too long");
        }
        char full_key[RUNTIMEC_KVSPACE_KEY_CAPACITY];
        memcpy(full_key, directory, directory_size);
        memcpy(full_key + directory_size, name, name_size);
        const size_t full_size = directory_size + name_size;
        full_key[full_size] = '\0';
        if (full_size < prefix_size ||
            strncmp(full_key, prefix, prefix_size) != 0) {
            continue;
        }
        if (full_size != 0U && full_key[full_size - 1U] == '/') {
            runtimec_status_t status = runtimec_kvspace_access_list_directory(
                adapter, full_key, full_size, prefix, prefix_size, visitor,
                visitor_context, depth + 1U, error);
            if (status != RUNTIMEC_STATUS_SUCCESS) {
                return status;
            }
            continue;
        }
        /* 目录不带尾斜杠时也要下钻：后端只在显式建索引时给尾斜杠，不能据此
         * 判断是否有子项。空子目录的 list_size 为 0，因此这次下钻是廉价的。 */
        if (full_size + 1U < RUNTIMEC_KVSPACE_KEY_CAPACITY) {
            char child_directory[RUNTIMEC_KVSPACE_KEY_CAPACITY];
            memcpy(child_directory, full_key, full_size);
            child_directory[full_size] = '/';
            child_directory[full_size + 1U] = '\0';
            int32_t child_count = 0;
            if (kvspaceListLen(
                    adapter->handle, child_directory, 0, 1, &child_count) == 0 &&
                child_count > 0) {
                runtimec_status_t status =
                    runtimec_kvspace_access_list_directory(
                        adapter, child_directory, full_size + 1U, prefix,
                        prefix_size, visitor, visitor_context, depth + 1U,
                        error);
                if (status != RUNTIMEC_STATUS_SUCCESS) {
                    return status;
                }
            }
        }
        runtimec_status_t status = visitor(
            visitor_context, runtimec_key_view_make(full_key, full_size),
            error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 枚举「以 prefix 为前缀的整棵子树」。kvspace 的列表接口只列一个目录的直接
 * 子项（目录名带尾斜杠），因此这里从 prefix 的父目录起逐层下钻，向访问者交出
 * 每个叶键的完整 key。参数定义键（`<func>.[0,±j]`）与函数目录（`<func>/`）
 * 是兄弟，前者在同一次父目录枚举里被交出。
 */
runtimec_status_t runtimec_kvspace_access_list(
    runtimec_kvspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t prefix,
    runtimec_kvspace_list_visitor_function_t visitor, void *visitor_context,
    runtimec_error_t *error) {
    (void)snapshot;
    runtimec_kvspace_access_t *adapter = access;
    if (adapter == NULL || visitor == NULL || error == NULL) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 24, "invalid list request");
    }
    char prefix_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    size_t prefix_size = 0U;
    if (!runtimec_kvspace_access_key_text(prefix, prefix_text,
                                           sizeof(prefix_text), &prefix_size)) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 25, "invalid list prefix");
    }
    while (prefix_size != 0U && prefix_text[prefix_size - 1U] == '/') {
        prefix_size -= 1U;
    }
    if (prefix_size == 0U) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 26, "list prefix is empty");
    }
    prefix_text[prefix_size] = '\0';

    size_t directory_size = prefix_size;
    while (directory_size != 0U && prefix_text[directory_size] != '/') {
        directory_size -= 1U;
    }
    if (directory_size == 0U) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 27, "list prefix has no parent");
    }
    /* directory 保持带尾斜杠：kvspace 的目录枚举要求如此。 */
    char directory[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    memcpy(directory, prefix_text, directory_size + 1U);
    directory[directory_size + 1U] = '\0';
    return runtimec_kvspace_access_list_directory(
        adapter, directory, directory_size + 1U, prefix_text, prefix_size,
        visitor, visitor_context, 0U, error);
}

runtimec_status_t runtimec_kvspace_access_watch(
    runtimec_kvspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_bytes_view_t expected,
    uint64_t timeout_nanoseconds, runtimec_bytes_buffer_t *observed,
    runtimec_error_t *error) {
    (void)snapshot;
    runtimec_kvspace_access_t *adapter = access;
    if (adapter == NULL || !runtimec_bytes_view_is_valid(expected) ||
        !runtimec_bytes_buffer_is_valid(observed) || error == NULL) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 27, "invalid watch request");
    }
    char key_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    if (!runtimec_kvspace_access_key_text(key, key_text, sizeof(key_text),
                                           NULL)) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 28, "invalid key view");
    }
    if (expected.size > UINT32_MAX) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 29, "watch target is too large");
    }
    uint8_t *value = NULL;
    uint32_t value_size = 0U;
    /* 阻塞点：作废派生视图并声明静默；返回后立刻放开。 */
    (void)kvspaceViewQuiesce(adapter->handle, 1);
    int watch_status = kvspaceWatch(
        adapter->handle, key_text, expected.data, (uint32_t)expected.size,
        timeout_nanoseconds, &value, &value_size);
    (void)kvspaceViewQuiesce(adapter->handle, 0);
    uint32_t epoch = 0U;
    (void)kvspaceViewAck(adapter->handle, &epoch);
    if (watch_status != 0) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 30, "kvspace watch failed");
    }
    if (value == NULL || value_size == 0U) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 31, "watch returned no value");
    }
    observed->size = value_size;
    if (observed->capacity < (size_t)value_size || observed->data == NULL) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY, 32,
            "observed value does not fit the caller buffer");
    }
    memcpy(observed->data, value, value_size);
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_kvspace_access_signal(
    runtimec_kvspace_access_t *access, runtimec_key_view_t key, runtimec_error_t *error) {
    (void)access;
    (void)key;
    /* kvspace 二进制接口没有「主动唤醒 watcher」原语：watch 由后端轮询，
     * 触发条件只有值变化或超时。缺失能力明确报告，不做空实现。 */
    return runtimec_kvspace_access_fail(
        error, RUNTIMEC_STATUS_NOT_IMPLEMENTED, 33,
        "kvspace has no explicit signal primitive");
}

/* 读 `/lib/<func>/‥ctime` 的 int64 body；缺失或畸形返回 false。 */
static bool runtimec_kvspace_access_read_source_generation(
    runtimec_kvspace_access_t *adapter, const char *function_text,
    uint64_t *output_generation) {
    char key_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    size_t function_size = strlen(function_text);
    while (function_size != 0U && function_text[function_size - 1U] == '/') {
        function_size -= 1U;
    }
    const size_t separator_size = sizeof(RUNTIMEC_KVSPACE_RUNTIME_MEMBER_SEPARATOR) - 1U;
    const size_t member_size = sizeof(runtimec_kvspace_member_source_revision) - 1U;
    if (function_size + 1U + separator_size + member_size + 1U >
        sizeof(key_text)) {
        return false;
    }
    memcpy(key_text, function_text, function_size);
    key_text[function_size] = '/';
    memcpy(key_text + function_size + 1U, RUNTIMEC_KVSPACE_RUNTIME_MEMBER_SEPARATOR,
           separator_size);
    memcpy(key_text + function_size + 1U + separator_size,
           runtimec_kvspace_member_source_revision, member_size);
    key_text[function_size + 1U + separator_size + member_size] = '\0';

    uint8_t *value = NULL;
    uint32_t value_size = 0U;
    if (kvspaceGet(adapter->handle, key_text, 1, &value,
                                      &value_size) != 0 ||
        value == NULL || value_size < KVSPACE_XVALUE_HEAD_LEN) {
        return false;
    }
    kvspaceXvalueHead head;
    memcpy(&head, value, sizeof(head));
    /* 长度与总长都只经 head helper 判：本层不写偏移算术。 */
    if (kvspaceXvalueHeadLen(&head) != 8U ||
        kvspaceXvalueHeadTotalLen(&head) > (uint64_t)value_size) {
        return false;
    }
    uint64_t generation = 0U;
    for (size_t index = 0U; index < 8U; index++) {
        generation |= (uint64_t)value[KVSPACE_XVALUE_HEAD_LEN + index]
                      << (8U * index);
    }
    *output_generation = generation;
    return true;
}

/*
 * source revision 的两个来源：
 *   1. 函数自己的 `‥ctime`——layout 每次重写函数都最后写它；
 *   2. 已交出的 XValue 基址是否仍然指向同一个 box——覆盖「运行时改了
 *      `/lib` 里的值，但 `‥ctime` 没变」的情形。
 * 任一变化都推进本适配器的 generation，调用方据此丢弃带 argaddr 的派生状态。
 */
runtimec_status_t runtimec_kvspace_access_source_revision(
    runtimec_kvspace_access_t *access, runtimec_key_view_t function_key,
    runtimec_source_revision_t *revision, runtimec_error_t *error) {
    runtimec_kvspace_access_t *adapter = access;
    runtimec_source_revision_reset(revision);
    if (adapter == NULL || revision == NULL || error == NULL) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 34, "invalid source revision request");
    }
    char function_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    if (!runtimec_kvspace_access_key_text(function_key, function_text,
                                           sizeof(function_text), NULL)) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 35, "invalid function key");
    }
    uint64_t source_generation = 0U;
    if (!runtimec_kvspace_access_read_source_generation(
            adapter, function_text, &source_generation)) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 36,
            "function has no readable source revision key");
    }
    bool changed = false;
    for (size_t index = 0U; index < adapter->resolved_address_count; index++) {
        runtimec_kvspace_resolved_address_t *record =
            &adapter->resolved_addresses[index];
        uint8_t *value = NULL;
        uint32_t value_size = 0U;
        if (kvspaceGet(adapter->handle, record->key, 1,
                                          &value, &value_size) != 0 ||
            value == NULL || (uint64_t)(uintptr_t)value != record->address ||
            (uint64_t)value_size != record->observed_size) {
            changed = true;
            break;
        }
    }
    if (!adapter->generation_started || changed ||
        adapter->generation == 0U) {
        adapter->generation += 1U;
        adapter->generation_started = true;
    }
    revision->instance_identifier = adapter->instance_identifier;
    revision->generation = adapter->generation;
    revision->schema_version = 0U;
    revision->instruction_set_version = source_generation > UINT32_MAX
                                            ? UINT32_MAX
                                            : (uint32_t)source_generation;
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 解析一个 key 当前的真地址并登记，供 source_revision 复核是否被搬移。
 * 静态与帧两条解析共用这一段：形状校验、地址来源、零地址检查完全一致，
 * 差别只在调用方对 key 形状与生命周期前提的判定。
 */
static runtimec_status_t runtimec_kvspace_access_resolve_address(
    runtimec_kvspace_access_t *adapter, const char *key_text,
    runtimec_xvalue_address_t *output_xvalue_address,
    runtimec_error_t *error) {
    uint8_t *value = NULL;
    uint32_t value_size = 0U;
    if (kvspaceGet(adapter->handle, key_text, 1, &value,
                                      &value_size) != 0) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 40, "kvspace read failed");
    }
    if (value == NULL || value_size < KVSPACE_XVALUE_HEAD_LEN) {
        /* 尚未分配或没有值：不是错误，但也不能交地址。 */
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 41, "key holds no value");
    }
    kvspaceXvalueHead head;
    memcpy(&head, value, sizeof(head));
    if (kvspaceXvalueHeadTotalLen(&head) > (uint64_t)value_size) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 42,
            "XValue body and attrs exceed the value");
    }
    const uint64_t address = (uint64_t)(uintptr_t)value;
    if (address == RUNTIMEC_XVALUE_ADDRESS_NONE) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 43, "kvspace returned a null address");
    }
    if (adapter->resolved_address_count == adapter->resolved_address_capacity) {
        const size_t capacity =
            adapter->resolved_address_capacity == 0U
                ? 16U
                : adapter->resolved_address_capacity * 2U;
        runtimec_kvspace_resolved_address_t *grown =
            runtimec_allocator_allocate(&adapter->allocator,
                                        capacity * sizeof(*grown),
                                        alignof(runtimec_kvspace_resolved_address_t));
        if (grown == NULL) {
            return runtimec_kvspace_access_fail(
                error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 44,
                "address ledger allocation failed");
        }
        if (adapter->resolved_address_count != 0U) {
            memcpy(grown, adapter->resolved_addresses,
                   adapter->resolved_address_count * sizeof(*grown));
        }
        if (adapter->resolved_addresses != NULL) {
            (void)runtimec_allocator_deallocate(
                &adapter->allocator, adapter->resolved_addresses,
                adapter->resolved_address_capacity * sizeof(*grown),
                alignof(runtimec_kvspace_resolved_address_t));
        }
        adapter->resolved_addresses = grown;
        adapter->resolved_address_capacity = capacity;
    }
    runtimec_kvspace_resolved_address_t *record =
        &adapter->resolved_addresses[adapter->resolved_address_count++];
    record->address = address;
    record->observed_size = (uint64_t)value_size;
    memcpy(record->key, key_text, strlen(key_text) + 1U);
    *output_xvalue_address = address;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_kvspace_access_resolve_static_xvalue_address(
    runtimec_kvspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_xvalue_address_t *output_xvalue_address,
    runtimec_error_t *error) {
    (void)snapshot;
    runtimec_kvspace_access_t *adapter = access;
    if (output_xvalue_address != NULL) {
        *output_xvalue_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
    }
    if (adapter == NULL || output_xvalue_address == NULL || error == NULL) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 37, "invalid address request");
    }
    char key_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    if (!runtimec_kvspace_access_key_text(key, key_text, sizeof(key_text),
                                           NULL)) {
        /* 非绝对路径 = 运行期拼出或帧相对 key：不可解析。 */
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 38,
            "key is not a static absolute key");
    }
    /* `/vthread/` 子树在每次指令完成时都会被写；基址无法承诺稳定，拒绝。 */
    if (strncmp(key_text, "/vthread/", sizeof("/vthread/") - 1U) == 0) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 39,
            "keys under /vthread are rewritten during execution");
    }
    return runtimec_kvspace_access_resolve_address(
        adapter, key_text, output_xvalue_address, error);
}

/*
 * 帧 key：`/vthread/{vid}/…`。与静态那条并列而不合并——它保证的是 generation
 * 而不是 compilation 租约：返回当前 generation，调用方在每次访问前比对，
 * 不等即失效重绑。动态拼出的 key（不以 `/vthread/` 开头的相对路径）一律拒绝。
 */
runtimec_status_t runtimec_kvspace_access_resolve_frame_xvalue_address(
    runtimec_kvspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_xvalue_address_t *output_xvalue_address,
    uint64_t *output_generation, runtimec_error_t *error) {
    (void)snapshot;
    runtimec_kvspace_access_t *adapter = access;
    if (output_xvalue_address != NULL) {
        *output_xvalue_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
    }
    if (output_generation != NULL) {
        *output_generation = 0U;
    }
    if (adapter == NULL || output_xvalue_address == NULL ||
        output_generation == NULL || error == NULL) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 63, "invalid frame address request");
    }
    char key_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    if (!runtimec_kvspace_access_key_text(key, key_text, sizeof(key_text),
                                           NULL)) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 64,
            "frame key is not an absolute key");
    }
    if (strncmp(key_text, "/vthread/", sizeof("/vthread/") - 1U) != 0) {
        /* 静态 key 走 resolve_static_xvalue_address，这里只接帧槽。 */
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 65,
            "key is not a frame slot under /vthread");
    }
    runtimec_status_t status = runtimec_kvspace_access_resolve_address(
        adapter, key_text, output_xvalue_address, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        *output_xvalue_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
        return status;
    }
    /* generation 与 source_revision 同源：地址被搬移或源被重写都会推进它。 */
    if (!adapter->generation_started || adapter->generation == 0U) {
        adapter->generation = 1U;
        adapter->generation_started = true;
    }
    *output_generation = adapter->generation;
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 把一段文本写成持久槽：`/vthread/<vthread>/‥<member>`，无 langtype 系统键。 */
static runtimec_status_t runtimec_kvspace_access_write_system_member(
    runtimec_kvspace_access_t *adapter, runtimec_string_view_t vthread,
    const char *member, runtimec_string_view_t text, runtimec_error_t *error) {
    if (!runtimec_string_view_is_valid(text) || text.size > UINT32_MAX) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 45, "invalid member text");
    }
    char key_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    const size_t separator_size =
        sizeof(RUNTIMEC_KVSPACE_RUNTIME_MEMBER_SEPARATOR) - 1U;
    if (sizeof("/vthread/") - 1U + vthread.size + 1U + separator_size +
            strlen(member) + 1U >
        sizeof(key_text)) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 46, "member key is too long");
    }
    int written = snprintf(key_text, sizeof(key_text), "/vthread/%.*s/%s%s",
                           (int)vthread.size, vthread.data,
                           RUNTIMEC_KVSPACE_RUNTIME_MEMBER_SEPARATOR, member);
    if (written <= 0 || (size_t)written >= sizeof(key_text)) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 47, "member key formatting failed");
    }
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    if (kvspaceXvalueHeadNewSlack(
            (const uint8_t *)text.data, text.size, text.size,
            RUNTIMEC_KVSPACE_SYSTEM_LANGTYPE, &wire, &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 48, "system value encoding failed");
    }
    char error_text[256];
    error_text[0] = '\0';
    int set_status = kvspaceSetValue(
        adapter->handle, key_text, wire, (uint32_t)wire_size, error_text,
        (uint32_t)sizeof(error_text));
    free(wire);
    if (set_status != 0) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 49,
            error_text[0] != '\0' ? error_text : "kvspace write failed");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 零值局部槽的宽度表。只列**定长标量**：
 *
 *   - 数字与 bool 进表；
 *   - `time` 与 `time/duration` 进表：规范（`03-类型系统/02-种类-标量时间容器.kv`）
 *     已定为 8 字节 int64 纳秒小端，取 Go 阵营。时长 kind 的规范名是
 *     `time/duration`——与 lib 路径同名不是巧合，`duration` 不是合法类型名，
 *     不留别名（两个名字指同一类型正是要消灭的第二事实源）；
 *   - 所有字符类型、数组、stringkeymap、struct 一律**不在表内**——字符值恒为
 *     一维序列、数组元素个数可变，零值长度都是 0，而写入要求与当前 body 等长，
 *     零长 body 永远写不进真实值。留它们在表里只会制造假成功。
 *   - 低精度浮点（float16/bfloat16/float8）**范围外**：runtimec 不处理这类格式，
 *     不是「暂未实现」。
 *
 * 表里没有的类型返回 UNSUPPORTED，不猜。
 */
typedef struct runtimec_kvspace_width_entry {
    const char *langtype;
    uint64_t width;
} runtimec_kvspace_width_entry_t;

static const runtimec_kvspace_width_entry_t runtimec_kvspace_width_table[] = {
    {"int8", 1U},    {"uint8", 1U},   {"bool", 1U},
    {"int16", 2U},   {"uint16", 2U},  {"int32", 4U},
    {"uint32", 4U},  {"float32", 4U}, {"int64", 8U},
    {"uint64", 8U},  {"float64", 8U}, {"time", 8U},
    {"time/duration", 8U},
};

/* langtype → 零值字节宽度。表中没有即 UNSUPPORTED，不做名称归一化。 */
static bool runtimec_kvspace_zero_width(runtimec_string_view_t langtype,
                                        uint64_t *output_width) {
    const size_t count = sizeof(runtimec_kvspace_width_table) /
                         sizeof(runtimec_kvspace_width_table[0]);

    *output_width = 0U;
    for (size_t ordinal = 0U; ordinal < count; ++ordinal) {
        const char *name = runtimec_kvspace_width_table[ordinal].langtype;
        const size_t size = strlen(name);
        if (langtype.size == size &&
            memcmp(langtype.data, name, size) == 0) {
            *output_width = runtimec_kvspace_width_table[ordinal].width;
            return true;
        }
    }
    return false;
}

bool runtimec_kvspace_access_langtype_has_fixed_zero_width(
    runtimec_string_view_t langtype) {
    uint64_t width = 0U;

    return runtimec_string_view_is_valid(langtype) && langtype.size != 0U &&
           runtimec_kvspace_zero_width(langtype, &width);
}

/*
 * 建帧：`<frame_path>/` 建目录、`<frame_path>/‥lib` 写函数 key。
 *
 * 帧路径必须已是 `/vthread/...` 的绝对路径——本层不拼、不猜 vid 或深度。
 * 目录已存在是幂等的（后端 mkindex 自身幂等）；`‥lib` 已是同一函数则跳过写入，
 * 指向别的函数则冲突失败：同一帧不能同时跑两个函数。
 */
runtimec_status_t runtimec_kvspace_access_ensure_frame(
    runtimec_kvspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t frame_path, runtimec_string_view_t function_key,
    runtimec_error_t *error) {
    (void)snapshot;
    runtimec_kvspace_access_t *adapter = access;

    if (adapter == NULL || !runtimec_key_view_is_valid(frame_path) ||
        frame_path.size == 0U ||
        !runtimec_string_view_is_valid(function_key) ||
        function_key.size == 0U || error == NULL) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 70, "invalid ensure-frame request");
    }
    char frame_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    if (!runtimec_kvspace_access_key_text(frame_path, frame_text,
                                           sizeof(frame_text), NULL)) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 71,
            "frame path is not an absolute key");
    }
    if (strncmp(frame_text, "/vthread/", sizeof("/vthread/") - 1U) != 0) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 72,
            "frame path is not under /vthread");
    }

    /* 目录键必须以 '/' 结尾；调用方给的是帧根，这里补上分隔符。 */
    char directory_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    const size_t frame_size = strlen(frame_text);
    if (frame_size + 2U > sizeof(directory_text)) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 73, "frame path is too long");
    }
    memcpy(directory_text, frame_text, frame_size);
    size_t directory_size = frame_size;
    if (directory_text[directory_size - 1U] != '/') {
        directory_text[directory_size] = '/';
        ++directory_size;
    }
    directory_text[directory_size] = '\0';

    char kvspace_error[256];
    kvspace_error[0] = '\0';
    if (kvspaceMkindex(
            adapter->handle, directory_text, 0U, kvspace_error,
            (uint32_t)sizeof(kvspace_error)) != 0) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 74,
            kvspace_error[0] != '\0' ? kvspace_error
                                      : "kvspace frame directory failed");
    }

    char library_key[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    const size_t separator_size =
        sizeof(RUNTIMEC_KVSPACE_RUNTIME_MEMBER_SEPARATOR) - 1U;
    if (directory_size + separator_size +
            sizeof(runtimec_kvspace_member_library) > sizeof(library_key)) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 75, "library key is too long");
    }
    int written = snprintf(library_key, sizeof(library_key), "%s%s%s",
                           directory_text,
                           RUNTIMEC_KVSPACE_RUNTIME_MEMBER_SEPARATOR,
                           runtimec_kvspace_member_library);
    if (written <= 0 || (size_t)written >= sizeof(library_key)) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 76, "library key formatting failed");
    }

    /*
     * 已存在且已是同一函数：幂等成功，不重写。
     *
     * 长度取**值头声明的 body 长度**，不是 wire 总长——wire 还带 attr（系统键是
     * 两字节 `[02 00]`），拿总长比会把每个已存在且相同的值都判成「指向别的函数」。
     */
    uint8_t *existing = NULL;
    uint32_t existing_size = 0U;
    if (kvspaceGet(adapter->handle, library_key, 1,
                                      &existing, &existing_size) == 0 &&
        existing != NULL) {
        const size_t head_size = KVSPACE_XVALUE_HEAD_LEN;
        if (existing_size >= head_size) {
            kvspaceXvalueHead existing_head;
            memcpy(&existing_head, existing, sizeof(existing_head));
            const uint64_t existing_body = kvspaceXvalueHeadLen(&existing_head);
            if (existing_body == (uint64_t)function_key.size &&
                memcmp(existing + head_size, function_key.data,
                       function_key.size) == 0) {
                return RUNTIMEC_STATUS_SUCCESS;
            }
        }
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 77,
            "frame already runs a different function");
    }

    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    if (kvspaceXvalueHeadNewSlack(
            (const uint8_t *)function_key.data, function_key.size,
            function_key.size, RUNTIMEC_KVSPACE_SYSTEM_LANGTYPE, &wire,
            &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 78, "library value encoding failed");
    }
    kvspace_error[0] = '\0';
    int set_status = kvspaceSetValue(
        adapter->handle, library_key, wire, (uint32_t)wire_size, kvspace_error,
        (uint32_t)sizeof(kvspace_error));
    free(wire);
    if (set_status != 0) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 79,
            kvspace_error[0] != '\0' ? kvspace_error
                                     : "kvspace library write failed");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_kvspace_access_frame_generation(
    runtimec_kvspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t frame_path, uint64_t *output_generation,
    runtimec_error_t *error) {
    runtimec_kvspace_access_t *adapter = access;
    char frame_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    char library_key[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    size_t directory_size;
    int written;

    if (output_generation != NULL) {
        *output_generation = 0U;
    }
    if (adapter == NULL || output_generation == NULL || error == NULL ||
        !runtimec_key_view_is_valid(frame_path) || frame_path.size == 0U) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 86,
            "invalid frame generation request");
    }
    if (!runtimec_kvspace_access_key_text(frame_path, frame_text,
                                           sizeof(frame_text), NULL)) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 87,
            "frame path is not an absolute key");
    }
    /* 与 `ensure_frame` 同一条拼法：帧根补 '/'，再接分隔符与 `lib`。 */
    directory_size = strlen(frame_text);
    if (directory_size + 1U >= sizeof(frame_text)) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 88, "frame path is too long");
    }
    if (frame_text[directory_size - 1U] != '/') {
        frame_text[directory_size] = '/';
        ++directory_size;
        frame_text[directory_size] = '\0';
    }
    written = snprintf(library_key, sizeof(library_key), "%s%s%s", frame_text,
                       RUNTIMEC_KVSPACE_RUNTIME_MEMBER_SEPARATOR,
                       runtimec_kvspace_member_library);
    if (written <= 0 || (size_t)written >= sizeof(library_key)) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 89, "library key formatting failed");
    }

    runtimec_xvalue_address_t xvalue_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
    runtimec_status_t status = runtimec_kvspace_access_resolve_frame_xvalue_address(
        adapter, snapshot,
        runtimec_key_view_make(library_key, (size_t)written), &xvalue_address,
        output_generation, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        *output_generation = 0U;
        return status;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 确保零值局部槽存在。三条语义见合同声明。
 *
 * 已有值且 body 长度相同即幂等成功，不重写；长度不同是冲突——同一槽换宽度
 * 意味着调用方对类型的判断与存储不一致，失败封闭而不是静默覆盖。
 */
runtimec_status_t runtimec_kvspace_access_ensure_zero_value(
    runtimec_kvspace_access_t *access, const runtimec_snapshot_t *snapshot, runtimec_key_view_t key,
    runtimec_string_view_t langtype, runtimec_error_t *error) {
    (void)snapshot;
    runtimec_kvspace_access_t *adapter = access;

    if (adapter == NULL || !runtimec_key_view_is_valid(key) || key.size == 0U ||
        !runtimec_string_view_is_valid(langtype) || langtype.size == 0U ||
        error == NULL) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 80,
            "invalid ensure-zero-value request");
    }
    char key_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    if (!runtimec_kvspace_access_key_text(key, key_text, sizeof(key_text),
                                           NULL)) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 82,
            "local slot key is not an absolute key");
    }

    /*
     * ── 两条语义拆开：**先查存在，再谈零值** ──────────────────────────
     *
     * 本函数历史上把「接收一个已存在的槽」与「创建一个零值」混在一起，而且顺序
     * 是**先算零宽、再看存在**——于是 `[]char/utf32` 这类变长槽哪怕**已经存在**，
     * 也会在算零宽那一步被拒（「no fixed zero width」）。可是「已有的槽合法与否」
     * 与「零值有多宽」根本是两个问题：前者不需要宽度。
     *
     * 后果实测（`strings.kv`）：`replace_at(s,0,"H") -> s` 的写目标 `s` 是调用方
     * 已有的变量，却被当成要新建的零值槽拒掉 ⇒ 窗口翻成
     * `ARGUMENT_MATERIALIZE_FAILED`（0x4014）。
     */
    uint64_t width = 0U;
    const bool fixed_width = runtimec_kvspace_zero_width(langtype, &width);

    /*
     * 已存在：
     *   定宽 —— 长度相同即成功，不同则冲突（维持原判据）；
     *   变长 —— **成功**。变长槽没有「应有的宽度」可对，存在即合法；它的 body
     *           长度由内容决定，不是声明决定。
     */
    uint8_t *existing = NULL;
    uint32_t existing_size = 0U;
    if (kvspaceGet(adapter->handle, key_text, 1, &existing,
                                      &existing_size) == 0 &&
        existing != NULL) {
        if (existing_size < KVSPACE_XVALUE_HEAD_LEN) {
            return runtimec_kvspace_access_fail(
                error, RUNTIMEC_STATUS_ERROR, 83,
                "existing local slot has no readable XValue head");
        }
        if (!fixed_width) {
            return RUNTIMEC_STATUS_SUCCESS;
        }
        kvspaceXvalueHead existing_head;
        memcpy(&existing_head, existing, sizeof(existing_head));
        if (kvspaceXvalueHeadLen(&existing_head) == width) {
            return RUNTIMEC_STATUS_SUCCESS;
        }
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 84,
            "existing local slot has a different width than its langtype");
    }

    /*
     * 不存在，才谈创建：
     *   定宽 —— 零值宽度就是声明宽度（原路径，不变）；
     *   变长 —— **零值就是空 body**。空串对 `[]char/…` 是一个合法值，长度 0 与
     *           「没有值」由 XValue 本身区分（存在性来自 head，不是来自长度）。
     *           ⇒ 这里不是「按定宽思路算不出零宽所以拒绝」，而是「变长的零值本来就
     *           是零字节」。
     *   表里没有的类型仍与变长同路处理：它是不是合法 langtype 由上游（layout /
     *   编码器）裁定，本层不重复判。
     */

    /* 不存在：按 langtype 编完整 wire。编码交给上游，本层不拼字节。 */
    char langtype_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    if (langtype.size + 1U > sizeof(langtype_text)) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 85, "langtype is too long");
    }
    memcpy(langtype_text, langtype.data, langtype.size);
    langtype_text[langtype.size] = '\0';

    const uint64_t body_length = fixed_width ? width : 0U;
    uint8_t *zero_body = body_length != 0U ? calloc(1U, (size_t)body_length) : NULL;
    if (body_length != 0U && zero_body == NULL) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 86,
            "zero body allocation failed");
    }
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    const int encode_status = kvspaceXvalueHeadNewSlack(
        zero_body, body_length, body_length, langtype_text, &wire, &wire_size);
    free(zero_body);
    if (encode_status != 0 || wire == NULL || wire_size > UINT32_MAX) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 87,
            "zero value encoding failed");
    }
    char kvspace_error[256];
    kvspace_error[0] = '\0';
    const int set_status = kvspaceSetValue(
        adapter->handle, key_text, wire, (uint32_t)wire_size, kvspace_error,
        (uint32_t)sizeof(kvspace_error));
    free(wire);
    if (set_status != 0) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 88,
            kvspace_error[0] != '\0' ? kvspace_error
                                      : "kvspace zero value write failed");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 写 `‥pc`：单 key 整值写，body 是 `[2]int32` —— `[stackid][s0]`，各 4 字节小端。
 *
 * 为什么是整值写而不是两个成员 key：「改 stackid 必改 s0」这个约束**由形态保证**，
 * 不再依赖写入顺序或中间态哨兵——两个字段在同一次写里更新，天然原子。
 * （两成员 key 的形态需要人为约定顺序，且无原子多键写时只能靠哨兵让中间态可检测；
 *   单值形态让这套协议根本不需要存在。）
 *
 * **offset 语义**：kvspace 的 `SetPart` 的 offset 相对**整条 wire**（含 head），不是
 * 相对 body。真后端实测：`offset=16/20` 正确，`offset=0/4` 会写进 head，返回 0 且
 * 值随后消失。将来加「只改 s0」的热路径时，offset 必须是
 * `KVSPACE_XVALUE_HEAD_LEN + 4`，不是 4。
 *
 * 本阶段只实现整值写：当前唯一调用方（checkpoint）总是写完整位置，没有「只推进 s0」
 * 的调用点。没有调用方的分支不会被任何用例覆盖，等它出现时再和调用方一起验。
 */
static runtimec_status_t runtimec_kvspace_access_write_program_counter(
    runtimec_kvspace_access_t *adapter, runtimec_string_view_t vthread,
    runtimec_program_counter_t counter, runtimec_error_t *error) {
    if (counter.source_instruction_ordinal < 0) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 94,
            "program counter ordinal must not be negative");
    }
    char key_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    const size_t separator_size =
        sizeof(RUNTIMEC_KVSPACE_RUNTIME_MEMBER_SEPARATOR) - 1U;
    if (sizeof("/vthread/") - 1U + vthread.size + 1U + separator_size +
            strlen(runtimec_kvspace_member_program_counter) + 1U >
        sizeof(key_text)) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 90, "counter key is too long");
    }
    int written = snprintf(key_text, sizeof(key_text), "/vthread/%.*s/%s%s",
                           (int)vthread.size, vthread.data,
                           RUNTIMEC_KVSPACE_RUNTIME_MEMBER_SEPARATOR,
                           runtimec_kvspace_member_program_counter);
    if (written <= 0 || (size_t)written >= sizeof(key_text)) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 91,
            "counter key formatting failed");
    }

    /* body = [stackid:i32 LE][s0:i32 LE]，一次写完整位置。 */
    uint8_t body[8];
    const uint32_t stack_bits = (uint32_t)counter.stack_identifier;
    const uint32_t ordinal_bits = (uint32_t)counter.source_instruction_ordinal;
    for (size_t index = 0U; index < 4U; ++index) {
        body[index] = (uint8_t)((stack_bits >> (8U * index)) & 0xFFU);
        body[4U + index] = (uint8_t)((ordinal_bits >> (8U * index)) & 0xFFU);
    }
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    /*
     * 落盘 langtype 带精确长度 `[2]int32`，不是源码写法 `[]int32`
     * （`03-类型系统/10-compact形态.kv`：源级 `[]T`，落盘 `[N]T`）。
     * 丢了「恰好 2 个元素」这个事实，读方就无从校验 body 8 字节与 langtype 一致。
     */
    if (kvspaceXvalueHeadNewSlack(body, sizeof(body), sizeof(body),
                                            "[2]int32", &wire, &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 92, "counter value encoding failed");
    }
    char error_text[256];
    error_text[0] = '\0';
    int set_status = kvspaceSetValue(
        adapter->handle, key_text, wire, (uint32_t)wire_size, error_text,
        (uint32_t)sizeof(error_text));
    free(wire);
    if (set_status != 0) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 93,
            error_text[0] != '\0' ? error_text : "kvspace write failed");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_kvspace_access_checkpoint(
    runtimec_kvspace_access_t *access, const runtimec_checkpoint_t *checkpoint,
    runtimec_error_t *error) {
    runtimec_kvspace_access_t *adapter = access;
    if (adapter == NULL || !runtimec_checkpoint_is_valid(checkpoint) ||
        error == NULL) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 50, "invalid checkpoint");
    }
    if (!runtimec_string_view_is_valid(checkpoint->vthread_identifier) ||
        checkpoint->vthread_identifier.size == 0U) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 51,
            "checkpoint has no vthread identifier");
    }
    runtimec_status_t status = runtimec_kvspace_access_write_program_counter(
        adapter, checkpoint->vthread_identifier,
        checkpoint->logical_program_counter, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = runtimec_kvspace_access_write_system_member(
        adapter, checkpoint->vthread_identifier,
        runtimec_kvspace_member_status, checkpoint->persistent_status, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (runtimec_string_view_is_valid(checkpoint->error.message) &&
        checkpoint->error.message.size != 0U) {
        return runtimec_kvspace_access_write_system_member(
            adapter, checkpoint->vthread_identifier,
            runtimec_kvspace_member_message, checkpoint->error.message, error);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/* handoff 完成条件的轮询间隔与总期限。 */
#define RUNTIMEC_KVSPACE_HANDOFF_POLL_NANOSECONDS UINT64_C(1000000)
#define RUNTIMEC_KVSPACE_HANDOFF_DEADLINE_NANOSECONDS UINT64_C(30000000000)

/*
 * 扩展 handoff：把当前帧的路由键挂到 `/lib/<operation>/vids/<vthread>`，
 * 等待认领方删除该条目。与 runtime 侧的 kvlangHandoffExternalRwir 使用同一
 * 个队列与同一个完成条件。
 *
 * 完成条件用轮询 get 判定，不用 `kvspaceWatch(…, None, …)`：实测该后端只在
 * 值变化时唤醒 watcher，删除（目标 None）不触发唤醒，会一直等到超时。轮询
 * 间隔 1ms，条目消失即返回，行为与「等它被删除」一致而不会被拖满 30 秒。
 */
runtimec_status_t runtimec_kvspace_access_handoff(
    runtimec_kvspace_access_t *access, const runtimec_handoff_request_t *request,
    runtimec_error_t *error) {
    runtimec_kvspace_access_t *adapter = access;
    if (adapter == NULL || !runtimec_handoff_request_is_valid(request) ||
        error == NULL) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 52, "invalid handoff request");
    }
    if (request->operation.size + sizeof("/lib/") + sizeof("/vids/") +
            request->checkpoint.vthread_identifier.size + 1U >
        RUNTIMEC_KVSPACE_KEY_CAPACITY) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 53, "handoff key is too long");
    }
    char queue_key[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    int written = snprintf(queue_key, sizeof(queue_key), "/lib/%.*s/vids/%.*s",
                           (int)request->operation.size, request->operation.data,
                           (int)request->checkpoint.vthread_identifier.size,
                           request->checkpoint.vthread_identifier.data);
    if (written <= 0 || (size_t)written >= sizeof(queue_key)) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 54, "handoff key formatting failed");
    }
    /*
     * 队列值携带 pc。pc 现在是两个 int32，但**队列外部消费方所需的形态没有
     * 事实源**：旧 runtime 写 `<函数路径>/[<s0>,0]` 文本，新的外部执行器尚未
     * 实现。本阶段保留文本形态，把 struct 格式化成同一个串——这不是把 pc 变回
     * 字符串，是 handoff 协议尚未随 pc 改造迁移。
     */
    char target_text[96];
    const int target_size = snprintf(
        target_text, sizeof(target_text), "[%d,%d]",
        (int)request->checkpoint.logical_program_counter.stack_identifier,
        (int)request->checkpoint.logical_program_counter
            .source_instruction_ordinal);
    if (target_size <= 0 || (size_t)target_size >= sizeof(target_text)) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 55,
            "handoff program counter formatting failed");
    }
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    if (kvspaceXvalueHeadNewSlack(
            (const uint8_t *)target_text, (uint64_t)target_size,
            (uint64_t)target_size, "char/utf8", &wire, &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 56, "handoff value encoding failed");
    }
    char error_text[256];
    error_text[0] = '\0';
    int set_status = kvspaceSetValue(
        adapter->handle, queue_key, wire, (uint32_t)wire_size, error_text,
        (uint32_t)sizeof(error_text));
    free(wire);
    if (set_status != 0) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 56,
            error_text[0] != '\0' ? error_text : "kvspace write failed");
    }
    uint64_t waited_nanoseconds = 0U;
    while (waited_nanoseconds <
           RUNTIMEC_KVSPACE_HANDOFF_DEADLINE_NANOSECONDS) {
        uint8_t *observed = NULL;
        uint32_t observed_size = 0U;
        if (kvspaceGet(adapter->handle, queue_key, 1,
                                          &observed, &observed_size) != 0) {
            return runtimec_kvspace_access_fail(
                error, RUNTIMEC_STATUS_ERROR, 57, "handoff queue read failed");
        }
        if (observed == NULL || observed_size == 0U) {
            return RUNTIMEC_STATUS_SUCCESS;
        }
        uint8_t *unchanged = NULL;
        uint32_t unchanged_size = 0U;
        (void)kvspaceViewQuiesce(adapter->handle, 1);
        /* 值变化时后端会立即唤醒；超时上限设为单个轮询间隔，使这一等既不会
         * 提前返回，也不会把删除拖成整段超时。 */
        (void)kvspaceWatch(
            adapter->handle, queue_key, observed, observed_size,
            RUNTIMEC_KVSPACE_HANDOFF_POLL_NANOSECONDS, &unchanged,
            &unchanged_size);
        (void)kvspaceViewQuiesce(adapter->handle, 0);
        uint32_t epoch = 0U;
        (void)kvspaceViewAck(adapter->handle, &epoch);
        waited_nanoseconds += RUNTIMEC_KVSPACE_HANDOFF_POLL_NANOSECONDS;
    }
    return runtimec_kvspace_access_fail(
        error, RUNTIMEC_STATUS_WAIT, 58, "handoff was not claimed in time");
}

bool runtimec_kvspace_access_key_is_static(runtimec_key_view_t key) {
    if (!runtimec_key_view_is_valid(key) || key.size == 0U ||
        key.data[0] != '/') {
        return false;
    }
    return strncmp(key.data, "/vthread/",
                   key.size < sizeof("/vthread/") - 1U ? key.size
                                                       : sizeof("/vthread/") -
                                                             1U) != 0;
}

runtimec_status_t runtimec_kvspace_access_create(
    const runtimec_kvspace_access_request_t *request,
    runtimec_kvspace_access_t **output, runtimec_error_t *error) {
    runtimec_error_reset(error);
    if (output != NULL) {
        *output = NULL;
    }
    if (request == NULL || output == NULL || error == NULL ||
        request->borrowed_kvspace_handle == NULL ||
        request->instance_identifier == 0U ||
        !runtimec_allocator_is_valid(&request->allocator)) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 58,
            "invalid kvspace access request");
    }
    runtimec_kvspace_access_t *access = runtimec_allocator_allocate(
        &request->allocator, sizeof(*access),
        alignof(runtimec_kvspace_access_t));
    if (access == NULL) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 59,
            "kvspace access allocation failed");
    }
    memset(access, 0, sizeof(*access));
    access->allocator = request->allocator;
    access->handle = request->borrowed_kvspace_handle;
    access->instance_identifier = request->instance_identifier;
    *output = access;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_kvspace_access_destroy(
    runtimec_kvspace_access_t *access, runtimec_error_t *error) {
    runtimec_error_reset(error);
    if (access == NULL || error == NULL) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 60,
            "invalid kvspace access destroy request");
    }
    const runtimec_allocator_t allocator = access->allocator;
    if (access->resolved_addresses != NULL) {
        runtimec_status_t status = runtimec_allocator_deallocate(
            &allocator, access->resolved_addresses,
            access->resolved_address_capacity *
                sizeof(runtimec_kvspace_resolved_address_t),
            alignof(runtimec_kvspace_resolved_address_t));
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return runtimec_kvspace_access_fail(
                error, RUNTIMEC_STATUS_ERROR, 61,
                "address ledger deallocation violated the allocator contract");
        }
    }
    memset(access, 0, sizeof(*access));
    runtimec_status_t status = runtimec_allocator_deallocate(
        &allocator, access, sizeof(*access),
        alignof(runtimec_kvspace_access_t));
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 62,
            "kvspace access deallocation violated the allocator contract");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 唯一的「基址 + 头长度」实现。零基址不是有效地址；加法会越过 UINT64_MAX 时
 * 直接失败，不回绕。调用方拿到 NONE 必须立即失败，不得把 0 当作合法 body。
 */
runtimec_value_body_address_t
runtimec_xvalue_body_address(runtimec_xvalue_address_t xvalue_address) {
    if (xvalue_address == RUNTIMEC_XVALUE_ADDRESS_NONE ||
        xvalue_address > UINT64_MAX - RUNTIMEC_XVALUE_BODY_OFFSET_BYTES) {
        return RUNTIMEC_VALUE_BODY_ADDRESS_NONE;
    }
    return xvalue_address + RUNTIMEC_XVALUE_BODY_OFFSET_BYTES;
}

/* `+ 头长度` 的逆。零地址与下溢都不是有效基址，一律返回 NONE，不回绕。 */
runtimec_xvalue_address_t
runtimec_value_body_xvalue_address(
    runtimec_value_body_address_t body_address) {
    if (body_address == RUNTIMEC_VALUE_BODY_ADDRESS_NONE ||
        body_address < RUNTIMEC_XVALUE_BODY_OFFSET_BYTES) {
        return RUNTIMEC_XVALUE_ADDRESS_NONE;
    }
    return body_address - RUNTIMEC_XVALUE_BODY_OFFSET_BYTES;
}

/* 值头 storetype 只能是这几个档位；其它值说明地址根本不是值头。 */
static int runtimec_value_header_store_type_is_known(uint8_t store_type) {
    return store_type == KVSPACE_XVALUE_ST_SHORT ||
           store_type == KVSPACE_XVALUE_ST_SLACK ||
           store_type == KVSPACE_XVALUE_ST_LARGE ||
           store_type == KVSPACE_XVALUE_ST_EXT ||
           store_type == KVSPACE_XVALUE_ST_VARLEN;
}

/*
 * storetype 4（定容量变长）的**有效长度**：attr 区里的 tag 2 记录给出 `reallen`。
 *
 * `head.bodylen` 在 storetype 4 下是**容量**，把容量当长度用会把容量余量当数据读，
 * 且不报错——所以这里必须单独取 `reallen`。
 *
 * 取值规则（三方一致，见 `workspace/622/coding1/storetype4-matrix.md`）：
 *   - 缺 tag 2 / 记录长度非 8 / 重复 tag 2 / `reallen > bodylen` → 不可信，返回 0；
 *   - 尾部不足一条记录的残余字节**忽略**（与 kvspace-durable / kvspace-c 的扫描同语义）；
 *   - 数值从 `KVSPACE_XVALUE_ATTR_TAG_REALLEN` 取，**不写死 2**。
 */
static uint64_t runtimec_value_varlen_effective_length(
    const kvspaceXvalueHead *head) {
    const uint64_t capacity = kvspaceXvalueHeadLen(head);
    const uint64_t attrs_length = (uint64_t)kvspaceXvalueHeadAttrsLen(head);
    const uint8_t *attrs = (const uint8_t *)(const void *)head +
                           RUNTIMEC_XVALUE_BODY_OFFSET_BYTES + capacity;
    uint64_t left = attrs_length;
    int seen = 0;
    uint64_t reallen = 0U;
    while (left >= 3U) {
        const uint8_t tag = attrs[0];
        const uint16_t record_length =
            (uint16_t)((uint16_t)attrs[1] | ((uint16_t)attrs[2] << 8));
        if ((uint64_t)record_length + 3U > left) {
            return 0U;
        }
        if (tag == KVSPACE_XVALUE_ATTR_TAG_REALLEN) {
            if (seen != 0 || record_length != 8U) {
                return 0U;
            }
            uint64_t value = 0U;
            for (size_t i = 0U; i < 8U; ++i) {
                value |= (uint64_t)attrs[3U + i] << (8U * i);
            }
            reallen = value;
            seen = 1;
        }
        attrs += (size_t)record_length + 3U;
        left -= (uint64_t)record_length + 3U;
    }
    if (seen == 0 || reallen > capacity) {
        return 0U;
    }
    return reallen;
}

/* 从值体地址往前挪头长度得到值头，长度从值头读。不可信一律返回 0。
 *
 * storetype 4 返回的是**有效长度（reallen）**而不是 `bodylen`（容量）：调用方
 * （`vmbin/value_access`）拿它来定读写窗口，容量会把余量当数据。
 * `reallen == 0` 合法（容量已分配、无内容），与「不可信返回 0」在这里同值——
 * 两者的后果都是「读 0 字节」，不需要额外区分。 */
uint32_t runtimec_value_body_length(runtimec_value_body_address_t body_address) {
    if (body_address == RUNTIMEC_VALUE_BODY_ADDRESS_NONE ||
        body_address < RUNTIMEC_XVALUE_BODY_OFFSET_BYTES) {
        return 0U;
    }
    const kvspaceXvalueHead *head = (const kvspaceXvalueHead *)(uintptr_t)(
        body_address - RUNTIMEC_XVALUE_BODY_OFFSET_BYTES);
    if (!runtimec_value_header_store_type_is_known(head->storetype)) {
        return 0U;
    }
    const uint64_t body_length =
        head->storetype == KVSPACE_XVALUE_ST_VARLEN
            ? runtimec_value_varlen_effective_length(head)
            : kvspaceXvalueHeadLen(head);
    return body_length > UINT32_MAX ? 0U : (uint32_t)body_length;
}

/*
 * 字节形态：从**已读回内存的完整 wire**（头 + body + attr）取出 body 视图。
 *
 * 与 `runtimec_xvalue_body_address` 是同一处偏移算术的两个入口——一个从地址推，
 * 一个从字节切。分成两处写就是第二份算术，而第二份算术不会随头长度变化更新。
 *
 * **只返回 body，不返回 body+attr。** 曾经写成 `wire.size - offset`——那是
 * 「body + attr」，与函数名和注释都相反。后果不是报错，而是**把 attr 字节
 * 当成值的一部分**：`vmkv_rwir_operation_name_from_wire` 读 HANDOFF 的算子名时，
 * 名字后面跟着 layout 写的 langtype attr（13 字节），于是外部扩展永远认领不到
 * 队列项——「停在 waiting 却没进队列」，与「跑完了」无法区分。
 *
 * **长度一律来自头，不从调用方传进来的 size 推**：attr 区长度只有头知道；
 * 用 `wire.size` 减出来的永远是「body + attr」。
 *
 * 任何不自洽（头读不出已知 storetype / body 或 attr 超出 wire）都返回空视图
 * （NULL / 0），不返回「有指针无长度」的半状态——**不猜，不截断到看起来合理**。
 */
runtimec_string_view_t runtimec_xvalue_body_from_wire(
    runtimec_bytes_view_t wire) {
    const size_t offset = (size_t)RUNTIMEC_XVALUE_BODY_OFFSET_BYTES;
    if (wire.data == NULL || wire.size < offset) {
        return runtimec_string_view_make(NULL, 0U);
    }
    kvspaceXvalueHead head;
    memcpy(&head, wire.data, sizeof(head));
    if (!runtimec_value_header_store_type_is_known(head.storetype)) {
        return runtimec_string_view_make(NULL, 0U);
    }
    const uint64_t body_length = kvspaceXvalueHeadLen(&head);
    const uint64_t attrs_length = (uint64_t)kvspaceXvalueHeadAttrsLen(&head);
    const uint64_t available = (uint64_t)wire.size - (uint64_t)offset;
    if (body_length > available || attrs_length > available - body_length) {
        return runtimec_string_view_make(NULL, 0U);
    }
    return runtimec_string_view_make((const char *)wire.data + offset,
                                     (size_t)body_length);
}

void runtimec_kvspace_value_header_reset(runtimec_kvspace_value_header_t *header) {
    if (header != NULL) {
        memset(header, 0, sizeof(*header));
    }
}

void runtimec_handoff_request_reset(runtimec_handoff_request_t *request) {
    if (request != NULL) {
        memset(request, 0, sizeof(*request));
    }
}

bool runtimec_handoff_request_is_valid(
    const runtimec_handoff_request_t *request) {
    return request != NULL &&
           runtimec_string_view_is_valid(request->operation) &&
           request->operation.size != 0U;
}
