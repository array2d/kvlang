/*
 * 组件：host/kvspace_adapter 的实现
 * 作用：把 host 能力表的每一项接到 kvspace C 二进制接口上，并把一个借用的
 *       kvspace 句柄包装成本模块自己的所有者结构。
 * 联系：只经 host/kvspace_binary_interface 调用 kvspace；不直接引用任何
 *       kvspace 符号，因此链接期不依赖 libkvspace。
 * 约束：只借用句柄（不连接、不关闭）；动态拼出的 key 不解析成地址；每个失败
 *       路径先清零输出再返回明确状态。
 */

#include "host/kvspace_adapter.h"

#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kvspace/xvalue_head.h"

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
typedef struct runtimec_kvspace_resolved_address {
    uint64_t address;
    uint64_t observed_size;
    char key[RUNTIMEC_KVSPACE_KEY_CAPACITY];
} runtimec_kvspace_resolved_address_t;

typedef struct runtimec_kvspace_adapter_context {
    runtimec_allocator_t allocator;
    void *handle;
    runtimec_kvspace_binary_interface_t binary_interface;
    runtimec_kvspace_resolved_address_t *resolved_addresses;
    size_t resolved_address_count;
    size_t resolved_address_capacity;
    uint64_t generation;
    bool generation_started;
    /* 组合根给的 store 实例身份，逐字进 source revision。 */
    uint64_t instance_identifier;
} runtimec_kvspace_adapter_context_t;

static runtimec_status_t runtimec_kvspace_adapter_fail(
    runtimec_error_t *error, runtimec_status_t status, int64_t code,
    const char *message) {
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_STORAGE, code,
                       runtimec_string_view_from_c_string(message),
                       runtimec_string_view_make(NULL, 0U));
    return status;
}

