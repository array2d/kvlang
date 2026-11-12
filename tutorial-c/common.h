// common.h — tutorial 公共助手：新存储 ABI 上的单键读写与目录枚举。
//
// 新 ABI 的模型只有三个动作，示例也正是按这三步写的，不含任何兼容层：
//   读：kvspaceResolveOffset(key) → offset，再读 base+offset 处的 head/body/attr
//   写：kvspaceRealloc(key, bodylen, attrlen) → offset，再直接写 base+offset
//   目录：kvspaceListlen 取 N/M → kvspaceList 一次取全部名字
//
// base 来自 kvspaceXvalueInProcessAddress(h)，对 shm 后端稳定；durable 没有
// 进程内值区，返回 NULL——那时读写走不了这套，示例只用 shm。
//
// XValue 的字节布局由 kvspace_xvalue.h / kvlang/xvalue.h 定义，这里不重复。
#ifndef KVSPACE_TUTORIAL_COMMON_H
#define KVSPACE_TUTORIAL_COMMON_H

#include <kvspace/kvspace.h>
#include <kvspace/kvspace_xvalue.h>
#include <kvlangxvalue.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KV_ERR_CAP 256

// 一条值的只读视图：head 在 wire 起点，body 紧随其后。attr 用 GetAttr 取。
typedef struct {
    const uint8_t *head;
    uint32_t head_len;   // 恒为 KVSPACE_XVALUE_HEAD_LEN
    const uint8_t *body;
    uint32_t body_len;   // 有效字节数
} kv_value;

// ── 连接 ─────────────────────────────────────────────────────────────

static inline void *kv_open_shm(const char *path) {
    char dsn[512];
    snprintf(dsn, sizeof dsn, "shm://%s", path);
    return kvspaceConnect(dsn);
}

// 进程内值区基址。每个读/写都经它把 offset 变成地址——offset 本身不是指针。
static inline uint8_t *kv_base(void *h) { return kvspaceXvalueInProcessAddress(h); }

// ── 读 ───────────────────────────────────────────────────────────────

// 解析 key 并填出只读视图。值不存在或 wire 不合法返回 -1。
// 注意 body 指针指向**后端常驻内存**，随该 key 被改写或 handle 关闭而失效；
// 需要留存就必须自己拷出去（示例里不需要，都是现读现打印）。
static inline int kv_read(void *h, const char *key, kv_value *out) {
    uint64_t offset = 0;
    char err[KV_ERR_CAP];
    if (kvspaceResolveOffset(h, key, 0, &offset, err, sizeof err) != KVSPACE_OK)
        return -1;
    uint8_t *base = kv_base(h);
    if (!base)
        return -1;
    const uint8_t *wire = base + offset;
    const kvspaceXvalueHead *head = (const kvspaceXvalueHead *)(const void *)wire;
    const uint64_t total = kvspaceXvalueHeadTotalLen(head);
    if (total == 0)
        return -1;
    out->head = wire;
    out->head_len = KVSPACE_XVALUE_HEAD_LEN;
    out->body = wire + KVSPACE_XVALUE_HEAD_LEN;
    out->body_len = (uint32_t)kvspaceXvalueHeadLen(head);
    return 0;
}

// 取 langtype（剥掉前导 [dims]，只留基 kind）。取不到返回空串。
// 整条 wire 的长度由 head 自己算得出，不需要调用方再传——这正是 attrslen 放在
// head 里的好处：attr 区没有第二层长度前缀。
static inline const char *kv_kind(const kv_value *v, char *kind, size_t cap) {
    if (cap)
        kind[0] = 0;
    const kvspaceXvalueHead *head = (const kvspaceXvalueHead *)(const void *)v->head;
    const uint64_t total = kvspaceXvalueHeadTotalLen(head);
    if (total == 0 || cap == 0)
        return kind;
    const uint8_t *lt = NULL;
    uint16_t n = kvlangXvalueAttrLangtypeBounded(v->head, total, &lt);
    const uint8_t *close = lt && n && lt[0] == '[' ? memchr(lt, ']', n) : NULL;
    if (close) {
        n -= (uint16_t)(close + 1 - lt);
        lt = close + 1;
    }
    if (n >= cap)
        n = (uint16_t)(cap - 1);
    if (n)
        memcpy(kind, lt, n);
    kind[n] = 0;
    return kind;
}

