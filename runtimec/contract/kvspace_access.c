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

/* nanosleep 是 POSIX：本工程用 C11（`-std=c11` 关闭扩展），必须显式要 POSIX。
 * 放在**所有** include 之前——特性宏晚于系统头就无效。 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "contract/kvspace_access.h"

#include <errno.h>
#include <time.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "contract/key_directory.h"
#include "contract/kvspace_interface.h"
#include "contract/kvspace_interface.h"
#include <kvspace/kvlang_xvalue.h>
#include <kvspace/kvspace_xvalue.h>

/* `RUNTIMEC_KVSPACE_KEY_CAPACITY` 现在定义在 `contract/kvspace_access.h`：
 * 地址账本的 key 槽与建 key 的缓冲必须共用同一个上界。 */

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
/*
 * 系统成员的值都是文本：`‥status` / `‥message` / `‥lib`。
 *
 * **必须带 langtype**：新 head 没有 storetype，`None` 的唯一判据是「空 body + 无
 * langtype」。若系统槽不带类型，一个空值就会被读成 `None`，与「空但不是 None」
 * 无法区分（见 `02-kvspace模型/06-XValueHead线格式.kv`「空值」节）。`‥pc` 早已
 * 带 `[2]int32`，本常量把其余三个对齐到同一规则。
 */
#define RUNTIMEC_KVSPACE_SYSTEM_LANGTYPE "[]char/utf8"


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

/*
 * 读一个 key 的整条 wire：**读写路径的唯一读入口**。
 *
 * 新接口只给 offset，地址要自己算：`kvspaceXvalueInProcessAddress(h) + offset`。
 * 返回的 `output_wire` 是**借用**——下一次对该 key 的 `Realloc`/`Free` 之前有效，
 * 跨写持有属于未定义（`kvspace.h` 头部的生命周期条款）。
 *
 * 三条判据（与旧 `kvspaceGet` 同语义，不因换接口而放宽）：
 *   - 键不存在 → `output_wire = NULL`，返回 SUCCESS（「没有」不是「坏」）；
 *   - 键存在但零字节 → `output_wire` 非 NULL、`output_size = 0`；
 *   - 后端不支持映射（durable）→ `kvspaceXvalueInProcessAddress` 返回 NULL，
 *     本函数报 `UNSUPPORTED`——不猜、不退化成旧接口。
 */
static runtimec_status_t runtimec_kvspace_access_read_wire(
    runtimec_kvspace_access_t *adapter, const char *key_text,
    const uint8_t **output_wire, uint32_t *output_size,
    runtimec_error_t *error) {
    if (output_wire != NULL) {
        *output_wire = NULL;
    }
    if (output_size != NULL) {
        *output_size = 0U;
    }
    if (adapter == NULL || key_text == NULL) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 3, "invalid read request");
    }
    char error_text[256];
    error_text[0] = '\0';
    uint64_t value_offset = 0U;
    const int status = kvspaceResolve(adapter->handle, key_text, 0U,
                                      &value_offset, error_text,
                                      (uint32_t)sizeof(error_text));
    if (status != 0) {
        /* 键不存在是**观察**不是故障：旧接口也是 0 + NULL。 */
        if (status == KVSPACE_MISSING) {
            return RUNTIMEC_STATUS_SUCCESS;
        }
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 3, "kvspace read failed");
    }
    const uint8_t *base = kvspaceXvalueInProcessAddress(adapter->handle);
    if (base == NULL) {
        /* 非映射后端没有进程内值区。读写路径按裁决「durable 先不管」尚未迁移，
         * 这里明确报告不支持，而不是拿一个 NULL 基址去算地址。 */
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 3,
            "the backend does not expose an in-process value area");
    }
    const uint8_t *wire = base + value_offset;
    const kvspaceXvalueHead *head = (const kvspaceXvalueHead *)(const void *)wire;
    /* padding 非零即非法：它是 head ABI 的一部分。总长也要能算出来。 */
    const uint64_t total = kvspaceXvalueHeadTotalLen(head);
    if (total == 0U || total > UINT32_MAX) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 4, "value head is malformed");
    }
    if (output_wire != NULL) {
        *output_wire = wire;
    }
    if (output_size != NULL) {
        *output_size = (uint32_t)total;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 写一个 key：**读写路径的唯一写入口**。
 *
 * `wire` 是调用方用 `kvspaceXvalueHeadNew*` 造好的**完整 wire**（构造点不变）。
 * 本函数把它拆成 body/attr 两段交给 `kvspaceRealloc`——`Realloc` 只分配并写 head，
 * body 与 attr 由本函数填。head 绝不在这里写第二遍。
 *
 * 拆段只在这里做：body 偏移算术与 attr 长度的定义点都在本文件，两处各算一次
 * 迟早会不一致（「head 说 bodylen=8、实际拷了 12」）。
 */
static runtimec_status_t runtimec_kvspace_access_write_wire(
    runtimec_kvspace_access_t *adapter, const char *key_text,
    const uint8_t *wire, uint32_t wire_size, runtimec_error_t *error) {
    if (adapter == NULL || key_text == NULL || wire == NULL ||
        wire_size < KVSPACE_XVALUE_HEAD_LEN) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 3, "invalid write request");
    }
    const kvspaceXvalueHead *head = (const kvspaceXvalueHead *)(const void *)wire;
    const uint64_t body_length = kvspaceXvalueHeadLen(head);
    const uint64_t attr_length = (uint64_t)kvspaceXvalueHeadAttrsLen(head);
    if (KVSPACE_XVALUE_HEAD_LEN + body_length + attr_length != (uint64_t)wire_size) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 4, "wire length does not match its head");
    }
    char error_text[256];
    error_text[0] = '\0';
    uint64_t value_offset = 0U;
    if (kvspaceRealloc(adapter->handle, key_text, body_length, attr_length,
                       &value_offset, error_text,
                       (uint32_t)sizeof(error_text)) != 0) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 5, "kvspace write failed");
    }
    uint8_t *base = kvspaceXvalueInProcessAddress(adapter->handle);
    if (base == NULL) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 5,
            "the backend does not expose an in-process value area");
    }
    /* head 由 Realloc 写好了；这里只填 body 与 attr。 */
    uint8_t *slot = base + value_offset;
    if (body_length != 0U) {
        memcpy(slot + KVSPACE_XVALUE_HEAD_LEN, wire + KVSPACE_XVALUE_HEAD_LEN,
               (size_t)body_length);
    }
    if (attr_length != 0U) {
        memcpy(slot + KVSPACE_XVALUE_HEAD_LEN + body_length,
               wire + KVSPACE_XVALUE_HEAD_LEN + body_length,
               (size_t)attr_length);
    }
    return RUNTIMEC_STATUS_SUCCESS;
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
    /*
     * **不跟 Ptr**：runtimec 的解引用只走显式 `*`（见 16-ptr.kv），读接口拿的是
     * key 本身的值。`kvspaceResolve` 契约就是「不跟 Ptr」，与旧接口 resolve=0 同义。
     */
    const uint8_t *value = NULL;
    uint32_t value_size = 0U;
    {
        const runtimec_status_t read_status = runtimec_kvspace_access_read_wire(
            adapter, key_text, &value, &value_size, error);
        if (read_status != RUNTIMEC_STATUS_SUCCESS) {
            return read_status;
        }
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
    /* 不跟 Ptr：理由同 `runtimec_kvspace_access_read`。 */
    const uint8_t *value = NULL;
    uint32_t value_size = 0U;
    {
        const runtimec_status_t read_status = runtimec_kvspace_access_read_wire(
            adapter, key_text, &value, &value_size, error);
        if (read_status != RUNTIMEC_STATUS_SUCCESS) {
            return read_status;
        }
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
    return runtimec_kvspace_access_write_wire(adapter, key_text, value.data,
                                              (uint32_t)value.size, error);
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
    /*
     * 原地写 body 的一段：`Resolve` 拿偏移 → `base + offset + 头长 + 段偏移`。
     * 比旧 `kvspaceSetPart` 更直接——那条接口内部也是这个算法，只是不给调用方
     * 基址。**不改 head**：本接口的前提是「值已存在且容量不变」，head 里的
     * bodylen/attrslen 由先前那次写定下。
     */
    char error_text[256];
    error_text[0] = '\0';
    uint64_t value_offset = 0U;
    if (kvspaceResolve(adapter->handle, key_text, 0U, &value_offset, error_text,
                       (uint32_t)sizeof(error_text)) != 0) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 16, "kvspace partial write failed");
    }
    uint8_t *base = kvspaceXvalueInProcessAddress(adapter->handle);
    if (base == NULL) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 16,
            "the backend does not expose an in-process value area");
    }
    uint8_t *slot = base + value_offset + KVSPACE_XVALUE_HEAD_LEN;
    if (value.size != 0U) {
        memcpy(slot + offset, value.data, (size_t)value.size);
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
    /* 单键删除：旧 `kvspaceDel` 一次删 n 个，新接口 `kvspaceFree` 一次一个。
     * 这里本来就只有一个键，直接调，不造批量包装。 */
    char error_text[256];
    error_text[0] = '\0';
    if (kvspaceFree(adapter->handle, key_text, error_text,
                    (uint32_t)sizeof(error_text)) != 0) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 19,
            error_text[0] != '\0' ? error_text : "kvspace remove failed");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 一个目录最多递归的层数：超过即视为损坏的 key 树，直接失败。 */