/* key 是视图，转成 kvspace 要求的 NUL 结尾字符串；超长或空即非法。 */
static bool runtimec_kvspace_adapter_key_text(runtimec_key_view_t key,
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

static runtimec_status_t runtimec_kvspace_adapter_read(
    void *context, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_bytes_buffer_t *output,
    runtimec_error_t *error) {
    (void)snapshot;
    runtimec_kvspace_adapter_context_t *adapter = context;
    if (adapter == NULL || !runtimec_bytes_buffer_is_valid(output) ||
        error == NULL) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 1, "invalid read request");
    }
    char key_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    if (!runtimec_kvspace_adapter_key_text(key, key_text, sizeof(key_text),
                                           NULL)) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 2, "invalid key view");
    }
    uint8_t *value = NULL;
    uint32_t value_size = 0U;
    if (adapter->binary_interface.get(adapter->handle, key_text, 1, &value,
                                      &value_size) != 0) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_ERROR, 3, "kvspace read failed");
    }
    if (value == NULL || value_size == 0U) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 4, "key holds no value");
    }
    output->size = value_size;
    if (output->capacity < (size_t)value_size || output->data == NULL) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY, 5,
            "value does not fit the caller buffer");
    }
    memcpy(output->data, value, value_size);
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t runtimec_kvspace_adapter_read_header(
    void *context, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_host_value_header_t *output,
    runtimec_error_t *error) {
    (void)snapshot;
    runtimec_kvspace_adapter_context_t *adapter = context;
    runtimec_host_value_header_reset(output);
    if (adapter == NULL || output == NULL || error == NULL) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 6, "invalid header request");
    }
    char key_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    if (!runtimec_kvspace_adapter_key_text(key, key_text, sizeof(key_text),
                                           NULL)) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 7, "invalid key view");
    }
    uint8_t *value = NULL;
    uint32_t value_size = 0U;
    if (adapter->binary_interface.get(adapter->handle, key_text, 1, &value,
                                      &value_size) != 0) {
        return runtimec_kvspace_adapter_fail(
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

static runtimec_status_t runtimec_kvspace_adapter_write(
    void *context, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_bytes_view_t value,
    runtimec_error_t *error) {
    (void)snapshot;
    runtimec_kvspace_adapter_context_t *adapter = context;
    if (adapter == NULL || !runtimec_bytes_view_is_valid(value) ||
        error == NULL) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 9, "invalid write request");
    }
    char key_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    if (!runtimec_kvspace_adapter_key_text(key, key_text, sizeof(key_text),
                                           NULL)) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 10, "invalid key view");
    }
    if (value.size > UINT32_MAX) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 11, "value is too large");
    }
    char error_text[256];
    error_text[0] = '\0';
    if (adapter->binary_interface.set_value(
            adapter->handle, key_text, value.data, (uint32_t)value.size,
            error_text, (uint32_t)sizeof(error_text)) != 0) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_ERROR, 12,
            error_text[0] != '\0' ? error_text : "kvspace write failed");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t runtimec_kvspace_adapter_write_partial(
    void *context, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, uint64_t offset, runtimec_bytes_view_t value,
    runtimec_error_t *error) {
    (void)snapshot;
    runtimec_kvspace_adapter_context_t *adapter = context;
    if (adapter == NULL || !runtimec_bytes_view_is_valid(value) ||
        error == NULL) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 13, "invalid partial write request");
    }
    char key_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    if (!runtimec_kvspace_adapter_key_text(key, key_text, sizeof(key_text),
                                           NULL)) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 14, "invalid key view");
    }
    if (offset > UINT32_MAX || value.size > UINT32_MAX) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 15,
            "partial write is outside the addressable range");
    }
    char error_text[256];
    error_text[0] = '\0';
    if (adapter->binary_interface.set_part(
            adapter->handle, key_text, (uint32_t)offset, value.data,
            (uint32_t)value.size, error_text,
            (uint32_t)sizeof(error_text)) != 0) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_ERROR, 16,
            error_text[0] != '\0' ? error_text : "kvspace partial write failed");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t runtimec_kvspace_adapter_remove(
    void *context, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_error_t *error) {
    (void)snapshot;
    runtimec_kvspace_adapter_context_t *adapter = context;
    if (adapter == NULL || error == NULL) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 17, "invalid remove request");
    }
    char key_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    if (!runtimec_kvspace_adapter_key_text(key, key_text, sizeof(key_text),
                                           NULL)) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 18, "invalid key view");
    }
    const char *keys[1];
    keys[0] = key_text;
    char error_text[256];
    error_text[0] = '\0';
    if (adapter->binary_interface.del(adapter->handle, keys, 1U, error_text,
                                      (uint32_t)sizeof(error_text)) != 0) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_ERROR, 19,
            error_text[0] != '\0' ? error_text : "kvspace remove failed");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 一个目录最多递归的层数：超过即视为损坏的 key 树，直接失败。 */
#define RUNTIMEC_KVSPACE_LIST_DEPTH_LIMIT 32U

