#include "runtime_internal.h"

/* kv 访问统一走 kvspace-durable 兼容 C ABI（kvspace*）。
 * 后端由链接的 kvspace 库决定（kvspace-durable / kvspace-c 均导出同一 ABI）。 */

kvlangKv_t *kvlangKvConnect(const char *dsn) {
    kvlangKv_t *k = calloc(1, sizeof(*k));
    k->h = kvspaceConnect(dsn);
    if (!k->h) {
        free(k);
        return NULL;
    }
    return k;
}

void kvlangKvDisconnect(kvlangKv_t *k) {
    if (!k)
        return;
    if (k->h)
        kvspaceClose(k->h);
    free(k);
}

/* 借用读（resolve=0，raw）：out 直接借 kvspace 常驻/借用池指针（borrowed=1，不 free、不入
 * kvlangKvSet 前不跨写）。空值 → out len=0。 */
int kvlangKvGetOne(kvlangKv_t *k, const char *key, kvlangXvalue_t *out) {
    kvlangXvalueZero(out);
    uint8_t *d;
    uint32_t len;
    if (kvspaceGet(k->h, key, 0, &d, &len) != 0)
        return -1;
    if (d && len > 0) {
        out->data = d;
        out->len = len;
        out->borrowed = 1;
    }
    return 0;
}

/* Frame member: dir 直连 name 组键，借用读（resolve=1 穿透 link，全路径 Get(resolve=0) 不穿透 [d] 帧）
 * → out 借 kvspace 指针（borrowed=1）。空值 → out len=0。 */
int kvlangKvGetMember(kvlangKv_t *k, const char *dir, const char *name, kvlangXvalue_t *out) {
    kvlangXvalueZero(out);
    if (!name || !name[0])
        return 0;
    size_t dl = strlen(dir), nl = strlen(name);
    char *key = malloc(dl + nl + 1);
    memcpy(key, dir, dl);
    memcpy(key + dl, name, nl);
    key[dl + nl] = 0;
    uint8_t *d;
    uint32_t len;
    if (kvspaceGet(k->h, key, 1, &d, &len) == 0 && d && len > 0) {
        out->data = d;
        out->len = len;
        out->borrowed = 1;
    }
    free(key);
    return 0;
}

/* 指令边界回收读借用池：VM 每条指令末调一次。 */
void kvlangKvReadReset(kvlangKv_t *k) { kvspaceReadReset(k->h); }

/* 定位读：借用读 key 值的 [off, off+len) 字节 → out(borrowed=1)。空/越界 → out len=0。 */
int kvlangKvGetPart(kvlangKv_t *k, const char *key, uint32_t off, uint32_t len, kvlangXvalue_t *out) {
    kvlangXvalueZero(out);
    uint8_t *d;
    uint32_t got;
    if (kvspaceGetPart(k->h, key, off, len, &d, &got) != 0)
        return -1;
    if (d && got > 0) {
        out->data = d;
        out->len = got;
        out->borrowed = 1;
    }
    return 0;
}

/* 定位写：就地写 buf 到 key 值的 [off, off+buf_len)（key 须已存在、不改结构）。 */
int kvlangKvSetPart(kvlangKv_t *k, const char *key, uint32_t off, const uint8_t *buf, uint32_t buf_len,
                     char *err, uint32_t err_cap) {
    return kvspaceSetPart(k->h, key, off, buf, buf_len, err, err_cap);
}

/* 读 head：只读值前缀解码三正交轴 head（不取 body）。空/不存在 → 非 0。 */
int kvlangKvGetHead(kvlangKv_t *k, const char *key, kvspaceHead_t *out) {
    return kvspaceGetHead(k->h, key, out);
}

/* 写即构造：逐条解 head 取 (langtype, body)——同 body_len 就地(WriteInPlace)，否则新位置
 * (WriteNewPlace)——向 kvspace 要 body 偏移指针后直接写字节，无预合并缓冲。 */