#define RUNTIMEC_KVSPACE_LIST_DEPTH_LIMIT 32U

/* 排序缓存的 source 绑定：存储接口的 `kvspaceList` + 句柄。 */
static runtimec_status_t runtimec_kvspace_access_directory_source(
    void *context, const char *directory, char *names, uint32_t capacity,
    uint32_t *output_length, uint32_t *output_count,
    uint64_t *output_generation);

/*
 * 目录枚举的 source：直接调 `kvspaceList`，把返回码翻成 runtimec 状态。
 *
 * `context` 就是 adapter 自己——缓存挂在 adapter 上，source 只需要句柄；多包一个
 * 结构体只是多一层间接。错误文本写进栈上缓冲后丢弃：诊断由调用方的具名 fail 给，
 * 后端 err 在这里没有更具体的去处。
 */
static runtimec_status_t runtimec_kvspace_access_directory_source(
    void *context, const char *directory, char *names, uint32_t capacity,
    uint32_t *output_length, uint32_t *output_count,
    uint64_t *output_generation) {
    runtimec_kvspace_access_t *adapter = (runtimec_kvspace_access_t *)context;
    if (adapter == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    char error_text[256];
    error_text[0] = '\0';
    const int status = kvspaceList(adapter->handle, directory, names, capacity,
                                   output_length, output_count,
                                   output_generation, error_text,
                                   (uint32_t)sizeof(error_text));
    if (status == 0) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    /* 容量不足是新接口**唯一**允许重试的失败，且它已写出所需长度与个数。 */
    if (status == -ENOSPC) {
        return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
    }
    return RUNTIMEC_STATUS_ERROR;
}

/*
 * 把一个目录前缀下的直接子项交给访问者；遇到子目录（名字带尾斜杠）继续下钻。
 *
 * **顺序来自本进程**：存储接口的 `kvspaceList` 明确「后端顺序、不排序」，所以这里
 * 经 `runtimec_key_directory` 按 `cmp_coord` 排好再按 index 取。旧实现依赖后端
 * `ListAt` 已排序，语义等价（`runtimec_key_compare_directory_entry` 与后端
 * `child_cmp` 逐项对齐，见 `tests/key_order_test.c`）。
 */
static runtimec_status_t runtimec_kvspace_access_list_directory(
    runtimec_kvspace_access_t *adapter, const char *directory,
    size_t directory_size, const char *prefix, size_t prefix_size,
    runtimec_kvspace_list_visitor_function_t visitor, void *visitor_context,
    unsigned depth, runtimec_error_t *error) {
    if (depth > RUNTIMEC_KVSPACE_LIST_DEPTH_LIMIT) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 20, "key tree is too deep");
    }
    /*
     * **每层一个独立缓存**：递归时外层还在用自己那份 `at()` 取项，共用一份会
     * 让子目录的 refresh 把外层正在遍历的目录换掉——实测拼出 `/lib/test/test/`
     * 这种重复前缀，之后整条链失败。栈上分配，深度上限 32，代价可忽略。
     */
    runtimec_key_directory_t level;
    runtimec_key_directory_reset(&level, &adapter->allocator, adapter,
                                 runtimec_kvspace_access_directory_source);
    runtimec_status_t refresh = runtimec_key_directory_refresh(&level, directory);
    if (refresh != RUNTIMEC_STATUS_SUCCESS) {
        (void)runtimec_key_directory_destroy(&level);
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 21, "kvspace list failed");
    }
    const uint32_t entry_count = level.count;
    for (uint32_t index = 0U; index < entry_count; index++) {
        const char *name = NULL;
        uint32_t name_size = 0U;
        if (runtimec_key_directory_at(&level, index, &name,
                                      &name_size) != RUNTIMEC_STATUS_SUCCESS ||
            name == NULL) {
            (void)runtimec_key_directory_destroy(&level);
            return runtimec_kvspace_access_fail(
                error, RUNTIMEC_STATUS_ERROR, 22, "kvspace list entry failed");
        }
        if (directory_size + (size_t)name_size >= RUNTIMEC_KVSPACE_KEY_CAPACITY) {
            (void)runtimec_key_directory_destroy(&level);
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
                (void)runtimec_key_directory_destroy(&level);
                return status;
            }
            continue;
        }
        /* 目录不带尾斜杠时也要下钻：后端只在显式建索引时给尾斜杠，不能据此
         * 判断是否有子项。空子目录的 list_size 为 0，因此这次下钻是廉价的。
         *
         * **探测必须用独立缓存**：`adapter->directory` 正被外层这轮循环使用
         * （`at()` 每次都要读它）。拿它去 refresh 子目录，就把外层正在遍历的
         * 目录换成了子目录——下一次 `at()` 便在子目录上取项，拼出来的 key
         * 是 `/lib/test/test/`，refresh 立刻失败。
         */
        if (full_size + 1U < RUNTIMEC_KVSPACE_KEY_CAPACITY) {
            char child_directory[RUNTIMEC_KVSPACE_KEY_CAPACITY];
            memcpy(child_directory, full_key, full_size);
            child_directory[full_size] = '/';
            child_directory[full_size + 1U] = '\0';
            runtimec_key_directory_t probe;
            runtimec_key_directory_reset(&probe, &adapter->allocator, adapter,
                                         runtimec_kvspace_access_directory_source);
            const bool has_children =
                runtimec_key_directory_refresh(&probe, child_directory) ==
                    RUNTIMEC_STATUS_SUCCESS &&
                probe.count > 0U;
            (void)runtimec_key_directory_destroy(&probe);
            if (has_children) {
                runtimec_status_t status =
                    runtimec_kvspace_access_list_directory(
                        adapter, child_directory, full_size + 1U, prefix,
                        prefix_size, visitor, visitor_context, depth + 1U,
                        error);
                if (status != RUNTIMEC_STATUS_SUCCESS) {
                    (void)runtimec_key_directory_destroy(&level);
                    return status;
                }
            }
        }
        runtimec_status_t status = visitor(
            visitor_context, runtimec_key_view_make(full_key, full_size),
            error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            (void)runtimec_key_directory_destroy(&level);
            return status;
        }
    }
    (void)runtimec_key_directory_destroy(&level);
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

    /* 不跟 Ptr（`Resolve` 的契约即如此）；这一处旧 resolve=1 的跟跳语义
     * 由需要它的调用方用 `contract/key_resolution` 显式做。 */
    const uint8_t *value = NULL;
    uint32_t value_size = 0U;
    if (runtimec_kvspace_access_read_wire(adapter, key_text, &value,
                                          &value_size, NULL) !=
            RUNTIMEC_STATUS_SUCCESS ||
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
    /*
     * **本函数必须是幂等查询**：同一存储状态下连续调用两次，返回值必须相同。
     *
     * 曾经这里扫到第一条失效记录就 `changed = true; break;`，**而不刷新账本**。
     * 账本只追加（只有 `record_address` 一处写），于是那条失效记录从此每次都被
     * 重新发现 ⇒ `generation` 每次 +1 ⇒ 「观测到失效的次数」被当成了「存储布局
     * 的纪元」。后果：runner 在 encode 前取一次、`vmkv_encode` 内部再取一次，
     * 两次必然差 1 ⇒ 假性 `STALE_SNAPSHOT`。
     *
     * 实测（`014_longest_common`，帧准备物化 `char/utf32` 触发一次重定位后）：
     *   [rev] fn=/lib/test records=32 changed=1 before=1 → after=2   (runner)
     *   [rev] fn=/lib/test records=32 changed=1 before=2 → after=3   (encode)
     * 两次都 changed=1、每次都 +1 —— 比较永远不等。
     *
     * 修法三条：
     *   1. 扫到失效记录就**就地刷新**（重新解析成功的写回新地址/尺寸）；
     *   2. **整批重定位只算一次**纪元推进（`changed` 是个 bool，不按条数累加）；
     *   3. key 已不存在 ⇒ **从账本移除**（swap 尾部），不让它永久污染后续每次查询。
     *
     * 比较本身保留：它仍然告诉调用方「地址账本已过期」，这是有价值的信息；
     * 修的是「用过一次就该落定」这件事。
     */
    /*
     * 扫全部槽。**命中一次就落定**：同 key 只有一槽，所以同一代里搬两次只留
     * 最后那个地址——这是有意的，不是遗漏。`changed` 从设计上就是 bool
     * （「整批重定位只算一次纪元推进」），「搬了几次」这条信息今天没有消费者；
     * 产出它要维护历史，而历史只会随调用次数增长，正是本账本要消灭的东西。
     *
     * **复核必须用登记时的同一个 resolve 模式。** 曾经这里写死 `resolve = 1`，
     * 而登记走 `resolve = 0`——指针 key 在两种模式下返回**不同的地址**
     * （实测：`/ptr` resolve=0 → `0x…1c0` 的指针值本体；resolve=1 →
     * `0x…1e0` 的目标）。于是每一轮扫都判一次「搬了」，`changed` 恒为 true，
     * `generation` **每调一次 source_revision 就 +1**；而 generation 是编译
     * 缓存键的一部分 ⇒ 缓存永不命中 ⇒ 每次 execute 重编整个 Program。
     * 实测 binary_search N=16：33 次缓存查询 → 18 个不同 generation、0 命中，
     * 93ms；账本修复后账本不再增长，但这一条不修缓存照样全 miss。
     */
    bool changed = false;
    size_t index = 0U;
    while (index < adapter->resolved_address_capacity) {
        runtimec_kvspace_resolved_address_t *record =
            &adapter->resolved_addresses[index];

        if (record->key_size == 0U) {
            index += 1U; /* 空槽 */
            continue;
        }
        char record_key[RUNTIMEC_KVSPACE_KEY_CAPACITY];
        memcpy(record_key, record->key, record->key_size);
        record_key[record->key_size] = '\0';
        /*
         * 账本记的是**地址**；新接口给的是 offset，所以这里按
         * 「基址 + offset」求当前地址再比。基址在进程内稳定（`kvspace.h`：
         * 预留 VA + MAP_FIXED），偏移才是会变的那一项——超容量搬移后它变，
         * 容量内原地则不变，正好对上「offset 变化 = 该 key 缓存失效」。
         */
        char record_error[128];
        record_error[0] = '\0';
        uint64_t record_offset = 0U;
        const int read_status =
            kvspaceResolve(adapter->handle, record_key, 0U, &record_offset,
                           record_error, (uint32_t)sizeof(record_error));
        const uint8_t *base = kvspaceXvalueInProcessAddress(adapter->handle);
        const uint64_t current_address =
            (read_status == 0 && base != NULL)
                ? (uint64_t)(uintptr_t)(base + record_offset)
                : 0U;
        const bool stale = read_status != 0 || base == NULL ||
                           current_address != record->address;
        if (!stale) {
            index += 1U;
            continue;
        }
        changed = true;
        if (read_status != 0 || base == NULL) {
            /*
             * key 已不存在：**清空该槽**。开放寻址不能像稠密数组那样把尾元素
             * 搬进来——那样会破坏探测链，让另一个 key 的槽永远找不到。清空后
             * 探测到空槽即中止，链的语义仍然成立（后续插入会复用这个空槽）。
             */
            record->key_size = 0U;
            adapter->resolved_address_count -= 1U;
            index += 1U;
            continue;
        }
        /* 仍在，只是被搬了：就地刷新，下一轮同一个 key 就不会再被判为失效。 */
        record->address = current_address;
        index += 1U;
    }
    /*
     * **纪元只在真的观察到搬移时前进。**
     *
     * `generation` 描述的是「存储布局变过没有」，它是编译缓存键的一部分；
     * 把「这一轮扫到了什么」也塞进同一个变量，就等于让缓存每次都失效。
     * 所以判据只有 `changed`（外加首次启动的一次初始化）。
     */
    if (!adapter->generation_started || adapter->generation == 0U) {
        adapter->generation = 1U;
        adapter->generation_started = true;
    } else if (changed) {
        adapter->generation += 1U;
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
 * **地址永远指向值自己；跟随指针由显式 `*` 完成。**
 *
 * 这是既有裁决「解引用只走显式 `*`」的直接推论：任何「读一个 key 就顺手跟一跳」
 * 都是隐式解引用。本模块所有 `kvspaceGet` 的 resolve 参数一律为 0，取值/取地址
 * 都拿到 key 上那一份 wire 本身——指针值的 wire 就是「head ref=PTR + body=目标路径」。
 * 跟随发生在 `vmbin/value_access` 的 `dereference` 分支，且只在那里。
 *
 * 不加一个「会跟随」的解析入口：两个解析器 = 同一语义两个实现（一个编码一个含义）。
 *
 * 解析一个 key 当前的真地址并登记，供 source_revision 复核是否被搬移。
 * 静态与帧两条解析共用这一段：形状校验、地址来源、零地址检查完全一致，
 * 差别只在调用方对 key 形状与生命周期前提的判定。
 */
/*
 * ── 地址账本：按 key 查地址的表 ────────────────────────────────────────
 *
 * **它是表，不是日志。** 唯一的消费者 `source_revision` 遍历它，逐条问「这个
 * key 的地址还是那个地址吗」；同一个 key 被问 N 次，就该只有一条答案。
 *
 * 曾经它是追加数组：每次解析 `count++`，同 key 反复登记。实测
 * （`workspace/tmp/helper/memgrow-214409/SUMMARY.md`）一次空函数调用追加 5 条
 * × 1040 字节，而整段程序只有 9 个不同 key——**把「调用次数」当成了容量增长轴**，
 * N=800 时账本 4 MB。
 */

/* 账本槽上限：1024 槽 × 1050 字节 ≈ 1.04 MiB。走到这里说明「调用次数」又被
 * 当成了「key 数」，应当响亮失败而不是继续吃内存。 */
#define RUNTIMEC_KVSPACE_LEDGER_SLOT_LIMIT 1024U
/* 装载因子上限 3/4：开放寻址线性探测在 0.75 以上探测长度劣化快。 */
#define RUNTIMEC_KVSPACE_LEDGER_LOAD_NUMERATOR 3U
#define RUNTIMEC_KVSPACE_LEDGER_LOAD_DENOMINATOR 4U

static uint64_t runtimec_kvspace_access_key_hash(const char *key, size_t size) {
    /* FNV-1a 64：够散、无表、无状态。碰撞由下面的逐字节比兜住。 */
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t index;

    for (index = 0U; index < size; ++index) {
        hash ^= (uint64_t)(uint8_t)key[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

/*
 * 找一个 key 的槽：命中返回其下标，否则返回**它该插入的空槽**下标。
 * `slots == NULL` 时只用于「该不该扩容」，调用方保证 capacity 非零。
 */
static size_t runtimec_kvspace_access_ledger_probe(
    const runtimec_kvspace_resolved_address_t *slots, size_t capacity,
    const char *key, uint16_t key_size, uint64_t key_hash) {
    size_t index = (size_t)(key_hash & (uint64_t)(capacity - 1U));

    for (;;) {
        const runtimec_kvspace_resolved_address_t *slot = &slots[index];

        if (slot->key_size == 0U) {
            return index; /* 空槽：未命中，这里就是插入点 */
        }
        if (slot->key_hash == key_hash && slot->key_size == key_size &&
            memcmp(slot->key, key, key_size) == 0) {
            return index; /* 命中 */
        }
        index = (index + 1U) & (capacity - 1U);
    }
}

/* 扩容到 new_capacity 并重新插入现有槽。失败时账本**原样可用**。 */
static runtimec_status_t runtimec_kvspace_access_ledger_grow(
    runtimec_kvspace_access_t *adapter, size_t new_capacity,
    runtimec_error_t *error) {
    runtimec_kvspace_resolved_address_t *grown =
        (runtimec_kvspace_resolved_address_t *)runtimec_allocator_allocate(
            &adapter->allocator,
            new_capacity * sizeof(*grown), _Alignof(runtimec_kvspace_resolved_address_t));

    if (grown == NULL) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 44,
            "address ledger allocation failed");
    }
    memset(grown, 0, new_capacity * sizeof(*grown));
    for (size_t ordinal = 0U; ordinal < adapter->resolved_address_capacity;
         ++ordinal) {
        const runtimec_kvspace_resolved_address_t *source =
            &adapter->resolved_addresses[ordinal];

        if (source->key_size == 0U) {
            continue;
        }
        grown[runtimec_kvspace_access_ledger_probe(
            grown, new_capacity, source->key, source->key_size,
            source->key_hash)] = *source;
    }
    if (adapter->resolved_addresses != NULL) {
        (void)runtimec_allocator_deallocate(
            &adapter->allocator, adapter->resolved_addresses,
            adapter->resolved_address_capacity * sizeof(*grown),
            _Alignof(runtimec_kvspace_resolved_address_t));
    }
    adapter->resolved_addresses = grown;
    adapter->resolved_address_capacity = new_capacity;
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 登记一条「key → 当前地址」。**同 key 就地更新，不追加。**
 *
 * 空 key 在**插入之前**具名拒绝：`key_size == 0` 同时是空槽判据，一条空 key
 * 走到插入会把一个占用槽静默清空（地址还在、按槽查变成空槽），账本从此丢一条
 * 复核记录。键在语言里不可能为空，出现即说明 key 拼接错了。
 */
static runtimec_status_t runtimec_kvspace_access_ledger_record(
    runtimec_kvspace_access_t *adapter, const char *key_text, size_t key_size,
    uint64_t address, uint64_t observed_size, runtimec_error_t *error) {
    const uint64_t key_hash =
        runtimec_kvspace_access_key_hash(key_text, key_size);
    size_t index;

    if (key_size == 0U) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 66,
            "cannot record an empty key in the address ledger");
    }
    if (key_size >= RUNTIMEC_KVSPACE_KEY_CAPACITY) {
        /* 与建 key 的缓冲同界，正常路径到不了这里；到了就是有人绕过了同界。 */
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 67,
            "resolved key exceeds the address ledger capacity");
    }
    if (adapter->resolved_address_capacity == 0U) {
        runtimec_status_t status = runtimec_kvspace_access_ledger_grow(
            adapter, 16U, error);

        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
    }
    if ((adapter->resolved_address_count + 1U) *
            RUNTIMEC_KVSPACE_LEDGER_LOAD_DENOMINATOR >
        adapter->resolved_address_capacity *
            RUNTIMEC_KVSPACE_LEDGER_LOAD_NUMERATOR) {
        if (adapter->resolved_address_capacity >=
            RUNTIMEC_KVSPACE_LEDGER_SLOT_LIMIT) {
            return runtimec_kvspace_access_fail(
                error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 68,
                "address ledger is full; the key count must not grow with "
                "call count");
        }
        runtimec_status_t status = runtimec_kvspace_access_ledger_grow(
            adapter, adapter->resolved_address_capacity * 2U, error);

        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
    }
    index = runtimec_kvspace_access_ledger_probe(
        adapter->resolved_addresses, adapter->resolved_address_capacity,
        key_text, (uint16_t)key_size, key_hash);
    if (adapter->resolved_addresses[index].key_size == 0U) {
        adapter->resolved_address_count += 1U; /* 新 key 才占新槽 */
        adapter->resolved_addresses[index].key_size = (uint16_t)key_size;
        adapter->resolved_addresses[index].key_hash = key_hash;
        memcpy(adapter->resolved_addresses[index].key, key_text, key_size);
    }
    adapter->resolved_addresses[index].address = address;
    adapter->resolved_addresses[index].observed_size = observed_size;
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 解析一个 key 的当前地址。
 *
 * `record` 为 false 的调用方**不登记**：它们只借代次、不持有返回的地址。
 * **传 false 的调用方，今后若开始持有该地址，必须改回 true**——不登记的地址
 * 不会被 `source_revision` 复核，存储搬移后它会变成悬空读数。这不是「默认优化」，
 * 是「我承诺不持有它」的声明。
 */
static runtimec_status_t runtimec_kvspace_access_resolve_address(
    runtimec_kvspace_access_t *adapter, const char *key_text, bool record,
    runtimec_xvalue_address_t *output_xvalue_address,
    runtimec_error_t *error) {
    const uint8_t *value = NULL;
    uint32_t value_size = 0U;
    {
        const runtimec_status_t read_status = runtimec_kvspace_access_read_wire(
            adapter, key_text, &value, &value_size, error);
        if (read_status != RUNTIMEC_STATUS_SUCCESS) {
            return read_status;
        }
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
    if (record) {
        const size_t key_size = strlen(key_text);
        const runtimec_status_t status = runtimec_kvspace_access_ledger_record(
            adapter, key_text, key_size, address, (uint64_t)value_size, error);

        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
    }
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
        adapter, key_text, true, output_xvalue_address, error);
}

/*
 * 帧 key：`/vthread/{vid}/…`。与静态那条并列而不合并——它保证的是 generation
 * 而不是 compilation 租约：返回当前 generation，调用方在每次访问前比对，
 * 不等即失效重绑。动态拼出的 key（不以 `/vthread/` 开头的相对路径）一律拒绝。
 */
static runtimec_status_t runtimec_kvspace_access_resolve_frame_address(
    runtimec_kvspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, bool record,
    runtimec_xvalue_address_t *output_xvalue_address,
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
        adapter, key_text, record, output_xvalue_address, error);
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

runtimec_status_t runtimec_kvspace_access_resolve_frame_xvalue_address(
    runtimec_kvspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_xvalue_address_t *output_xvalue_address,
    uint64_t *output_generation, runtimec_error_t *error) {
    return runtimec_kvspace_access_resolve_frame_address(
        access, snapshot, key, true, output_xvalue_address, output_generation,
        error);
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
            (const uint8_t *)text.data, text.size,
            RUNTIMEC_KVSPACE_SYSTEM_LANGTYPE, &wire, &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 48, "system value encoding failed");
    }
    char error_text[256];
    error_text[0] = '\0';
    const bool wrote = runtimec_kvspace_write_wire(
        adapter->handle, key_text, wire, (uint32_t)wire_size, error_text,
        (uint32_t)sizeof(error_text));
    const int set_status = wrote ? 0 : 1;
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

/*
 * 定宽 langtype → 零值字节宽度。
 *
 * **表里的三种东西必须分清**（p1：一条路径不许同时表达三种语义）：
 *
 *   1. **定宽的字符类型**——`char/utf32` 是**一个码位、恒 4 字节**
 *      （等同 C 的 `char32_t`）；`char/ascii` 是**一个字符、恒 1 字节**
 *      （等同 C 的 `char`）。两者都必须进表：它们是标量，帧准备应当按固定宽度
 *      物化零值并绑定，与 `int64` 无差别。
 *
 *   2. **真正的变长类型**——`char/utf8` 是 1–4 字节的 UTF-8 序列，`[]char/` 前缀的
 *      数组形态字符串按完整串匹配。它们**不进表**，走变长路径（写路径
 *      创建、按内容定长）。**不许**把 `char/utf8` 塞进来：它的宽度取决于内容，
 *      给它编一个固定宽度就是「发明语义」。
 *
 *   3. **根本不认识的 langtype**——同样返回 false，但那是「不知道」，与第 2 类
 *      的「知道它变长」是两件事。调用方（帧准备）对两者的动作恰好相同（不物化），
 *      所以共用 `false` 返回值是可接受的；**但表的内容必须只由第 1 类构成**。
 *
 * 曾经 `char/utf32` 与 `char/ascii` 没进表：于是 `char/utf32` 的局部槽在帧准备
 * 时不物化，任何「首次写是 `LOCATE_WRITE` 取可写位置」的指令必然
 * `UNBOUND_SLOT`（`strings` / `ndarray_numel_scattered` / `020_valid_parentheses` /
 * `008_string_to_int` 四条）。
 */
static const runtimec_kvspace_width_entry_t runtimec_kvspace_width_table[] = {
    {"int8", 1U},    {"uint8", 1U},   {"bool", 1U},
    {"int16", 2U},   {"uint16", 2U},  {"int32", 4U},
    {"uint32", 4U},  {"float32", 4U}, {"int64", 8U},
    {"uint64", 8U},  {"float64", 8U}, {"time", 8U},
    {"time/duration", 8U},
    /* 定宽的字符标量（见上）。`char/utf8` 故意不在此——它变长。 */
    {"char/ascii", 1U}, {"char/utf32", 4U},
};

/*
 * 基种类 → 零值字节宽度。表中没有即不认识，返回 false。
 *
 * 只在**没有形状前缀**时被 `runtimec_kvspace_zero_width` 调用，也是数组形状
 * 求积时的被乘数。
 */
static bool runtimec_kvspace_base_width(runtimec_string_view_t langtype,
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

/*
 * 定长数组的形状前缀 → 元素个数。`[2]` → 2、`[2,3]` → 6。可嵌套，逐层相乘。
 *
 * **只认定长维**：每个维度必须是非空十进制数字串。下面这些都**不是**定长，
 * 一律返回 false（交给调用方的变长路径，不物化）：
 *
 *   `[]K`      空维——长度由内容决定
 *   `[?]K`     问号——同上
 *   `[*]K`     `*`——同上
 *   `[+]K`     加号——形式上「至少一维」，宽度仍不可知
 *   `[n]K`     符号/表达式——不是十进制数字
 *
 * 这条判据的形状与 `generate_rwir_catalog.py` 的 `SHAPE_PREFIX_PATTERN` 一致
 * （`^[[0-9,]+]`），两处都用「非空十进制数字与逗号」的字符集。**两处必须一起看**：
 * 那边是生成期校验、这里是运行期物化，判据不一致会出现「生成器放行、运行期不物化」
 * 这种只在执行时才暴露的分歧。
 */
static bool runtimec_kvspace_array_element_count(runtimec_string_view_t shape,
                                                 uint64_t *output_count) {
    uint64_t total = 1U;
    size_t cursor = 0U;

    *output_count = 0U;
    while (cursor < shape.size && shape.data[cursor] == '[') {
        uint64_t dimension = 0U;
        bool have_digit = false;

        cursor += 1U;
        while (cursor < shape.size && shape.data[cursor] != ']') {
            const char character = shape.data[cursor];

            if (character == ',') {
                /* 逗号之后必须还有数字：`[2,]` 不是定长形状。 */
                if (!have_digit) {
                    return false;
                }
                total *= dimension;
                dimension = 0U;
                have_digit = false;
                cursor += 1U;
                continue;
            }
            if (character < '0' || character > '9') {
                return false; /* `?` / `*` / `+` / 符号表达式：不是定长 */
            }
            /* 溢出保护：宽度是字节数，超过 uint64 的十分之一就必然越界。 */
            if (dimension > (UINT64_MAX - (uint64_t)(character - '0')) / 10U) {
                return false;
            }
            dimension = dimension * 10U + (uint64_t)(character - '0');
            have_digit = true;
            cursor += 1U;
        }
        if (cursor >= shape.size || !have_digit) {
            return false; /* 没有闭合的 `]`，或该维为空 */
        }
        total *= dimension;
        cursor += 1U; /* 跳过 ']' */
    }
    if (cursor == 0U) {
        return false; /* 没有形状前缀 —— 调用方应先处理这种情况 */
    }
    *output_count = total;
    return true;
}

/*
 * langtype → 零值字节宽度。不认识即 false，不做名称归一化。
 *
 * 三种形态（判据有序，**先形状后基种类**）：
 *
 *   1. `[N]K` / `[d0,d1,…]K`（各维非空十进制）⇒ 各维乘积 × 基种类宽度；
 *   2. 无前缀的标量 ⇒ 直接查基表；
 *   3. 其余（变长维、未知基种类、容器）⇒ false，**不猜**。
 *
 * 为什么定长数组必须物化：`vthread·pc()` 的 `result1` 是 `[2]int32`（spec
 * `05-runtime语义/02-执行模型-续.kv:26-40`），而 `write_body` 要求与当前 body
 * 等长——槽不物化，第一个写就 `UNBOUND_SLOT`。同理 `detail=16` 那批容器局部
 * 也卡在这里。
 *
 * 为什么变长数组**不**物化：`[]int32` 的零值长度是 0，而写入要求等长——物化出
 * 一个零长槽反而让第一次真实写必失败。它们走写路径创建，由调用方负责。
 */
static bool runtimec_kvspace_zero_width(runtimec_string_view_t langtype,
                                        uint64_t *output_width) {
    uint64_t element_count = 0U;
    size_t cursor = 0U;
    runtimec_string_view_t base;
    uint64_t base_width = 0U;

    *output_width = 0U;
    /*
     * ── 判据一：指针与扩展不是定宽 ─────────────────────────────────────
     *
     * `*T`（指针）与 `@T`（扩展定位）的 body 是**变长文本**（目标 key 路径 / 定位串），
     * 而且空 body 是合法值（None）——所以它们的零宽是「没有固定宽度」：
     * 帧准备不物化，首次写由存储层创建。
     *
     * 规范 `02-kvspace模型/06-XValueHead线格式.kv`：「指针**不是**定宽（空 body
     * 即 None），帧准备不物化指针局部，首次写创建」。
     *
     * **必须排在形状判据之前**：`*[2]int32` 是指向定长数组的指针（不是定宽），
     * 而 `[*]int32` 是变长数组（也不是定宽）——两者都拒绝，但理由不同：
     * 前者因为「指针」、后者因为「变长维」。顺序写反会让 `*[2]int32` 先撞形状规则，
     * 错误归因成「数组形状不认识」，而真正的原因是指针。
     */
    if (langtype.size != 0U &&
        (langtype.data[0] == '*' || langtype.data[0] == '@')) {
        return false;
    }
    /*
     * ── 判据二：定长形状 ───────────────────────────────────────────────
     *
     * 只认**非空十进制维**：`[2]int32` / `[2,3]int32` / `[2][3]int32` 定宽；
     * `[]` / `[?]` / `[*]` / `[+]` 的宽度由内容决定，不是定宽。
     */
    if (langtype.size != 0U && langtype.data[0] == '[') {
        /*
         * 找到形状前缀的结尾：连续的 `[..]` 组。剩下的才是基种类。
         * 这里只找边界，不解释语义——`runtimec_kvspace_array_element_count`
         * 负责判合法性。
         */
        while (cursor < langtype.size && langtype.data[cursor] == '[') {
            while (cursor < langtype.size && langtype.data[cursor] != ']') {
                cursor += 1U;
            }
            if (cursor >= langtype.size) {
                return false; /* 没有闭合 */
            }
            cursor += 1U;
        }
        {
            const runtimec_string_view_t shape =
                runtimec_string_view_make(langtype.data, cursor);

            if (!runtimec_kvspace_array_element_count(shape, &element_count)) {
                return false;
            }
        }
        base = runtimec_string_view_make(langtype.data + cursor,
                                         langtype.size - cursor);
        if (!runtimec_kvspace_base_width(base, &base_width)) {
            return false;
        }
        if (element_count != 0U && base_width > UINT64_MAX / element_count) {
            return false;
        }
        *output_width = element_count * base_width;
        return true;
    }
    return runtimec_kvspace_base_width(langtype, output_width);
}

bool runtimec_kvspace_access_langtype_has_fixed_zero_width(
    runtimec_string_view_t langtype) {
    uint64_t width = 0U;

    return runtimec_kvspace_access_langtype_zero_width(langtype, &width);
}

bool runtimec_kvspace_access_langtype_zero_width(
    runtimec_string_view_t langtype, uint64_t *output_width) {
    if (output_width != NULL) {
        *output_width = 0U;
    }
    if (output_width == NULL || !runtimec_string_view_is_valid(langtype) ||
        langtype.size == 0U) {
        return false;
    }
    return runtimec_kvspace_zero_width(langtype, output_width);
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
    if (kvspaceMkindex(adapter->handle, directory_text, kvspace_error,
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
    const uint8_t *existing = NULL;
    uint32_t existing_size = 0U;
    if (runtimec_kvspace_access_read_wire(adapter, library_key, &existing,
                                          &existing_size, error) ==
            RUNTIMEC_STATUS_SUCCESS &&
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
            RUNTIMEC_KVSPACE_SYSTEM_LANGTYPE, &wire, &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 78, "library value encoding failed");
    }
    kvspace_error[0] = '\0';
    const bool wrote = runtimec_kvspace_write_wire(
        adapter->handle, library_key, wire, (uint32_t)wire_size, kvspace_error,
        (uint32_t)sizeof(kvspace_error));
    const int set_status = wrote ? 0 : 1;
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

    /*
     * **`record = false`**：本函数只为拿代次，返回的地址当场丢弃、谁都拿不到。
     * 传 false 的调用方**今后若开始持有该地址，必须改回 true**——不登记的地址
     * 不会被 `source_revision` 复核，存储搬移后会变成悬空读数。
     *
     * 顺带去掉的是原本 60% 的无效登记：实测一次函数调用登记 5 个 key，其中 3 个
     * 是 `‥lib` 这类系统键（`workspace/tmp/helper/memgrow-214409/SUMMARY.md`）。
     * 它们的地址搬移只会来自「帧目录重建」（`frame_state` 的 storage_generation
     * 独立处理）或「存储整体换代」（`instance_identifier` 在 source_revision
     * 第一段就拦下）——账本提供的信息全是已经知道的。
     */
    runtimec_xvalue_address_t xvalue_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
    runtimec_status_t status = runtimec_kvspace_access_resolve_frame_address(
        adapter, snapshot, runtimec_key_view_make(library_key, (size_t)written),
        false, &xvalue_address, output_generation, error);
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
    const uint8_t *existing = NULL;
    uint32_t existing_size = 0U;
    if (runtimec_kvspace_access_read_wire(adapter, key_text, &existing,
                                          &existing_size, error) ==
            RUNTIMEC_STATUS_SUCCESS &&
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
        zero_body, body_length, langtype_text, &wire, &wire_size);
    free(zero_body);
    if (encode_status != 0 || wire == NULL || wire_size > UINT32_MAX) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 87,
            "zero value encoding failed");
    }
    char kvspace_error[256];
    kvspace_error[0] = '\0';
    const bool wrote = runtimec_kvspace_write_wire(
        adapter->handle, key_text, wire, (uint32_t)wire_size, kvspace_error,
        (uint32_t)sizeof(kvspace_error));
    const int set_status = wrote ? 0 : 1;
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
runtimec_status_t runtimec_kvspace_access_write_program_counter(
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
    if (kvspaceXvalueHeadNewSlack(body, sizeof(body),
                                            "[2]int32", &wire, &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 92, "counter value encoding failed");
    }
    char error_text[256];
    error_text[0] = '\0';
    const bool wrote = runtimec_kvspace_write_wire(
        adapter->handle, key_text, wire, (uint32_t)wire_size, error_text,
        (uint32_t)sizeof(error_text));
    const int set_status = wrote ? 0 : 1;
    free(wire);
    if (set_status != 0) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 93,
            error_text[0] != '\0' ? error_text : "kvspace write failed");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 读 `‥pc`：与写侧同一套 key 拼法与编码（`[2]int32`，8 字节小端）。
 *
 * 缺失与坏槽必须分开：缺失返回 present=false（新 vthread 还没写过位置，是正常
 * 状态）；存在但 body 不是 8 字节是坏槽，返回 INVALID。把两者都折叠成「没有
 * 位置」，会让一个损坏的 pc 被当成「从头开始」——那正是 resume 最危险的形态。
 */
runtimec_status_t runtimec_kvspace_access_read_program_counter(
    runtimec_kvspace_access_t *access, runtimec_string_view_t vthread,
    runtimec_program_counter_t *output_counter, bool *output_present,
    runtimec_error_t *error) {
    runtimec_kvspace_access_t *adapter = access;
    char key_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    const size_t separator_size =
        sizeof(RUNTIMEC_KVSPACE_RUNTIME_MEMBER_SEPARATOR) - 1U;

    if (output_counter != NULL) {
        *output_counter = RUNTIMEC_PROGRAM_COUNTER_NONE;
    }
    if (output_present != NULL) {
        *output_present = false;
    }
    if (adapter == NULL || !runtimec_string_view_is_valid(vthread) ||
        vthread.size == 0U || output_counter == NULL ||
        output_present == NULL || error == NULL) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 95,
            "invalid program counter read request");
    }
    if (sizeof("/vthread/") - 1U + vthread.size + 1U + separator_size +
            strlen(runtimec_kvspace_member_program_counter) + 1U >
        sizeof(key_text)) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 96, "counter key is too long");
    }
    const int written = snprintf(
        key_text, sizeof(key_text), "/vthread/%.*s/%s%s", (int)vthread.size,
        vthread.data, RUNTIMEC_KVSPACE_RUNTIME_MEMBER_SEPARATOR,
        runtimec_kvspace_member_program_counter);
    if (written <= 0 || (size_t)written >= sizeof(key_text)) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 97,
            "counter key formatting failed");
    }

    const uint8_t *wire = NULL;
    uint32_t wire_size = 0U;
    if (runtimec_kvspace_access_read_wire(adapter, key_text, &wire, &wire_size,
                                          NULL) != RUNTIMEC_STATUS_SUCCESS ||
        wire == NULL) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    if (wire_size < KVSPACE_XVALUE_HEAD_LEN) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 98, "counter wire is truncated");
    }
    kvspaceXvalueHead head;
    memcpy(&head, wire, sizeof(head));
    if (kvspaceXvalueHeadLen(&head) != 8U) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 99,
            "counter body is not [2]int32");
    }
    const uint8_t *body = wire + KVSPACE_XVALUE_HEAD_LEN;
    uint32_t stack_bits = 0U;
    uint32_t ordinal_bits = 0U;
    for (size_t index = 0U; index < 4U; ++index) {
        stack_bits |= (uint32_t)body[index] << (8U * index);
        ordinal_bits |= (uint32_t)body[4U + index] << (8U * index);
    }
    output_counter->stack_identifier = (int32_t)stack_bits;
    output_counter->source_instruction_ordinal = (int32_t)ordinal_bits;
    *output_present = true;
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 读 `<frame_path>/‥lib`。值体是函数 key 文本（`ensure_frame` 写入的形状）。
 * 返回的文本由调用方缓冲承接，NUL 结尾；body 超容量是 INVALID，不截断。
 */