// ── 写 ───────────────────────────────────────────────────────────────

// 写入一条值：先 realloc 出 body+attr 空间，再直接写 base+offset。
// langtype 为空/NULL 时不写 attr 记录。
static inline int kv_write(void *h, const char *key, const uint8_t *body,
                           uint64_t body_len, const char *langtype) {
    const uint16_t lt_len = langtype && langtype[0] ? (uint16_t)strlen(langtype) : 0;
    const uint64_t attr_len = lt_len ? 3u + lt_len : 0u;
    uint64_t offset = 0;
    char err[KV_ERR_CAP];
    if (kvspaceRealloc(h, key, body_len, attr_len, &offset, err, sizeof err) !=
        KVSPACE_OK)
        return -1;
    uint8_t *base = kv_base(h);
    if (!base)
        return -1;
    uint8_t *wire = base + offset;
    if (body_len && body)
        memcpy(wire + KVSPACE_XVALUE_HEAD_LEN, body, (size_t)body_len);
    if (lt_len) {
        uint8_t *attr = wire + KVSPACE_XVALUE_HEAD_LEN + body_len;
        attr[0] = KVLANG_XVALUE_ATTR_TAG_LANGTYPE;
        attr[1] = (uint8_t)lt_len;
        attr[2] = (uint8_t)(lt_len >> 8);
        memcpy(attr + 3, langtype, lt_len);
    }
    return 0;
}

static inline int kv_set_int(void *h, const char *key, int64_t v) {
    return kv_write(h, key, (const uint8_t *)&v, sizeof v, "int64");
}
static inline int kv_set_str(void *h, const char *key, const char *s) {
    return kv_write(h, key, (const uint8_t *)s, strlen(s), "char/utf8");
}

// 下面三个是给「同一段 body 要写成不同 langtype」的示例用的：它们在示例里
// 各自扮演「造一条值」的角色，与新 ABI 的 realloc+直写是同一步，不需要把
// body 先打包成 wire 再拆开——那样反而多了两次拷贝。
static inline int kv_set_bytes(void *h, const char *key, const uint8_t *raw,
                               uint64_t len, const char *langtype) {
    return kv_write(h, key, raw, len, langtype);
}
static inline int kv_set_raw(void *h, const char *key, const uint8_t *raw,
                             uint64_t len) {
    return kv_write(h, key, raw, len, NULL);
}

static inline void kv_del(void *h, const char *key) {
    char err[KV_ERR_CAP];
    (void)kvspaceFree(h, key, err, sizeof err);
}

static inline void kv_mkindex(void *h, const char *prefix) {
    char err[KV_ERR_CAP];
    (void)kvspaceMkindex(h, prefix, err, sizeof err);
}

// ── 目录 ─────────────────────────────────────────────────────────────

// 一次取全部直接子项名（NUL 分隔，目录项带尾 `/`），拼成 \n 分隔的 malloc
// 缓冲交给调用方 free。N 由 listlen 给出，M 给最长名字节数——两者配合就能
// 一次分配到位，不必先试错容量。
static inline int kv_list(void *h, const char *prefix, uint8_t **out, uint32_t *out_len) {
    *out = NULL;
    *out_len = 0;
    int64_t count = 0;
    uint32_t max_name = 0;
    char err[KV_ERR_CAP];
    if (kvspaceListlen(h, prefix, &count, &max_name, err, sizeof err) != KVSPACE_OK)
        return -1;
    if (count <= 0)
        return 0;
    char *names = malloc((size_t)count * (max_name + 1));
    uint32_t length = 0, listed = 0;
    uint64_t generation = 0;
    if (kvspaceList(h, prefix, names, (uint32_t)((size_t)count * (max_name + 1)),
                    &length, &listed, &generation, err, sizeof err) != KVSPACE_OK) {
        free(names);
        return -1;
    }
    // NUL 分隔 → \n 分隔（示例打印用这套分隔符，保留原可读性）。
    uint8_t *buf = malloc(length);
    uint32_t used = 0;
    for (uint32_t i = 0; i < length; i++) {
        if (names[i] == '\0') {
            if (used > 0)
                buf[used++] = '\n';
        } else {
            buf[used++] = (uint8_t)names[i];
        }
    }
    free(names);
    *out = buf;
    *out_len = used;
    return 0;
}

#endif
