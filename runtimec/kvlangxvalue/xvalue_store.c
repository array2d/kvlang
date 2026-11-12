/*
 * 组件：xvalue_store — kvspace 存储 + kvlangxvalue 编解码的缝合层。
 *
 * 本文件不含任何「类型是什么」的判断：langtype 解析在 kvlangxvalue/langtype，
 * 文本↔字节在 kvlangxvalue/value_text，寻址在 kvspace。这里只按顺序调它们。
 */

#include <kvspace/kvspace.h>
#include <kvspace/kvspace_xvalue.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kvlang/xvalue_store.h"
#include "kvlangxvalue/langtype.h"
#include "kvlangxvalue/value_text.h"
#include "kvlangxvalue/view.h"

#define XVALUE_STORE_HEAD_LEN KVSPACE_XVALUE_HEAD_LEN
#define XVALUE_STORE_LANGTYPE_TAG 1u
#define XVALUE_STORE_ERROR_TEXT_CAPACITY 512u

/* 失败即终止。**这是本文件唯一的错误出口**——没有返回值、没有 errno、没有降级。 */
static void die(const char *message, const char *detail) {
    if (detail) {
        fprintf(stderr, "kvlang-xvalue-store: %s: %s\n", message, detail);
    } else {
        fprintf(stderr, "kvlang-xvalue-store: %s\n", message);
    }
    abort();
}

static void die_kvspace(const char *operation, int status, const char *error) {
    fprintf(stderr, "kvlang-xvalue-store: %s failed (status %d): %s\n", operation,
            status, error[0] ? error : "(无消息)");
    abort();
}

/* ── 一条 wire 的拆解 ──────────────────────────────────────────────── */

typedef struct stored_xvalue {
    uint64_t body_length;
    uint16_t attrs_length;
    const uint8_t *body;
    const uint8_t *attrs;
} stored_xvalue_t;

/* 校验 head 自洽并拆出三段。**畸形一律终止**：读一条坏 wire 得到的是「看起来正常」
   的长度，那比读失败更危险。 */
static stored_xvalue_t dissect(const uint8_t *wire, uint64_t wire_length,
                               const char *key) {
    if (wire_length < XVALUE_STORE_HEAD_LEN) {
        die("wire 短于 16 字节 head", key);
    }
    const kvspaceXvalueHead *head = (const kvspaceXvalueHead *)(const void *)wire;
    const uint64_t total = kvspaceXvalueHeadTotalLen(head);
    if (total == 0 || total > wire_length) {
        die("wire head 不自洽（padding 非零或总长越界）", key);
    }
    stored_xvalue_t result;
    result.body_length = kvspaceXvalueHeadLen(head);
    result.attrs_length = kvspaceXvalueHeadAttrsLen(head);
    result.body = wire + XVALUE_STORE_HEAD_LEN;
    result.attrs = result.body + result.body_length;
    return result;
}

/* 从 attr 区取 tag 1（langtype）。直接用 kvspace 的那对读写入口——
   `kvspaceXvalueGetAttr` 已是 attr 区的**唯一实现**，且它内部已经「扫完整区再决定
   返回值」（命中目标 tag 不算合法，tag 2 出现在任何位置都拒）。在这里再扫一遍就是
   第二份格式知识，症状是两边对「什么算畸形」判得不一样。

   **返回 -1 是两种情况合一**：没有这条记录，或者 wire 畸形。调用方无法靠返回码区分，
   因为 kvspace 的契约就是这么定的（见 kvspace_xvalue.h）。本组件对两者的处置相同：
   都终止。区别只在错误文案——而那个区别不值得为它复制一遍扫描器。 */
static bool find_langtype(const uint8_t *wire, uint64_t wire_length,
                          const uint8_t **out_data, uint16_t *out_length) {
    return kvspaceXvalueGetAttr(wire, wire_length, XVALUE_STORE_LANGTYPE_TAG,
                                out_data, out_length) == 0;
}

/* ── 读 ────────────────────────────────────────────────────────────── */