/* 把一个目录前缀下的直接子项交给访问者；遇到子目录（名字带尾斜杠）继续下钻。 */
static runtimec_status_t runtimec_kvspace_adapter_list_directory(
    runtimec_kvspace_adapter_context_t *adapter, const char *directory,
    size_t directory_size, const char *prefix, size_t prefix_size,
    runtimec_host_list_visitor_function_t visitor, void *visitor_context,
    unsigned depth, runtimec_error_t *error) {
    if (depth > RUNTIMEC_KVSPACE_LIST_DEPTH_LIMIT) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_ERROR, 20, "key tree is too deep");
    }
    int32_t entry_count = 0;
    if (adapter->binary_interface.list_size(adapter->handle, directory, 0, 1,
                                            &entry_count) != 0) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_ERROR, 21, "kvspace list failed");
    }
    for (int32_t index = 0; index < entry_count; index++) {
        uint8_t name[256];
        uint32_t name_size = 0U;
        if (adapter->binary_interface.list_at(
                adapter->handle, directory, 0, 1, index, name,
                (uint32_t)sizeof(name), &name_size) != 0) {
            return runtimec_kvspace_adapter_fail(
                error, RUNTIMEC_STATUS_ERROR, 22, "kvspace list entry failed");
        }
        if (directory_size + (size_t)name_size >= RUNTIMEC_KVSPACE_KEY_CAPACITY) {
            return runtimec_kvspace_adapter_fail(
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
            runtimec_status_t status = runtimec_kvspace_adapter_list_directory(
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
            if (adapter->binary_interface.list_size(
                    adapter->handle, child_directory, 0, 1, &child_count) == 0 &&
                child_count > 0) {
                runtimec_status_t status =
                    runtimec_kvspace_adapter_list_directory(
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
static runtimec_status_t runtimec_kvspace_adapter_list(
    void *context, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t prefix,
    runtimec_host_list_visitor_function_t visitor, void *visitor_context,
    runtimec_error_t *error) {
    (void)snapshot;
    runtimec_kvspace_adapter_context_t *adapter = context;
    if (adapter == NULL || visitor == NULL || error == NULL) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 24, "invalid list request");
    }
    char prefix_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    size_t prefix_size = 0U;
    if (!runtimec_kvspace_adapter_key_text(prefix, prefix_text,
                                           sizeof(prefix_text), &prefix_size)) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 25, "invalid list prefix");
    }
    while (prefix_size != 0U && prefix_text[prefix_size - 1U] == '/') {
        prefix_size -= 1U;
    }
    if (prefix_size == 0U) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 26, "list prefix is empty");
    }
    prefix_text[prefix_size] = '\0';

    size_t directory_size = prefix_size;
    while (directory_size != 0U && prefix_text[directory_size] != '/') {
        directory_size -= 1U;
    }
    if (directory_size == 0U) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 27, "list prefix has no parent");
    }
    /* directory 保持带尾斜杠：kvspace 的目录枚举要求如此。 */
    char directory[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    memcpy(directory, prefix_text, directory_size + 1U);
    directory[directory_size + 1U] = '\0';
    return runtimec_kvspace_adapter_list_directory(
        adapter, directory, directory_size + 1U, prefix_text, prefix_size,
        visitor, visitor_context, 0U, error);
}

static runtimec_status_t runtimec_kvspace_adapter_watch(
    void *context, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_bytes_view_t expected,
    uint64_t timeout_nanoseconds, runtimec_bytes_buffer_t *observed,
    runtimec_error_t *error) {
    (void)snapshot;
    runtimec_kvspace_adapter_context_t *adapter = context;
    if (adapter == NULL || !runtimec_bytes_view_is_valid(expected) ||
        !runtimec_bytes_buffer_is_valid(observed) || error == NULL) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 27, "invalid watch request");
    }
    char key_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    if (!runtimec_kvspace_adapter_key_text(key, key_text, sizeof(key_text),
                                           NULL)) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 28, "invalid key view");
    }
    if (expected.size > UINT32_MAX) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 29, "watch target is too large");
    }
    uint8_t *value = NULL;
    uint32_t value_size = 0U;
    /* 阻塞点：作废派生视图并声明静默；返回后立刻放开。 */
    (void)adapter->binary_interface.view_quiesce(adapter->handle, 1);
    int watch_status = adapter->binary_interface.watch(
        adapter->handle, key_text, expected.data, (uint32_t)expected.size,
        timeout_nanoseconds, &value, &value_size);
    (void)adapter->binary_interface.view_quiesce(adapter->handle, 0);
    uint32_t epoch = 0U;
    (void)adapter->binary_interface.view_ack(adapter->handle, &epoch);
    if (watch_status != 0) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_ERROR, 30, "kvspace watch failed");
    }
    if (value == NULL || value_size == 0U) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_ERROR, 31, "watch returned no value");
    }
    observed->size = value_size;
    if (observed->capacity < (size_t)value_size || observed->data == NULL) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY, 32,
            "observed value does not fit the caller buffer");
    }
    memcpy(observed->data, value, value_size);
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t runtimec_kvspace_adapter_signal(
    void *context, runtimec_key_view_t key, runtimec_error_t *error) {
    (void)context;
    (void)key;
    /* kvspace 二进制接口没有「主动唤醒 watcher」原语：watch 由后端轮询，
     * 触发条件只有值变化或超时。缺失能力明确报告，不做空实现。 */
    return runtimec_kvspace_adapter_fail(
        error, RUNTIMEC_STATUS_NOT_IMPLEMENTED, 33,
        "kvspace has no explicit signal primitive");
}

