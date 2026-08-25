#include "runtime_internal.h"

/* kv 访问统一走 kvspace-durable 兼容 C ABI（kvspace*）。
 * 后端由链接的 kvspace 库决定（kvspace-durable / kvspace-c 均导出同一 ABI）。 */

static void kvlangXvalueCopyMalloc(kvlangXvalue_t *out, const uint8_t *d, uint32_t len) {
    if (len > 0) {
        out->data = malloc(len);
        memcpy(out->data, d, len);
        out->len = len;
    }
}

kvlangKv_t *kvlangKvConnect(const char *dsn) {
    kvlangKv_t *k = calloc(1, sizeof(*k));
    k->h = kvspaceConnect(dsn);
    if (!k->h) { free(k); return NULL; }
    return k;
}

void kvlangKvDisconnect(kvlangKv_t *k) {
    if (!k) return;
    if (k->h) kvspaceClose(k->h);
    free(k);
}

int kvlangKvGetOne(kvlangKv_t *k, const char *key, kvlangXvalue_t *out) {
    kvlangXvalueZero(out);
    uint8_t *d; uint32_t len;
    if (kvspaceGet(k->h, key, &d, &len) != 0) return -1;
    kvlangXvalueCopyMalloc(out, d, len);
    kvspaceBytesFree(d, len);
    return 0;
}

int kvlangKvGetBatch(kvlangKv_t *k, const char *prefix, char **names, int n, kvlangXvalue_t *out) {
    for (int i = 0; i < n; i++) kvlangXvalueZero(&out[i]);
    const char **ns = malloc(sizeof(char *) * (size_t)n);
    for (int i = 0; i < n; i++) ns[i] = names[i];
    uint8_t *d; uint32_t len;
    int rc = kvspaceGetBatch(k->h, prefix, ns, (uint32_t)n, &d, &len);
    free(ns);
    if (rc != 0) return rc;
    uint32_t off = 0;
    for (int i = 0; i < n; i++) {
        if (off + 4 > len) break;
        uint32_t vl = (uint32_t)d[off] | ((uint32_t)d[off + 1] << 8) | ((uint32_t)d[off + 2] << 16) | ((uint32_t)d[off + 3] << 24);
        off += 4;
        if (vl > 0 && off + vl <= len) kvlangXvalueCopyMalloc(&out[i], d + off, vl);
        off += vl;
    }
    kvspaceBytesFree(d, len);
    return 0;
}

int kvlangKvSet(kvlangKv_t *k, const kvlangKvPair_t *pairs, int n, char *err, uint32_t err_cap) {
    if (n <= 0) return 0;
    const char **keys = malloc(sizeof(char *) * (size_t)n);
    uint32_t *lens = malloc(sizeof(uint32_t) * (size_t)n);
    size_t total = 0;
    for (int i = 0; i < n; i++) {
        keys[i] = pairs[i].key;
        lens[i] = pairs[i].val.len;
        total += pairs[i].val.len;
    }
    uint8_t *vals = malloc(total ? total : 1);
    size_t off = 0;
    for (int i = 0; i < n; i++) {
        if (pairs[i].val.len) memcpy(vals + off, pairs[i].val.data, pairs[i].val.len);
        off += pairs[i].val.len;
    }
    int rc = kvspaceSet(k->h, keys, vals, lens, (uint32_t)n, err, err_cap);
    free(keys); free(lens); free(vals);
    return rc;
}

int kvlangKvDel(kvlangKv_t *k, const char *key, char *err, uint32_t err_cap) {
    const char *keys[1] = { key };
    return kvspaceDel(k->h, keys, 1, err, err_cap);
}

int kvlangKvDelTree(kvlangKv_t *k, const char *prefix, char *err, uint32_t err_cap) {
    return kvspaceDelTree(k->h, prefix, err, err_cap);
}

int kvlangKvMkindex(kvlangKv_t *k, const char *path, char *err, uint32_t err_cap) {
    return kvspaceMkindex(k->h, path, err, err_cap);
}