int kvlangKvSet(kvlangKv_t *k, const kvlangKvPair_t *pairs, int n, char *err, uint32_t err_cap) {
    /* 借用值全程有效：durable 惰性写不再清读池，读借用池由 VM 在指令边界统一 ReadReset 回收，
     * 故写期直接用 v->data，不再需要防御性快照。 */
    int rc = 0;
    for (int i = 0; i < n; i++) {
        const kvlangXvalue_t *v = &pairs[i].val;
        if (!v->data || v->len == 0) { /* None → 删键，令该槽读回 None（不可静默跳过留旧值） */
            const char *dk[1] = {pairs[i].key};
            kvspaceDel(k->h, dk, 1, err, err_cap);
            continue;
        }
        kvspaceHead_t h;
        if (kvspaceDecodeHead(v->data, v->len, &h) != 0 || !h.langtype[0])
            continue;
        uint32_t body_len = h.body_len < 0 ? 0 : (uint32_t)h.body_len;
        const uint8_t *body = v->data + h.body_offset;
        uint8_t *dst = NULL;
        if (kvspaceWriteInPlace(k->h, pairs[i].key, 1, body_len, &dst, err, err_cap) != 0) {
            if (kvspaceWriteNewPlace(k->h, pairs[i].key, h.ref, h.storetype, h.ro, h.vid, (const char *)h.langtype, body_len, &dst, err, err_cap) != 0) {
                rc = -1;
                break;
            }
        }
        if (body_len > 0 && dst)
            memcpy(dst, body, body_len);
    }
    return rc;
}

int kvlangKvDel(kvlangKv_t *k, const char *key, char *err, uint32_t err_cap) {
    const char *keys[1] = {key};
    return kvspaceDel(k->h, keys, 1, err, err_cap);
}

int kvlangKvDelTree(kvlangKv_t *k, const char *prefix, char *err, uint32_t err_cap) {
    return kvspaceDelTree(k->h, prefix, err, err_cap);
}

int kvlangKvCp(kvlangKv_t *k, const char *src, const char *dst, char *err, uint32_t err_cap) {
    return kvspaceCp(k->h, src, dst, err, err_cap);
}

int kvlangKvCpTree(kvlangKv_t *k, const char *src, const char *dst, char *err, uint32_t err_cap) {
    return kvspaceCpTree(k->h, src, dst, err, err_cap);
}

int kvlangKvCpList(kvlangKv_t *k, const char *src, const char *dst, char *err, uint32_t err_cap) {
    return kvspaceCpList(k->h, src, dst, err, err_cap);
}

int kvlangKvMkindex(kvlangKv_t *k, const char *path, uint32_t capacity, char *err, uint32_t err_cap) {
    return kvspaceMkindex(k->h, path, capacity, err, err_cap);
}

int kvlangKvExtIndex(kvlangKv_t *k, const char *path, const char *ext, char *err, uint32_t err_cap) {
    return kvspaceMkindexExt(k->h, path, ext, err, err_cap);
}

int kvlangKvDelExtIndex(kvlangKv_t *k, const char *path, char *err, uint32_t err_cap) {
    return kvspaceRmindexExt(k->h, path, err, err_cap);
}

int kvlangKvList(kvlangKv_t *k, const char *prefix, bool expand_ext, bool resolve,
                 char ***out_names, int *out_count) {
    *out_names = NULL;
    *out_count = 0;
    int ex = expand_ext ? 1 : 0, rs = resolve ? 1 : 0;
    int32_t count = 0;
    if (kvspaceListLen(k->h, prefix, ex, rs, &count) != 0)
        return -1;
    if (count <= 0)
        return 0;
    char **names = malloc(sizeof(char *) * (size_t)count);
    for (int32_t i = 0; i < count; i++) {
        uint8_t buf[1024];
        uint32_t len = 0;
        if (kvspaceListAt(k->h, prefix, ex, rs, i, buf, sizeof buf, &len) == 0)
            names[i] = strndup((const char *)buf, len);
        else
            names[i] = strdup("");
    }
    *out_names = names;
    *out_count = (int)count;
    return 0;
}

int kvlangKvWatch(kvlangKv_t *k, const char *key, const kvlangXvalue_t *target, uint64_t tick_ns, kvlangXvalue_t *out) {
    kvlangXvalueZero(out);
    const uint8_t *t = target->data ? target->data : (const uint8_t *)"";
    uint32_t tl = target->len;
    uint8_t *d;
    uint32_t len;
    if (kvspaceWatch(k->h, key, t, tl, tick_ns, &d, &len) != 0)
        return -1;
    if (d && len) {
        out->data = d;
        out->len = len;
        out->borrowed = 1;
        kvlangXvalueMaterialize(out);
    }
    return 0;
}