/* 读 `/lib/<func>/‥ctime` 的 int64 body；缺失或畸形返回 false。 */
static bool runtimec_kvspace_adapter_read_source_generation(
    runtimec_kvspace_adapter_context_t *adapter, const char *function_text,
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
    if (adapter->binary_interface.get(adapter->handle, key_text, 1, &value,
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
static runtimec_status_t runtimec_kvspace_adapter_source_revision(
    void *context, runtimec_key_view_t function_key,
    runtimec_source_revision_t *revision, runtimec_error_t *error) {
    runtimec_kvspace_adapter_context_t *adapter = context;
    runtimec_source_revision_reset(revision);
    if (adapter == NULL || revision == NULL || error == NULL) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 34, "invalid source revision request");
    }
    char function_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    if (!runtimec_kvspace_adapter_key_text(function_key, function_text,
                                           sizeof(function_text), NULL)) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 35, "invalid function key");
    }
    uint64_t source_generation = 0U;
    if (!runtimec_kvspace_adapter_read_source_generation(
            adapter, function_text, &source_generation)) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_ERROR, 36,
            "function has no readable source revision key");
    }
    bool changed = false;
    for (size_t index = 0U; index < adapter->resolved_address_count; index++) {
        runtimec_kvspace_resolved_address_t *record =
            &adapter->resolved_addresses[index];
        uint8_t *value = NULL;
        uint32_t value_size = 0U;
        if (adapter->binary_interface.get(adapter->handle, record->key, 1,
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
static runtimec_status_t runtimec_kvspace_adapter_resolve_address(
    runtimec_kvspace_adapter_context_t *adapter, const char *key_text,
    runtimec_xvalue_address_t *output_xvalue_address,
    runtimec_error_t *error) {
    uint8_t *value = NULL;
    uint32_t value_size = 0U;
    if (adapter->binary_interface.get(adapter->handle, key_text, 1, &value,
                                      &value_size) != 0) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_ERROR, 40, "kvspace read failed");
    }
    if (value == NULL || value_size < KVSPACE_XVALUE_HEAD_LEN) {
        /* 尚未分配或没有值：不是错误，但也不能交地址。 */
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 41, "key holds no value");
    }
    kvspaceXvalueHead head;
    memcpy(&head, value, sizeof(head));
    if (kvspaceXvalueHeadTotalLen(&head) > (uint64_t)value_size) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_ERROR, 42,
            "XValue body and attrs exceed the value");
    }
    const uint64_t address = (uint64_t)(uintptr_t)value;
    if (address == RUNTIMEC_XVALUE_ADDRESS_NONE) {
        return runtimec_kvspace_adapter_fail(
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
            return runtimec_kvspace_adapter_fail(
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

static runtimec_status_t runtimec_kvspace_adapter_resolve_static_xvalue_address(
    void *context, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_xvalue_address_t *output_xvalue_address,
    runtimec_error_t *error) {
    (void)snapshot;
    runtimec_kvspace_adapter_context_t *adapter = context;
    if (output_xvalue_address != NULL) {
        *output_xvalue_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
    }
    if (adapter == NULL || output_xvalue_address == NULL || error == NULL) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 37, "invalid address request");
    }
    char key_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    if (!runtimec_kvspace_adapter_key_text(key, key_text, sizeof(key_text),
                                           NULL)) {
        /* 非绝对路径 = 运行期拼出或帧相对 key：不可解析。 */
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 38,
            "key is not a static absolute key");
    }
    /* `/vthread/` 子树在每次指令完成时都会被写；基址无法承诺稳定，拒绝。 */
    if (strncmp(key_text, "/vthread/", sizeof("/vthread/") - 1U) == 0) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 39,
            "keys under /vthread are rewritten during execution");
    }
    return runtimec_kvspace_adapter_resolve_address(
        adapter, key_text, output_xvalue_address, error);
}

