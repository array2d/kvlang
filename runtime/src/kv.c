#include "runtime_internal.h"

/* kv 访问统一走 kvspace-durable 兼容 C ABI（kvspace_*）。
 * 后端由链接的 kvspace 库决定（kvspace-durable / kvspace-c 均导出同一 ABI）。 */

static void xv_copy_malloc(xval_t *out, const uint8_t *d, uint32_t len) {
    if (len > 0) {
        out->data = malloc(len);
        memcpy(out->data, d, len);
        out->len = len;
    }
}

kv_t *kv_connect(const char *dsn) {
    kv_t *k = calloc(1, sizeof(*k));
    k->h = kvspace_conn(dsn);
    if (!k->h) { free(k); return NULL; }
    return k;
}

void kv_disconnect(kv_t *k) {
    if (!k) return;
    if (k->h) kvspace_free(k->h);
    free(k);
}

int kv_get_one(kv_t *k, const char *key, xval_t *out) {
    xv_zero(out);
    uint8_t *d; uint32_t len;
    if (kvspace_get_one(k->h, key, &d, &len) != 0) return -1;
    xv_copy_malloc(out, d, len);
    kvspace_bytes_free(d, len);
    return 0;
}

int kv_get_batch(kv_t *k, const char *prefix, char **names, int n, xval_t *out) {
    for (int i = 0; i < n; i++) xv_zero(&out[i]);
    const char **ns = malloc(sizeof(char *) * (size_t)n);
    for (int i = 0; i < n; i++) ns[i] = names[i];
    uint8_t *d; uint32_t len;
    int rc = kvspace_get_batch(k->h, prefix, ns, (uint32_t)n, &d, &len);
    free(ns);
    if (rc != 0) return rc;
    uint32_t off = 0;
    for (int i = 0; i < n; i++) {
        if (off + 4 > len) break;
        uint32_t vl = (uint32_t)d[off] | ((uint32_t)d[off + 1] << 8) | ((uint32_t)d[off + 2] << 16) | ((uint32_t)d[off + 3] << 24);
        off += 4;
        if (vl > 0 && off + vl <= len) xv_copy_malloc(&out[i], d + off, vl);
        off += vl;
    }
    kvspace_bytes_free(d, len);
    return 0;
}

int kv_set(kv_t *k, const kv_pair_t *pairs, int n, char *err, uint32_t err_cap) {
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
    int rc = kvspace_set(k->h, keys, vals, lens, (uint32_t)n, err, err_cap);
    free(keys); free(lens); free(vals);
    return rc;
}

int kv_del(kv_t *k, const char *key, char *err, uint32_t err_cap) {
    const char *keys[1] = { key };
    return kvspace_del(k->h, keys, 1, err, err_cap);
}

int kv_del_tree(kv_t *k, const char *prefix, char *err, uint32_t err_cap) {
    return kvspace_del_tree(k->h, prefix, err, err_cap);
}

int kv_mkindex(kv_t *k, const char *path, char *err, uint32_t err_cap) {
    return kvspace_mkindex(k->h, path, err, err_cap);
}

int kv_ext_index(kv_t *k, const char *path, const char *ext, char *err, uint32_t err_cap) {
    return kvspace_ext_index(k->h, path, ext, err, err_cap);
}

int kv_del_ext_index(kv_t *k, const char *path, char *err, uint32_t err_cap) {
    return kvspace_del_ext_index(k->h, path, err, err_cap);
}

int kv_list(kv_t *k, const char *prefix, bool expand_ext, bool resolve,
            char ***out_names, int *out_count) {
    *out_names = NULL; *out_count = 0;
    uint8_t *d; uint32_t len;
    if (kvspace_list(k->h, prefix, expand_ext ? 1 : 0, resolve ? 1 : 0, &d, &len) != 0) return -1;
    if (len == 0) { if (d) kvspace_bytes_free(d, len); return 0; }
    char *s = malloc((size_t)len + 1);
    memcpy(s, d, len); s[len] = 0;
    kvspace_bytes_free(d, len);
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

int kv_watch(kv_t *k, const char *key, const xval_t *target, uint64_t tick_ns, xval_t *out) {
    xv_zero(out);
    const uint8_t *t = target->data ? target->data : (const uint8_t *)"";
    uint32_t tl = target->len;
    uint8_t *d; uint32_t len;
    if (kvspace_watch(k->h, key, t, tl, tick_ns, &d, &len) != 0) return -1;
    xv_copy_malloc(out, d, len);
    kvspace_bytes_free(d, len);
    return 0;
}