/* 取一条 wire 的只读视图。key 不存在返回 false（**唯一允许的 false**）。 */
static bool load(kvlang_xvalue_store_t store, const char *key,
                 const uint8_t **out_wire, uint64_t *out_length) {
    uint64_t offset = 0;
    char error[XVALUE_STORE_ERROR_TEXT_CAPACITY] = {0};
    const int status = kvspaceResolveOffset(store, key, KVSPACE_ANCHOR_ROOT, &offset,
                                            error, sizeof error);
    if (status == KVSPACE_MISSING) {
        return false;
    }
    if (status != KVSPACE_OK) {
        die_kvspace("resolve", status, error);
    }
    const uint8_t *base = kvspaceXvalueInProcessAddress(store);
    if (base == NULL) {
        die("存储没有进程内值区（非映射后端）", key);
    }
    const uint8_t *wire = base + offset;
    const kvspaceXvalueHead *head = (const kvspaceXvalueHead *)(const void *)wire;
    const uint64_t total = kvspaceXvalueHeadTotalLen(head);
    if (total == 0) {
        die("wire head 不自洽", key);
    }
    *out_wire = wire;
    *out_length = total;
    return true;
}

/* ── 写 ────────────────────────────────────────────────────────────── */

/* 拼一条 wire：head + body + 一条 langtype 记录（langtype 为空则无记录）。
 *
 * 记录**不由这里手写**：`kvspaceXvalueSetAttr` 是 attr 区的唯一写入入口，它负责
 * `[tag][len][data]` 的排布、attrslen 的回填、tag 2/0 的拒绝。在这里再拼一遍就是
 * 第二份格式知识。
 *
 * 分配按「head + body + 3 + langtype 长度」一次到位，`attrslen` 先置 0——SetAttr
 * 期望的是一条**自洽的空 attr 区**，它会自己把记录追加进去并把 attrslen 填好。 */
static uint8_t *assemble(runtimec_bytes_view_t body,
                          runtimec_string_view_t langtype, uint64_t *out_length) {
    if (langtype.size > UINT16_MAX) {
        die("langtype 过长", NULL);
    }
    const uint64_t record_length = langtype.size ? 3u + langtype.size : 0u;
    const uint64_t capacity = XVALUE_STORE_HEAD_LEN + body.size + record_length;
    uint8_t *wire = (uint8_t *)calloc(1, (size_t)capacity);
    if (wire == NULL) {
        die("分配 wire 失败", NULL);
    }
    kvspaceXvalueHead head = {.padding = {0u, 0u, 0u, 0u, 0u, 0u}};
    kvspaceXvalueHeadSetLen(&head, body.size);
    kvspaceXvalueHeadSetAttrsLen(&head, 0u);
    memcpy(wire, &head, sizeof head);
    memcpy(wire + XVALUE_STORE_HEAD_LEN, body.data, body.size);
    if (langtype.size == 0) {
        *out_length = XVALUE_STORE_HEAD_LEN + body.size;
        return wire;
    }
    uint64_t new_length = 0;
    if (kvspaceXvalueSetAttr(wire, capacity, XVALUE_STORE_LANGTYPE_TAG,
                             (const uint8_t *)langtype.data,
                             (uint16_t)langtype.size, &new_length) != 0) {
        free(wire);
        die("写入 langtype 记录失败", NULL);
    }
    *out_length = new_length;
    return wire;
}

/* Realloc 拿到偏移后直接写值区。容量内原地、超容量搬移都由 kvspace 决定。 */
static void store_wire(kvlang_xvalue_store_t store, const char *key,
                       const uint8_t *wire, uint64_t wire_length) {
    uint64_t offset = 0;
    char error[XVALUE_STORE_ERROR_TEXT_CAPACITY] = {0};
    const int status = kvspaceRealloc(store, key, wire_length, 0, &offset, error,
                                      sizeof error);
    if (status != KVSPACE_OK) {
        die_kvspace("realloc", status, error);
    }
    uint8_t *base = kvspaceXvalueInProcessAddress(store);
    if (base == NULL) {
        die("存储没有进程内值区（非映射后端）", key);
    }
    memcpy(base + offset, wire, (size_t)wire_length);
}