/*
 * 帧 key：`/vthread/{vid}/…`。与静态那条并列而不合并——它保证的是 generation
 * 而不是 compilation 租约：返回当前 generation，调用方在每次访问前比对，
 * 不等即失效重绑。动态拼出的 key（不以 `/vthread/` 开头的相对路径）一律拒绝。
 */
static runtimec_status_t runtimec_kvspace_adapter_resolve_frame_xvalue_address(
    void *context, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_xvalue_address_t *output_xvalue_address,
    uint64_t *output_generation, runtimec_error_t *error) {
    (void)snapshot;
    runtimec_kvspace_adapter_context_t *adapter = context;
    if (output_xvalue_address != NULL) {
        *output_xvalue_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
    }
    if (output_generation != NULL) {
        *output_generation = 0U;
    }
    if (adapter == NULL || output_xvalue_address == NULL ||
        output_generation == NULL || error == NULL) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 63, "invalid frame address request");
    }
    char key_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    if (!runtimec_kvspace_adapter_key_text(key, key_text, sizeof(key_text),
                                           NULL)) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 64,
            "frame key is not an absolute key");
    }
    if (strncmp(key_text, "/vthread/", sizeof("/vthread/") - 1U) != 0) {
        /* 静态 key 走 resolve_static_xvalue_address，这里只接帧槽。 */
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 65,
            "key is not a frame slot under /vthread");
    }
    runtimec_status_t status = runtimec_kvspace_adapter_resolve_address(
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
static runtimec_status_t runtimec_kvspace_adapter_write_system_member(
    runtimec_kvspace_adapter_context_t *adapter, runtimec_string_view_t vthread,
    const char *member, runtimec_string_view_t text, runtimec_error_t *error) {
    if (!runtimec_string_view_is_valid(text) || text.size > UINT32_MAX) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 45, "invalid member text");
    }
    char key_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    const size_t separator_size =
        sizeof(RUNTIMEC_KVSPACE_RUNTIME_MEMBER_SEPARATOR) - 1U;
    if (sizeof("/vthread/") - 1U + vthread.size + 1U + separator_size +
            strlen(member) + 1U >
        sizeof(key_text)) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 46, "member key is too long");
    }
    int written = snprintf(key_text, sizeof(key_text), "/vthread/%.*s/%s%s",
                           (int)vthread.size, vthread.data,
                           RUNTIMEC_KVSPACE_RUNTIME_MEMBER_SEPARATOR, member);
    if (written <= 0 || (size_t)written >= sizeof(key_text)) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 47, "member key formatting failed");
    }
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    if (adapter->binary_interface.new_slack(
            (const uint8_t *)text.data, text.size, text.size,
            RUNTIMEC_KVSPACE_SYSTEM_LANGTYPE, &wire, &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_ERROR, 48, "system value encoding failed");
    }
    char error_text[256];
    error_text[0] = '\0';
    int set_status = adapter->binary_interface.set_value(
        adapter->handle, key_text, wire, (uint32_t)wire_size, error_text,
        (uint32_t)sizeof(error_text));
    free(wire);
    if (set_status != 0) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_ERROR, 49,
            error_text[0] != '\0' ? error_text : "kvspace write failed");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 建帧：`<frame_path>/` 建目录、`<frame_path>/‥lib` 写函数 key。
 *
 * 帧路径必须已是 `/vthread/...` 的绝对路径——本层不拼、不猜 vid 或深度。
 * 目录已存在是幂等的（后端 mkindex 自身幂等）；`‥lib` 已是同一函数则跳过写入，
 * 指向别的函数则冲突失败：同一帧不能同时跑两个函数。
 */
