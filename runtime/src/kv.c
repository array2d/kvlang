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

/* 借用读（resolve=0，raw）→ 拷贝为 runtime 自持。空值 → out len=0。 */
int kvlangKvGetOne(kvlangKv_t *k, const char *key, kvlangXvalue_t *out) {
    kvlangXvalueZero(out);
    uint8_t *d; uint32_t len;
    if (kvspaceGet(k->h, key, 0, &d, &len) != 0) return -1;
    kvlangXvalueCopyMalloc(out, d, len);
    return 0;
}

/* Frame member: prefix 直连 name 组键，穿透 link（resolve=1）——全路径 Get(resolve=0) 不穿透 [d] 帧。 */
int kvlangKvGetMember(kvlangKv_t *k, const char *dir, const char *name, kvlangXvalue_t *out) {
    kvlangXvalueZero(out);
    if (!name || !name[0]) return 0;
    char *nm = (char *)name;
    return kvlangKvGetBatch(k, dir, &nm, 1, out);
}

/* 逐名单值借用读（key=prefix+name，resolve=1 穿透 link）→ 拷贝自持。取代已删的 kvspaceGetBatch。 */
int kvlangKvGetBatch(kvlangKv_t *k, const char *prefix, char **names, int n, kvlangXvalue_t *out) {
    size_t pl = strlen(prefix);
    for (int i = 0; i < n; i++) {
        kvlangXvalueZero(&out[i]);
        size_t nl = strlen(names[i]);
        char *key = malloc(pl + nl + 1);
        memcpy(key, prefix, pl); memcpy(key + pl, names[i], nl); key[pl + nl] = 0;
        uint8_t *d; uint32_t len;
        if (kvspaceGet(k->h, key, 1, &d, &len) == 0 && d && len > 0)
            kvlangXvalueCopyMalloc(&out[i], d, len);
        free(key);
    }
    return 0;
}

/* 写即构造：逐条解 head 取 (kindexpr, body)——同 body_len 就地(WriteInPlace)，否则新位置
 * (WriteNewPlace)——向 kvspace 要 body 偏移指针后直接写字节，无预合并缓冲。 */
int kvlangKvSet(kvlangKv_t *k, const kvlangKvPair_t *pairs, int n, char *err, uint32_t err_cap) {
    for (int i = 0; i < n; i++) {
        const kvlangXvalue_t *v = &pairs[i].val;
        if (!v->data || v->len == 0) {  /* None → 删键，令该槽读回 None（不可静默跳过留旧值） */
            const char *dk[1] = { pairs[i].key };
            kvspaceDel(k->h, dk, 1, err, err_cap);
            continue;
        }
        kvspaceHead_t h;
        if (kvspaceDecodeHead(v->data, v->len, &h) != 0 || !h.kindexpr[0]) continue;
        uint32_t body_len = h.body_len < 0 ? 0 : (uint32_t)h.body_len;
        const uint8_t *body = v->data + h.body_offset;
        uint8_t *dst = NULL;
        if (kvspaceWriteInPlace(k->h, pairs[i].key, 1, body_len, &dst, err, err_cap) != 0) {
            if (kvspaceWriteNewPlace(k->h, pairs[i].key, (const char *)h.kindexpr, body_len, &dst, err, err_cap) != 0)
                return -1;
        }
        if (body_len > 0 && dst) memcpy(dst, body, body_len);
    }
    return 0;
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
    if (!d || len == 0) return 0;
    char *s = malloc((size_t)len + 1);
    memcpy(s, d, len); s[len] = 0;
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
    return 0;
}