int kvlangKvExtIndex(kvlangKv_t *k, const char *path, const char *ext, char *err, uint32_t err_cap) {
    return kvspaceMkindexExt(k->h, path, ext, err, err_cap);
}

int kvlangKvDelExtIndex(kvlangKv_t *k, const char *path, char *err, uint32_t err_cap) {
    return kvspaceRmindexExt(k->h, path, err, err_cap);
}

int kvlangKvList(kvlangKv_t *k, const char *prefix, bool expand_ext, bool resolve,
            char ***out_names, int *out_count) {
    *out_names = NULL; *out_count = 0;
    uint8_t *d; uint32_t len;
    if (kvspaceList(k->h, prefix, expand_ext ? 1 : 0, resolve ? 1 : 0, &d, &len) != 0) return -1;
    if (len == 0) { if (d) kvspaceBytesFree(d, len); return 0; }
    char *s = malloc((size_t)len + 1);
    memcpy(s, d, len); s[len] = 0;
    kvspaceBytesFree(d, len);
    int cnt = 1;
    for (uint32_t i = 0; i < len; i++) if (s[i] == '\n') cnt++;
    char **names = malloc(sizeof(char *) * (size_t)cnt);
    int idx = 0;
    char *save = NULL;
    for (char *tok = strtok_r(s, "\n", &save); tok; tok = strtok_r(NULL, "\n", &save))
        names[idx++] = strdup(tok);
    free(s);
    *out_names = names; *out_count = idx;
    return 0;
}

int kvlangKvWatch(kvlangKv_t *k, const char *key, const kvlangXvalue_t *target, uint64_t tick_ns, kvlangXvalue_t *out) {
    kvlangXvalueZero(out);
    const uint8_t *t = target->data ? target->data : (const uint8_t *)"";
    uint32_t tl = target->len;
    uint8_t *d; uint32_t len;
    if (kvspaceWatch(k->h, key, t, tl, tick_ns, &d, &len) != 0) return -1;
    kvlangXvalueCopyMalloc(out, d, len);
    kvspaceBytesFree(d, len);
    return 0;
}

int kvlangKvNotify(kvlangKv_t *k, const char *key, const kvlangXvalue_t *val, char *err, uint32_t err_cap) {
    const uint8_t *p = val->data ? val->data : (const uint8_t *)"";
    return kvspaceNotify(k->h, key, p, val->len, err, err_cap);
}

int kvlangKvTake(kvlangKv_t *k, const char *key, uint64_t timeout_ns, kvlangXvalue_t *out) {
    kvlangXvalueZero(out);
    uint8_t *d; uint32_t len;
    if (kvspaceTake(k->h, key, timeout_ns, &d, &len) != 0) return -1;
    kvlangXvalueCopyMalloc(out, d, len);
    kvspaceBytesFree(d, len);
    return 0;
}

int kvlangKvIncr(kvlangKv_t *k, const char *key, int64_t *out, char *err, uint32_t err_cap) {
    return kvspaceIncr(k->h, key, out, err, err_cap);
}

int kvlangKvExpire(kvlangKv_t *k, const char *key, uint64_t ttl_ns, char *err, uint32_t err_cap) {
    return kvspaceExpire(k->h, key, ttl_ns, err, err_cap);
}

int kvlangKvWatchAny(kvlangKv_t *k, const char *const *keys, int n, uint64_t timeout_ns,
                     char **out_key, kvlangXvalue_t *out) {
    *out_key = NULL;
    kvlangXvalueZero(out);
    uint8_t *kb = NULL, *d = NULL; uint32_t kl = 0, len = 0;
    if (kvspaceWatchAny(k->h, keys, (uint32_t)n, timeout_ns, &kb, &kl, &d, &len) != 0) return -1;
    if (kb) {
        *out_key = malloc((size_t)kl + 1);
        memcpy(*out_key, kb, kl);
        (*out_key)[kl] = 0;
        kvspaceBytesFree(kb, kl);
    }
    kvlangXvalueCopyMalloc(out, d, len);
    kvspaceBytesFree(d, len);
    return 0;
}