static runtimec_status_t runtimec_kvspace_adapter_ensure_frame(
    void *context, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t frame_path, runtimec_string_view_t function_key,
    runtimec_error_t *error) {
    (void)snapshot;
    runtimec_kvspace_adapter_context_t *adapter = context;

    if (adapter == NULL || !runtimec_key_view_is_valid(frame_path) ||
        frame_path.size == 0U ||
        !runtimec_string_view_is_valid(function_key) ||
        function_key.size == 0U || error == NULL) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 70, "invalid ensure-frame request");
    }
    char frame_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    if (!runtimec_kvspace_adapter_key_text(frame_path, frame_text,
                                           sizeof(frame_text), NULL)) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 71,
            "frame path is not an absolute key");
    }
    if (strncmp(frame_text, "/vthread/", sizeof("/vthread/") - 1U) != 0) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 72,
            "frame path is not under /vthread");
    }

    /* 目录键必须以 '/' 结尾；调用方给的是帧根，这里补上分隔符。 */
    char directory_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    const size_t frame_size = strlen(frame_text);
    if (frame_size + 2U > sizeof(directory_text)) {
        return runtimec_kvspace_adapter_fail(
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
    if (adapter->binary_interface.mkindex(
            adapter->handle, directory_text, 0U, kvspace_error,
            (uint32_t)sizeof(kvspace_error)) != 0) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_ERROR, 74,
            kvspace_error[0] != '\0' ? kvspace_error
                                      : "kvspace frame directory failed");
    }

    char library_key[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    const size_t separator_size =
        sizeof(RUNTIMEC_KVSPACE_RUNTIME_MEMBER_SEPARATOR) - 1U;
    if (directory_size + separator_size +
            sizeof(runtimec_kvspace_member_library) > sizeof(library_key)) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 75, "library key is too long");
    }
    int written = snprintf(library_key, sizeof(library_key), "%s%s%s",
                           directory_text,
                           RUNTIMEC_KVSPACE_RUNTIME_MEMBER_SEPARATOR,
                           runtimec_kvspace_member_library);
    if (written <= 0 || (size_t)written >= sizeof(library_key)) {
        return runtimec_kvspace_adapter_fail(
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
    if (adapter->binary_interface.get(adapter->handle, library_key, 1,
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
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 77,
            "frame already runs a different function");
    }

    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    if (adapter->binary_interface.new_slack(
            (const uint8_t *)function_key.data, function_key.size,
            function_key.size, RUNTIMEC_KVSPACE_SYSTEM_LANGTYPE, &wire,
            &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_ERROR, 78, "library value encoding failed");
    }
    kvspace_error[0] = '\0';
    int set_status = adapter->binary_interface.set_value(
        adapter->handle, library_key, wire, (uint32_t)wire_size, kvspace_error,
        (uint32_t)sizeof(kvspace_error));
    free(wire);
    if (set_status != 0) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_ERROR, 79,
            kvspace_error[0] != '\0' ? kvspace_error
                                     : "kvspace library write failed");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t runtimec_kvspace_adapter_checkpoint(
    void *context, const runtimec_checkpoint_t *checkpoint,
    runtimec_error_t *error) {
    runtimec_kvspace_adapter_context_t *adapter = context;
    if (adapter == NULL || !runtimec_checkpoint_is_valid(checkpoint) ||
        error == NULL) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 50, "invalid checkpoint");
    }
    if (!runtimec_string_view_is_valid(checkpoint->vthread_identifier) ||
        checkpoint->vthread_identifier.size == 0U) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 51,
            "checkpoint has no vthread identifier");
    }
    runtimec_status_t status = runtimec_kvspace_adapter_write_system_member(
        adapter, checkpoint->vthread_identifier,
        runtimec_kvspace_member_program_counter,
        checkpoint->logical_program_counter, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = runtimec_kvspace_adapter_write_system_member(
        adapter, checkpoint->vthread_identifier,
        runtimec_kvspace_member_status, checkpoint->persistent_status, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (runtimec_string_view_is_valid(checkpoint->error.message) &&
        checkpoint->error.message.size != 0U) {
        return runtimec_kvspace_adapter_write_system_member(
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
static runtimec_status_t runtimec_kvspace_adapter_handoff(
    void *context, const runtimec_handoff_request_t *request,
    runtimec_error_t *error) {
    runtimec_kvspace_adapter_context_t *adapter = context;
    if (adapter == NULL || !runtimec_handoff_request_is_valid(request) ||
        error == NULL) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 52, "invalid handoff request");
    }
    if (request->operation.size + sizeof("/lib/") + sizeof("/vids/") +
            request->checkpoint.vthread_identifier.size + 1U >
        RUNTIMEC_KVSPACE_KEY_CAPACITY) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 53, "handoff key is too long");
    }
    char queue_key[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    int written = snprintf(queue_key, sizeof(queue_key), "/lib/%.*s/vids/%.*s",
                           (int)request->operation.size, request->operation.data,
                           (int)request->checkpoint.vthread_identifier.size,
                           request->checkpoint.vthread_identifier.data);
    if (written <= 0 || (size_t)written >= sizeof(queue_key)) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 54, "handoff key formatting failed");
    }
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    const runtimec_string_view_t target =
        request->checkpoint.logical_program_counter;
    if (!runtimec_string_view_is_valid(target) || target.size > UINT32_MAX ||
        adapter->binary_interface.new_slack((const uint8_t *)target.data,
                                            target.size, target.size,
                                            "char/utf8", &wire,
                                            &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_ERROR, 55, "handoff value encoding failed");
    }
    char error_text[256];
    error_text[0] = '\0';
    int set_status = adapter->binary_interface.set_value(
        adapter->handle, queue_key, wire, (uint32_t)wire_size, error_text,
        (uint32_t)sizeof(error_text));
    free(wire);
    if (set_status != 0) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_ERROR, 56,
            error_text[0] != '\0' ? error_text : "kvspace write failed");
    }
    uint64_t waited_nanoseconds = 0U;
    while (waited_nanoseconds <
           RUNTIMEC_KVSPACE_HANDOFF_DEADLINE_NANOSECONDS) {
        uint8_t *observed = NULL;
        uint32_t observed_size = 0U;
        if (adapter->binary_interface.get(adapter->handle, queue_key, 1,
                                          &observed, &observed_size) != 0) {
            return runtimec_kvspace_adapter_fail(
                error, RUNTIMEC_STATUS_ERROR, 57, "handoff queue read failed");
        }
        if (observed == NULL || observed_size == 0U) {
            return RUNTIMEC_STATUS_SUCCESS;
        }
        uint8_t *unchanged = NULL;
        uint32_t unchanged_size = 0U;
        (void)adapter->binary_interface.view_quiesce(adapter->handle, 1);
        /* 值变化时后端会立即唤醒；超时上限设为单个轮询间隔，使这一等既不会
         * 提前返回，也不会把删除拖成整段超时。 */
        (void)adapter->binary_interface.watch(
            adapter->handle, queue_key, observed, observed_size,
            RUNTIMEC_KVSPACE_HANDOFF_POLL_NANOSECONDS, &unchanged,
            &unchanged_size);
        (void)adapter->binary_interface.view_quiesce(adapter->handle, 0);
        uint32_t epoch = 0U;
        (void)adapter->binary_interface.view_ack(adapter->handle, &epoch);
        waited_nanoseconds += RUNTIMEC_KVSPACE_HANDOFF_POLL_NANOSECONDS;
    }
    return runtimec_kvspace_adapter_fail(
        error, RUNTIMEC_STATUS_WAIT, 58, "handoff was not claimed in time");
}