/* 文本 → body → wire → 存储。langtype 由调用方给（write_new）或从旧值读（write_typed）。 */
static void write_with_langtype(kvlang_xvalue_store_t store, const char *key,
                                runtimec_string_view_t langtype,
                                const char *value_text) {
    runtimec_string_view_t text = {value_text, strlen(value_text)};
    size_t body_length = 0;
    runtimec_value_text_failure_t failure = RUNTIMEC_VALUE_TEXT_OK;
    if (!runtimec_value_text_to_bytes(langtype, text, NULL, 0, &body_length,
                                      &failure)) {
        die(runtimec_value_text_failure_text(failure), key);
    }
    uint8_t *body = (uint8_t *)malloc(body_length ? body_length : 1u);
    if (body == NULL) {
        die("分配 body 失败", key);
    }
    if (!runtimec_value_text_to_bytes(langtype, text, body, body_length,
                                      &body_length, &failure)) {
        free(body);
        die(runtimec_value_text_failure_text(failure), key);
    }
    runtimec_bytes_view_t body_view = {body, body_length};
    uint64_t wire_length = 0;
    uint8_t *wire = assemble(body_view, langtype, &wire_length);
    store_wire(store, key, wire, wire_length);
    free(wire);
    free(body);
}

/* ── 公开 ABI ──────────────────────────────────────────────────────── */

kvlang_xvalue_store_t kvlang_xvalue_store_open(const char *dsn) {
    return (kvlang_xvalue_store_t)kvspaceConnect(dsn);
}

void kvlang_xvalue_store_close(kvlang_xvalue_store_t store) {
    kvspaceClose(store);
}

void kvlang_xvalue_store_write_new(kvlang_xvalue_store_t store, const char *key,
                                   const char *langtype, const char *value_text) {
    if (store == NULL || key == NULL || langtype == NULL || value_text == NULL) {
        die("write_new 参数为空", NULL);
    }
    /* 空 langtype 不是「不约束」而是畸形：write_new 的语义就是「按给定类型写」。 */
    runtimec_string_view_t langtype_view = {langtype, strlen(langtype)};
    if (langtype_view.size == 0) {
        die("write_new 的 langtype 不能为空", key);
    }
    write_with_langtype(store, key, langtype_view, value_text);
}

void kvlang_xvalue_store_write_typed(kvlang_xvalue_store_t store, const char *key,
                                     const char *value_text) {
    if (store == NULL || key == NULL || value_text == NULL) {
        die("write_typed 参数为空", NULL);
    }
    const uint8_t *wire = NULL;
    uint64_t wire_length = 0;
    if (!load(store, key, &wire, &wire_length)) {
        die("write_typed 的目标不存在（类型不猜）", key);
    }
    const uint8_t *langtype_data = NULL;
    uint16_t langtype_length = 0;
    if (!find_langtype(wire, wire_length, &langtype_data, &langtype_length)) {
        die("目标没有 langtype 记录（类型不猜）", key);
    }
    if (langtype_length == 0) {
        die("目标没有 langtype 记录（类型不猜）", key);
    }
    runtimec_string_view_t langtype_view = {(const char *)langtype_data,
                                            langtype_length};
    write_with_langtype(store, key, langtype_view, value_text);
}

bool kvlang_xvalue_store_read(kvlang_xvalue_store_t store, const char *key,
                              char *langtype_out, size_t langtype_capacity,
                              char *value_out, size_t value_capacity) {
    if (store == NULL || key == NULL || langtype_out == NULL ||
        langtype_capacity == 0 || value_out == NULL || value_capacity == 0) {
        die("read 参数为空或缓冲容量为 0", NULL);
    }
    const uint8_t *wire = NULL;
    uint64_t wire_length = 0;
    if (!load(store, key, &wire, &wire_length)) {
        return false;
    }
    const stored_xvalue_t value = dissect(wire, wire_length, key);
    const uint8_t *langtype_data = NULL;
    uint16_t langtype_length = 0;
    if (!find_langtype(wire, wire_length, &langtype_data, &langtype_length)) {
        die("目标没有 langtype 记录", key);
    }
    if (langtype_length == 0) {
        die("目标没有 langtype 记录", key);
    }
    if ((size_t)langtype_length + 1 > langtype_capacity) {
        die("langtype 输出缓冲不足（不截断）", key);
    }
    memcpy(langtype_out, langtype_data, langtype_length);
    langtype_out[langtype_length] = '\0';

    runtimec_string_view_t langtype_view = {(const char *)langtype_data,
                                            langtype_length};
    runtimec_bytes_view_t body_view = {value.body, value.body_length};
    size_t text_length = 0;
    runtimec_value_text_failure_t failure = RUNTIMEC_VALUE_TEXT_OK;
    if (!runtimec_value_text_to_text(langtype_view, body_view, value_out,
                                     value_capacity, &text_length, &failure)) {
        die(runtimec_value_text_failure_text(failure), key);
    }
    return true;
}