runtimec_status_t runtimec_kvspace_access_read_frame_library(
    runtimec_kvspace_access_t *access, runtimec_key_view_t frame_path,
    char *output_text, size_t output_capacity, runtimec_error_t *error) {
    runtimec_kvspace_access_t *adapter = access;
    char frame_text[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    char library_key[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    size_t directory_size;
    int written;

    if (output_text != NULL && output_capacity != 0U) {
        output_text[0] = '\0';
    }
    if (adapter == NULL || !runtimec_key_view_is_valid(frame_path) ||
        frame_path.size == 0U || output_text == NULL ||
        output_capacity == 0U || error == NULL) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 100,
            "invalid frame library read request");
    }
    if (!runtimec_kvspace_access_key_text(frame_path, frame_text,
                                          sizeof(frame_text), NULL)) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 101,
            "frame path is not an absolute key");
    }
    directory_size = strlen(frame_text);
    if (directory_size + 1U >= sizeof(frame_text)) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 102, "frame path is too long");
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
            error, RUNTIMEC_STATUS_INVALID, 103,
            "library key formatting failed");
    }

    const uint8_t *wire = NULL;
    uint32_t wire_size = 0U;
    if (runtimec_kvspace_access_read_wire(adapter, library_key, &wire,
                                          &wire_size, error) !=
            RUNTIMEC_STATUS_SUCCESS ||
        wire == NULL) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, 104,
            "frame has no library");
    }
    if (wire_size < KVSPACE_XVALUE_HEAD_LEN) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 105, "library wire is truncated");
    }
    kvspaceXvalueHead head;
    memcpy(&head, wire, sizeof(head));
    const uint64_t body_size = kvspaceXvalueHeadLen(&head);
    if (body_size + 1U > (uint64_t)output_capacity ||
        body_size > (uint64_t)UINT32_MAX) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_INVALID, 106,
            "library text exceeds the output buffer");
    }
    memcpy(output_text, wire + KVSPACE_XVALUE_HEAD_LEN, (size_t)body_size);
    output_text[body_size] = '\0';
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
/*
 * 睡一段。handoff 轮询用它代替旧的 `kvspaceWatch`——存储接口没有等待原语，
 * 而这里需要的就是「等一个间隔再查一次」。POSIX 的 `nanosleep` 在 C11 严格模式
 * 下要显式声明特性宏（与 `vmbin/handlers/host.c` 的做法一致）。
 */