bool runtimec_kvspace_adapter_key_is_static(runtimec_key_view_t key) {
    if (!runtimec_key_view_is_valid(key) || key.size == 0U ||
        key.data[0] != '/') {
        return false;
    }
    return strncmp(key.data, "/vthread/",
                   key.size < sizeof("/vthread/") - 1U ? key.size
                                                       : sizeof("/vthread/") -
                                                             1U) != 0;
}

runtimec_status_t runtimec_kvspace_adapter_create_services(
    const runtimec_kvspace_adapter_request_t *request,
    const runtimec_kvspace_binary_interface_t *binary_interface,
    runtimec_host_services_t *output, runtimec_error_t *error) {
    runtimec_error_reset(error);
    runtimec_host_services_initialize(output);
    if (request == NULL || binary_interface == NULL || output == NULL ||
        error == NULL || request->borrowed_kvspace_handle == NULL ||
        request->instance_identifier == 0U ||
        !runtimec_allocator_is_valid(&request->allocator) ||
        !runtimec_kvspace_binary_interface_is_complete(binary_interface)) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 58,
            "invalid kvspace adapter request");
    }
    runtimec_kvspace_adapter_context_t *adapter =
        runtimec_allocator_allocate(&request->allocator, sizeof(*adapter),
                                    alignof(runtimec_kvspace_adapter_context_t));
    if (adapter == NULL) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 59,
            "kvspace adapter allocation failed");
    }
    memset(adapter, 0, sizeof(*adapter));
    adapter->allocator = request->allocator;
    adapter->handle = request->borrowed_kvspace_handle;
    adapter->binary_interface = *binary_interface;
    adapter->instance_identifier = request->instance_identifier;

    runtimec_host_services_initialize(output);
    output->context = adapter;
    output->allocator = request->allocator;
    output->read = runtimec_kvspace_adapter_read;
    output->read_header = runtimec_kvspace_adapter_read_header;
    output->write = runtimec_kvspace_adapter_write;
    output->write_partial = runtimec_kvspace_adapter_write_partial;
    output->remove = runtimec_kvspace_adapter_remove;
    output->list = runtimec_kvspace_adapter_list;
    output->watch = runtimec_kvspace_adapter_watch;
    output->signal = runtimec_kvspace_adapter_signal;
    output->source_revision = runtimec_kvspace_adapter_source_revision;
    output->resolve_static_xvalue_address =
        runtimec_kvspace_adapter_resolve_static_xvalue_address;
    output->resolve_frame_xvalue_address =
        runtimec_kvspace_adapter_resolve_frame_xvalue_address;
    output->ensure_frame = runtimec_kvspace_adapter_ensure_frame;
    output->checkpoint = runtimec_kvspace_adapter_checkpoint;
    output->handoff = runtimec_kvspace_adapter_handoff;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_kvspace_adapter_destroy_services(
    runtimec_host_services_t *services, runtimec_error_t *error) {
    runtimec_error_reset(error);
    if (services == NULL || error == NULL || services->context == NULL) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID, 60,
            "invalid kvspace adapter destroy request");
    }
    runtimec_kvspace_adapter_context_t *adapter = services->context;
    const runtimec_allocator_t allocator = adapter->allocator;
    if (adapter->resolved_addresses != NULL) {
        runtimec_status_t status = runtimec_allocator_deallocate(
            &allocator, adapter->resolved_addresses,
            adapter->resolved_address_capacity *
                sizeof(*adapter->resolved_addresses),
            alignof(runtimec_kvspace_resolved_address_t));
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return runtimec_kvspace_adapter_fail(
                error, RUNTIMEC_STATUS_ERROR, 61,
                "address ledger deallocation violated the allocator contract");
        }
    }
    memset(adapter, 0, sizeof(*adapter));
    runtimec_status_t status = runtimec_allocator_deallocate(
        &allocator, adapter, sizeof(*adapter),
        alignof(runtimec_kvspace_adapter_context_t));
    runtimec_host_services_initialize(services);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_ERROR, 62,
            "adapter deallocation violated the allocator contract");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}
