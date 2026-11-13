
#ifndef PATHSPACE_TUTORIAL_COMMON_H
#define PATHSPACE_TUTORIAL_COMMON_H

#include <pathspace/pathspace.h>
#include <pathspace/pathspace_xvalue.h>
#include <pathlang/xvalue.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KV_ERR_CAP 256

typedef struct {
    const uint8_t *head;
    uint32_t head_len;   
    const uint8_t *body;
    uint32_t body_len;   
} kv_value;

static inline void *kv_open_shm(const char *path) {
    char dsn[512];
    snprintf(dsn, sizeof dsn, "shm://%s", path);
    return pathspaceConnect(dsn);
}

static inline uint8_t *kv_base(void *h) { return pathspaceXvalueInProcessAddress(h); }

static inline int kv_read(void *h, const char *key, kv_value *out) {
    uint64_t offset = 0;
    char err[KV_ERR_CAP];
    if (pathspaceResolveOffset(h, key, 0, &offset, err, sizeof err) != PATHSPACE_OK)
        return -1;
    uint8_t *base = kv_base(h);
    if (!base)
        return -1;
    const uint8_t *wire = base + offset;
    const pathspaceXvalueHead *head = (const pathspaceXvalueHead *)(const void *)wire;
    const uint64_t total = pathspaceXvalueHeadTotalLen(head);
    if (total == 0)
        return -1;
    out->head = wire;
    out->head_len = PATHSPACE_XVALUE_HEAD_LEN;
    out->body = wire + PATHSPACE_XVALUE_HEAD_LEN;
    out->body_len = (uint32_t)pathspaceXvalueHeadLen(head);
    return 0;
}

static inline const char *kv_kind(const kv_value *v, char *kind, size_t cap) {
    if (cap)
        kind[0] = 0;
    const pathspaceXvalueHead *head = (const pathspaceXvalueHead *)(const void *)v->head;
    const uint64_t total = pathspaceXvalueHeadTotalLen(head);
    if (total == 0 || cap == 0)
        return kind;
    const uint8_t *lt = NULL;
    uint16_t n = pathlangXvalueHeadLangtypeBounded(v->head, total, &lt);
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

static inline int kv_write(void *h, const char *key, const uint8_t *body,
                           uint64_t body_len, const char *langtype) {
    const uint16_t lt_len = langtype && langtype[0] ? (uint16_t)strlen(langtype) : 0;
    const uint64_t attr_len = lt_len ? 3u + lt_len : 0u;
    uint64_t offset = 0;
    char err[KV_ERR_CAP];
    if (pathspaceRealloc(h, key, body_len, attr_len, &offset, err, sizeof err) !=
        PATHSPACE_OK)
        return -1;
    uint8_t *base = kv_base(h);
    if (!base)
        return -1;
    uint8_t *wire = base + offset;
    if (body_len && body)
        memcpy(wire + PATHSPACE_XVALUE_HEAD_LEN, body, (size_t)body_len);
    if (lt_len) {
        uint8_t *attr = wire + PATHSPACE_XVALUE_HEAD_LEN + body_len;
        attr[0] = PATHLANG_XVALUE_ATTR_TAG_LANGTYPE;
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
    (void)pathspaceFree(h, key, err, sizeof err);
}

static inline void kv_mkindex(void *h, const char *prefix) {
    char err[KV_ERR_CAP];
    (void)pathspaceMkindex(h, prefix, err, sizeof err);
}

static inline int kv_list(void *h, const char *prefix, uint8_t **out, uint32_t *out_len) {
    *out = NULL;
    *out_len = 0;
    int64_t count = 0;
    uint32_t max_name = 0;
    char err[KV_ERR_CAP];
    if (pathspaceListlen(h, prefix, &count, &max_name, err, sizeof err) != PATHSPACE_OK)
        return -1;
    if (count <= 0)
        return 0;
    char *names = malloc((size_t)count * (max_name + 1));
    uint32_t length = 0, listed = 0;
    uint64_t generation = 0;
    if (pathspaceList(h, prefix, names, (uint32_t)((size_t)count * (max_name + 1)),
                    &length, &listed, &generation, err, sizeof err) != PATHSPACE_OK) {
        free(names);
        return -1;
    }
    
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
