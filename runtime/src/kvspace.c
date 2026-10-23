#include "runtime_internal.h"

kvspace_t *kvlangKvspaceConnect(const char *dsn) {
    kvspace_t *k = calloc(1, sizeof(*k));
    if (!k)
        return NULL;
    k->handle = kvspaceConnect(dsn);
    if (!k->handle) {
        free(k);
        return NULL;
    }
    k->shm = strncmp(dsn, "shm://", 6) == 0;
    return k;
}

void kvlangKvspaceDisconnect(kvspace_t *k) {
    if (!k)
        return;
    if (k->isa_handle && !k->isa_shared)
        kvspaceClose(k->isa_handle);
    if (k->handle)
        kvspaceClose(k->handle);
    free(k);
}

/* Borrowed read (resolve=0). Empty -> out len=0. */
int kvlangKvspaceGetOne(kvspace_t *k, const char *key, kvlangXvalue_t *out) {
    kvlangXvalueZero(out);
    uint8_t *d = NULL;
    uint32_t len = 0;
    if (kvspaceGet(k->handle, key, 0, &d, &len) != 0)
        return -1;
    if (d && len > 0) {
        out->bytes = d;
        out->length = len;
    }
    return 0;
}

int kvlangKvspaceGetMember(kvspace_t *k, const char *dir, const char *name,
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
    int rc = kvlangKvspaceGetOne(k, key, out);
    if (key != stackbuf)
        free(key);
    return rc;
}

/* Borrowed slice of the value body. Empty/OOB -> out len=0. */
int kvlangKvspaceGetPart(kvspace_t *k, const char *key, uint32_t off, uint32_t len, kvlangXvalue_t *out) {
    kvlangXvalueZero(out);
    uint8_t *d;
    uint32_t got;
    if (kvspaceGetPart(k->handle, key, off, len, &d, &got) != 0)
        return -1;
    if (d && got > 0) {
        out->bytes = d;
        out->length = got;
    }
    return 0;
}

/* In-place write into an existing value. Key must already exist. */
int kvlangKvspaceSetPart(kvspace_t *k, const char *key, uint32_t off, const uint8_t *buf, uint32_t buf_len,
                     char *err, uint32_t err_cap) {
    return kvspaceSetPart(k->handle, key, off, buf, buf_len, err, err_cap);
}

/* Read head only (no body). Missing/empty -> non-zero. */
int kvlangKvspaceGetHead(kvspace_t *k, const char *key, kvspaceHead_t *out) {
    return kvspaceGetHead(k->handle, key, out);
}

int kvlangKvspaceSet(kvspace_t *k, const kvlangKvPair_t *pairs, int n, char *err, uint32_t err_cap) {
    for (int i = 0; i < n; i++) {
        const kvlangXvalue_t *v = &pairs[i].value;
        if (v->bytes && v->length >= XH_PREFIX)
            kvlangBodyInvalidateKind(pairs[i].key, xh_langtype(v->bytes),
                                     xh_langtype_len(v->bytes));
        else
            kvlangBodyInvalidate(pairs[i].key);
        if (kvlangXvalueNone(v)) {
            const char *dk[1] = {pairs[i].key};
            if (kvspaceDel(k->handle, dk, 1, err, err_cap) != 0)
                return -1;
            continue;
        }
        if (kvspaceSetValue(k->handle, pairs[i].key, v->bytes, v->length, 0, 0,
                            err, err_cap) != 0)
            return -1;
    }
    return 0;
}

int kvlangKvspaceSetChar(kvspace_t *k, const char *key, const char *s) {
    kvlangXvalue_t value;
    kvlangXvalueNewCharUtf8(&value, s);
    if (!value.bytes || value.length == 0)
        return -1;
    kvlangKvPair_t pair = {(char *)key, value};
    char err[256];
    int rc = kvlangKvspaceSet(k, &pair, 1, err, sizeof err);
    kvlangXvalueFree(&value);
    return rc;
}

int kvlangKvspaceDelete(kvspace_t *k, const char *key, char *err, uint32_t err_cap) {
    kvlangBodyInvalidate(key);
    const char *keys[1] = {key};
    return kvspaceDel(k->handle, keys, 1, err, err_cap);
}

