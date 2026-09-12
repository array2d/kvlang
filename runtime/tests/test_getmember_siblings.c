#include "runtime_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;

#define CHECK(c)                                                               \
    do {                                                                       \
        if (!(c)) {                                                            \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);       \
        }                                                                      \
    } while (0)

static int64_t xv_i64(const kvlangXvalue_t *v) {
    kvspaceHead_t h;
    const uint8_t *b;
    int64_t n = 0;
    int i;
    if (!v || !v->data || v->len == 0)
        return -999;
    if (kvlangXvalueHead(v, &h) != 0 || h.body_len < 8)
        return -999;
    b = v->data + h.body_offset;
    for (i = 0; i < 8; i++)
        n |= (int64_t)b[i] << (8 * i);
    return n;
}

static int set_i64(kvlangKv_t *k, const char *key, int64_t n) {
    kvlangXvalue_t v;
    kvlangKvPair_t p;
    char err[128];
    kvlangXvalueNewInt64(&v, n);
    p.key = (char *)key;
    p.val = v;
    err[0] = 0;
    if (kvlangKvSet(k, &p, 1, err, sizeof err) != 0) {
        kvlangXvalueFree(&v);
        return -1;
    }
    kvlangXvalueFree(&v);
    return 0;
}

/* Drop leaf/hot so GetMember cannot use per-key block_id. Keep fpar. */
static void drop_leaf_hot(kvlangKv_t *k) {
    int i;
    for (i = 0; i < k->nref; i++)
        free(k->ref[i].key);
    k->nref = 0;
    for (i = 0; i < k->nhot; i++) {
        free(k->hot[i].name);
        free(k->hot[i].key);
        k->hot[i].name = k->hot[i].key = NULL;
    }
    k->nhot = 0;
}

int main(void) {
    char dir[] = "/tmp/kvs-gm-XXXXXX";
    char path[256], dsn[288];
    kvlangKv_t *k;
    const char *frm = "/vthread/vt0/[0]/";
    static const char *names[] = {"a", "i", "n", "x", "y", "lo", "hi"};
    static const int64_t want[] = {11, 22, 33, 44, 55, 66, 77};
    const int n = 7;
    int i;
    kvlangXvalue_t out;

    if (!mkdtemp(dir))
        return 1;
    snprintf(path, sizeof path, "%s/s", dir);
    snprintf(dsn, sizeof dsn, "shm://%s", path);

    k = kvlangKvConnect(dsn);
    if (!k) {
        fprintf(stderr, "kvlangKvConnect failed dsn=%s\n", dsn);
        return 1;
    }
    CHECK(k->ref_on);

    for (i = 0; i < n; i++) {
        char key[128];
        snprintf(key, sizeof key, "%s%s", frm, names[i]);
        CHECK(set_i64(k, key, want[i]) == 0);
    }
    CHECK(k->fpar.key != NULL);
    CHECK(k->fpar.klen == (uint32_t)strlen(frm));
    CHECK(memcmp(k->fpar.key, frm, k->fpar.klen) == 0);
    CHECK(k->fpar.block_id != 0);
    CHECK(k->fpar.gen != 0);

    drop_leaf_hot(k);
    CHECK(k->nref == 0);
    CHECK(k->nhot == 0);
    CHECK(k->fpar.block_id != 0);

    for (i = 0; i < n; i++) {
        int64_t got;
        CHECK(kvlangKvGetMember(k, frm, names[i], &out) == 0);
        got = xv_i64(&out);
        CHECK(got == want[i]);
        CHECK(got != want[(i + 1) % n]);
        kvlangKvReadReset(k);
    }

    printf("GetMember siblings via cached ART parent: %d keys, distinct values\n", n);
    printf("fpar block_id=%u depth=%u\n", k->fpar.block_id, k->fpar.gen);

    /* Set a 2-char sibling with leaf/hot empty so the write uses fpar. */
    {
        char lokey[128];
        snprintf(lokey, sizeof lokey, "%slo", frm);
        CHECK(set_i64(k, lokey, 88) == 0);
        CHECK(kvlangKvGetMember(k, frm, "lo", &out) == 0);
        CHECK(xv_i64(&out) == 88);
        kvlangKvReadReset(k);
        CHECK(kvlangKvGetMember(k, frm, "hi", &out) == 0);
        CHECK(xv_i64(&out) == 77);
        kvlangKvReadReset(k);
        printf("Set sibling via cached ART parent: lo=88 hi=77\n");
    }

    /* New frame dir must replace sticky fpar so siblings resolve from that parent. */
    {
        const char *frm2 = "/vthread/vt0/[1]/";
        static const char *n2[] = {"x", "y"};
        static const int64_t w2[] = {101, 202};
        char key[128];
        int j;
        for (j = 0; j < 2; j++) {
            snprintf(key, sizeof key, "%s%s", frm2, n2[j]);
            CHECK(set_i64(k, key, w2[j]) == 0);
        }
        CHECK(k->fpar.key != NULL);
        CHECK(k->fpar.klen == (uint32_t)strlen(frm2));
        CHECK(memcmp(k->fpar.key, frm2, k->fpar.klen) == 0);
        drop_leaf_hot(k);
        for (j = 0; j < 2; j++) {
            CHECK(kvlangKvGetMember(k, frm2, n2[j], &out) == 0);
            CHECK(xv_i64(&out) == w2[j]);
            kvlangKvReadReset(k);
        }
        printf("fpar refreshed for frame [1]: x=101 y=202\n");
    }

    kvlangKvDisconnect(k);
    if (failures) {
        fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    return 0;
}
