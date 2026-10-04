#include "runtime_internal.h"

kvlangKv_t *kvlangKvConnect(const char *dsn) {
    kvlangKv_t *k = calloc(1, sizeof(*k));
    if (!k)
        return NULL;
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

/* Borrowed read (resolve=0). Empty -> out len=0. */
int kvlangKvGetOne(kvlangKv_t *k, const char *key, kvlangXvalue_t *out) {
    kvlangXvalueZero(out);
    uint8_t *d = NULL;
    uint32_t len = 0;
    if (kvspaceGet(k->h, key, 0, &d, &len) != 0)
        return -1;
    if (d && len > 0) {
        out->data = d;
        out->len = len;
        out->borrowed = 1;
    }
    return 0;
}

int kvlangKvGetMember(kvlangKv_t *k, const char *dir, const char *name,
                      kvlangXvalue_t *out) {
    kvlangXvalueZero(out);
    if (!dir || !name || !name[0])
        return 0;
    size_t dl = strlen(dir), nl = strlen(name);
    if (dl > SIZE_MAX - nl - 1)
        return -1;
    char stackbuf[512];
    char *key = dl + nl + 1 <= sizeof stackbuf ? stackbuf : malloc(dl + nl + 1);
    if (!key)
        return -1;
    memcpy(key, dir, dl);
    memcpy(key + dl, name, nl + 1);
    int rc = kvlangKvGetOne(k, key, out);
    if (key != stackbuf)
        free(key);
    return rc;
}

/* Recycle the read-borrow pool at instruction boundaries. */
void kvlangKvReadReset(kvlangKv_t *k) { kvspaceReadReset(k->h); }

/* Borrowed slice of the value body. Empty/OOB -> out len=0. */
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

/* In-place write into an existing value. Key must already exist. */
int kvlangKvSetPart(kvlangKv_t *k, const char *key, uint32_t off, const uint8_t *buf, uint32_t buf_len,
                     char *err, uint32_t err_cap) {
    return kvspaceSetPart(k->h, key, off, buf, buf_len, err, err_cap);
}

/* Read head only (no body). Missing/empty -> non-zero. */
int kvlangKvGetHead(kvlangKv_t *k, const char *key, kvspaceHead_t *out) {
    return kvspaceGetHead(k->h, key, out);
}

int kvlangKvSet(kvlangKv_t *k, const kvlangKvPair_t *pairs, int n, char *err, uint32_t err_cap) {
    for (int i = 0; i < n; i++) {
        const kvlangXvalue_t *v = &pairs[i].val;
        if (kvlangXvalueNone(v)) {
            const char *dk[1] = {pairs[i].key};
            if (kvspaceDel(k->h, dk, 1, err, err_cap) != 0)
                return -1;
            continue;
        }
        if (kvspaceSetValue(k->h, pairs[i].key, v->data, v->len, 0, 0,
                            err, err_cap) != 0)
            return -1;
    }
    return 0;
}

int kvlangKvSetChar(kvlangKv_t *k, const char *key, const char *s) {
    kvlangXvalue_t value;
    kvlangXvalueNewCharUtf8(&value, s);
    if (!value.data || value.len == 0)
        return -1;
    kvlangKvPair_t pair = {(char *)key, value};
    char err[256];
    int rc = kvlangKvSet(k, &pair, 1, err, sizeof err);
    kvlangXvalueFree(&value);
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