static void runtimec_kvspace_access_sleep(uint64_t duration_nanoseconds) {
    if (duration_nanoseconds == 0U) {
        return;
    }
    const struct timespec interval = {
        .tv_sec = (time_t)(duration_nanoseconds / UINT64_C(1000000000)),
        .tv_nsec = (long)(duration_nanoseconds % UINT64_C(1000000000)),
    };
    nanosleep(&interval, NULL);
}

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
            "char/utf8", &wire, &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 56, "handoff value encoding failed");
    }
    char error_text[256];
    error_text[0] = '\0';
    const bool wrote = runtimec_kvspace_write_wire(
        adapter->handle, queue_key, wire, (uint32_t)wire_size, error_text,
        (uint32_t)sizeof(error_text));
    const int set_status = wrote ? 0 : 1;
    free(wire);
    if (set_status != 0) {
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_ERROR, 56,
            error_text[0] != '\0' ? error_text : "kvspace write failed");
    }
    uint64_t waited_nanoseconds = 0U;
    while (waited_nanoseconds <
           RUNTIMEC_KVSPACE_HANDOFF_DEADLINE_NANOSECONDS) {
        const uint8_t *observed = NULL;
        uint32_t observed_size = 0U;
        if (runtimec_kvspace_access_read_wire(adapter, queue_key, &observed,
                                              &observed_size, error) !=
            RUNTIMEC_STATUS_SUCCESS) {
            return runtimec_kvspace_access_fail(
                error, RUNTIMEC_STATUS_ERROR, 57, "handoff queue read failed");
        }
        if (observed == NULL || observed_size == 0U) {
            return RUNTIMEC_STATUS_SUCCESS;
        }
        /*
         * 纯轮询，不再 `kvspaceWatch`：存储接口没有等待原语（架构师裁决
         * 2026-11-10，`kvspace·watch` 语言算子同时停用）。完成条件本来就
         * 是「条目被认领方删除」，轮询 `Get` 判定与阻塞等待的判据一致——
         * 之前用 watch 只是拿它当「睡一个间隔」，现在直接睡。
         *
         * 同理不再 `ViewQuiesce`/`ViewAck`：新接口没有视图/epoch，
         * `offset` 的生命周期由「下一次对该 key 的 Realloc/Free」界定。
         */
        runtimec_kvspace_access_sleep(RUNTIMEC_KVSPACE_HANDOFF_POLL_NANOSECONDS);
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
    /*
     * 排序缓存随 adapter 一起分配。它必须在这里建好：`list_directory` 拿不到它
     * 就无法枚举，而「第一次 list 时才懒建」会把一次分配失败拖到执行热路径上，
     * 那时没有干净的错误通道。
     */
    access->directory = runtimec_allocator_allocate(
        &request->allocator, sizeof(*access->directory),
        alignof(runtimec_key_directory_t));
    if (access->directory == NULL) {
        runtimec_status_t cleanup = runtimec_allocator_deallocate(
            &request->allocator, access, sizeof(*access),
            alignof(runtimec_kvspace_access_t));
        (void)cleanup;
        return runtimec_kvspace_access_fail(
            error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 59,
            "directory cache allocation failed");
    }
    runtimec_key_directory_reset(access->directory, &request->allocator, access,
                                 runtimec_kvspace_access_directory_source);
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
    if (access->directory != NULL) {
        runtimec_status_t released =
            runtimec_key_directory_destroy(access->directory);
        runtimec_allocator_deallocate(&allocator, access->directory,
                                      sizeof(*access->directory),
                                      alignof(runtimec_key_directory_t));
        access->directory = NULL;
        if (released != RUNTIMEC_STATUS_SUCCESS) {
            return runtimec_kvspace_access_fail(
                error, RUNTIMEC_STATUS_ERROR, 61,
                "directory cache deallocation violated the allocator contract");
        }
    }
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

/* 有效长度 = head 的 `bodylen`。head 里没有 layout / reserved 字段，attr 区没有
 * reallen tag：wire 上不表达容量，容量是存储后端的分配属性。
 *
 * 仍校验 padding 为零——它是 head ABI 的一部分，读取路径必须先过这一关。 */
static bool runtimec_value_effective_length(const kvspaceXvalueHead *head,
                                            uint64_t *output_length) {
    *output_length = 0U;
    if (!kvspaceXvalueHeadPaddingIsZero(head)) {
        return false;
    }
    *output_length = kvspaceXvalueHeadLen(head);
    return true;
}

/* 从值体地址往前挪头长度得到值头，有效长度从值头读。不可信一律返回 0——本函数
 * 的返回类型是「长度」不是「状态」，0 与「空 body」在这里同值。 */
uint32_t runtimec_value_body_length(runtimec_value_body_address_t body_address) {
    if (body_address == RUNTIMEC_VALUE_BODY_ADDRESS_NONE ||
        body_address < RUNTIMEC_XVALUE_BODY_OFFSET_BYTES) {
        return 0U;
    }
    const kvspaceXvalueHead *head = (const kvspaceXvalueHead *)(uintptr_t)(
        body_address - RUNTIMEC_XVALUE_BODY_OFFSET_BYTES);
    uint64_t body_length = 0U;

    if (!runtimec_value_effective_length(head, &body_length)) {
        return 0U;
    }
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
 * 名字后面跟着 langtype attr，于是外部扩展永远认领不到队列项——「停在 waiting
 * 却没进队列」，与「跑完了」无法区分。
 *
 * **长度一律来自头，不从调用方传进来的 size 推**：attr 区长度只有头知道；
 * 用 `wire.size` 减出来的永远是「body + attr」。
 *
 * 任何不自洽（`padding` 非零 / body 或 attr 超出 wire）都返回空视图（NULL / 0），
 * 不返回「有指针无长度」的半状态——**不猜，不截断到看起来合理**。
 *
 * **不校验 langtype 可解析性**（架构师裁决 2026-11-06）：本函数的职责是「切出
 * body」，langtype 可解析性归调用方。它连 langtype 都不读。
 */
runtimec_string_view_t runtimec_xvalue_body_from_wire(
    runtimec_bytes_view_t wire) {
    const size_t offset = (size_t)RUNTIMEC_XVALUE_BODY_OFFSET_BYTES;
    if (wire.data == NULL || wire.size < offset) {
        return runtimec_string_view_make(NULL, 0U);
    }
    kvspaceXvalueHead head;
    memcpy(&head, wire.data, sizeof(head));
    if (!kvspaceXvalueHeadPaddingIsZero(&head)) {
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