int kvlangKvspaceDeleteTree(kvspace_t *k, const char *prefix, char *err, uint32_t err_cap) {
    kvlangBodyInvalidateTree(prefix);
    return kvspaceDelTree(k->handle, prefix, err, err_cap);
}

int kvlangKvspaceCopy(kvspace_t *k, const char *src, const char *dst, char *err, uint32_t err_cap) {
    kvlangBodyInvalidateTree(dst);
    return kvspaceCp(k->handle, src, dst, err, err_cap);
}

int kvlangKvspaceCopyTree(kvspace_t *k, const char *src, const char *dst, char *err, uint32_t err_cap) {
    kvlangBodyInvalidateTree(dst);
    return kvspaceCpTree(k->handle, src, dst, err, err_cap);
}

int kvlangKvspaceCopyList(kvspace_t *k, const char *src, const char *dst, char *err, uint32_t err_cap) {
    kvlangBodyInvalidateTree(dst);
    return kvspaceCpList(k->handle, src, dst, err, err_cap);
}

int kvlangKvspaceMakeIndex(kvspace_t *k, const char *path, uint32_t capacity, char *err, uint32_t err_cap) {
    return kvspaceMkindex(k->handle, path, capacity, err, err_cap);
}

int kvlangKvspaceExtensionIndex(kvspace_t *k, const char *path, const char *ext, char *err, uint32_t err_cap) {
    return kvspaceMkindexExt(k->handle, path, ext, err, err_cap);
}

int kvlangKvspaceDeleteExtensionIndex(kvspace_t *k, const char *path, char *err, uint32_t err_cap) {
    return kvspaceRmindexExt(k->handle, path, err, err_cap);
}

int kvlangKvspaceListLen(kvspace_t *k, const char *prefix, bool expand_ext,
                         bool resolve, int *out_count) {
    int32_t count = 0;
    if (kvspaceListLen(k->handle, prefix, expand_ext ? 1 : 0,
                       resolve ? 1 : 0, &count) != 0)
        return -1;
    *out_count = (int)count;
    return 0;
}

int kvlangKvspaceListAt(kvspace_t *k, const char *prefix, bool expand_ext,
                        bool resolve, int idx, char **out_name) {
    *out_name = NULL;
    if (idx < 0)
        return -1;
    uint8_t buf[1024];
    uint32_t len = 0;
    if (kvspaceListAt(k->handle, prefix, expand_ext ? 1 : 0,
                      resolve ? 1 : 0, idx, buf, sizeof buf, &len) != 0)
        return -1;
    *out_name = strndup((const char *)buf, len);
    return 0;
}

int kvlangKvspaceList(kvspace_t *k, const char *prefix, bool expand_ext,
                      bool resolve, char ***out_names, int *out_count) {
    *out_names = NULL;
    *out_count = 0;
    int count = 0;
    if (kvlangKvspaceListLen(k, prefix, expand_ext, resolve, &count) != 0)
        return -1;
    if (count <= 0)
        return 0;
    char **names = malloc(sizeof(char *) * (size_t)count);
    for (int i = 0; i < count; i++) {
        if (kvlangKvspaceListAt(k, prefix, expand_ext, resolve, i, &names[i]) != 0)
            names[i] = strdup("");
    }
    *out_names = names;
    *out_count = count;
    return 0;
}

int kvlangKvspaceWatch(kvspace_t *k, const char *key, const kvlangXvalue_t *target, uint64_t tick_ns, kvlangXvalue_t *out) {
    kvlangXvalueZero(out);
    const uint8_t *t = target->bytes ? target->bytes : (const uint8_t *)"";
    uint32_t tl = target->length;
    uint8_t *d;
    uint32_t len;
    if (kvspaceWatch(k->handle, key, t, tl, tick_ns, &d, &len) != 0)
        return -1;
    if (d && len) {
        out->bytes = d; /* 借用视图；调用方在同一指令内使用，不 free */
        out->length = len;
    }
    return 0;
}
