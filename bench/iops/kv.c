/* #204 floor: shm Get+decode / +1 / NewInt64+Set. Same loop as kvlang a=a+1.
 * v0.2.18 无 kvspaceSet / kvspaceBytesFree：Get 是借用（不得 free）；Set 等价于
 * DecodeHead 后 WriteInPlace，失败再 WriteNewPlace，把 NewInt64 的 body 拷进去。 */
#include "kvspace/kvspace.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int64_t rd64(const uint8_t *p) {
    uint64_t u = (uint64_t)p[0] | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16) |
                 ((uint64_t)p[3] << 24) | ((uint64_t)p[4] << 32) |
                 ((uint64_t)p[5] << 40) | ((uint64_t)p[6] << 48) |
                 ((uint64_t)p[7] << 56);
    return (int64_t)u;
}

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* NewInt64 产出的完整 XValue → 后端。codec 缓冲由调用方 free。 */
static int set_xvalue(void *h, const char *key, uint8_t *xvalue, uint32_t tlen,
                   char *err, uint32_t err_cap) {
    kvspaceHead_t hd;
    uint8_t *dst = NULL;
    uint32_t body_len;
    if (kvspaceDecodeHead(xvalue, tlen, &hd) != 0)
        return 1;
    body_len = hd.body_len < 0 ? 0 : (uint32_t)hd.body_len;
    if (kvspaceWriteInPlace(h, key, 0, body_len, &dst, err, err_cap) != 0) {
        if (kvspaceWriteNewPlace(h, key, hd.ref, hd.storetype, hd.ro, hd.vid,
                                 hd.langtype, body_len, (uint64_t)body_len, &dst,
                                 err, err_cap) != 0)
            return 1;
    }
    if (body_len && dst)
        memcpy(dst, xvalue + hd.body_offset, body_len);
    return 0;
}

int main(void) {
    const char *dsn = getenv("KVSPACE");
    if (!dsn || !dsn[0])
        dsn = "shm:///tmp/kvlang_iops.shm";
    int64_t n = 1000000;
    const char *ns = getenv("IOPS_N");
    if (ns && ns[0])
        n = strtoll(ns, NULL, 10);

    void *h = kvspaceConnect(dsn);
    if (!h) {
        fprintf(stderr, "kvspaceConnect failed: %s\n", dsn);
        return 1;
    }

    uint8_t *xvalue = NULL;
    uint32_t tlen = 0;
    if (kvspaceNewInt64(0, &xvalue, &tlen) != 0 || !xvalue) {
        fprintf(stderr, "NewInt64 0 failed\n");
        return 1;
    }
    const char *key = "/a";
    char err[128] = {0};
    if (set_xvalue(h, key, xvalue, tlen, err, sizeof err) != 0) {
        fprintf(stderr, "seed set: %s\n", err);
        return 1;
    }
    free(xvalue);

    uint64_t t0 = now_ns();
    int64_t a = 0;
    for (;;) {
        uint8_t *d = NULL;
        uint32_t len = 0;
        if (kvspaceGet(h, key, 0, &d, &len) != 0 || !d) {
            fprintf(stderr, "get failed\n");
            return 1;
        }
        kvspaceHead_t hd;
        memset(&hd, 0, sizeof hd);
        if (kvspaceDecodeHead(d, len, &hd) != 0 || hd.body_len < 8 ||
            hd.body_offset + 8 > (int32_t)len) {
            fprintf(stderr, "decode failed len=%u\n", len);
            return 1;
        }
        a = rd64(d + hd.body_offset);
        if (a >= n)
            break;
        a += 1;
        xvalue = NULL;
        tlen = 0;
        if (kvspaceNewInt64(a, &xvalue, &tlen) != 0) {
            fprintf(stderr, "NewInt64 failed\n");
            return 1;
        }
        if (set_xvalue(h, key, xvalue, tlen, err, sizeof err) != 0) {
            fprintf(stderr, "set: %s\n", err);
            free(xvalue);
            return 1;
        }
        free(xvalue);
    }
    uint64_t elapsed = now_ns() - t0;
    kvspaceClose(h);
    if (a != n) {
        fprintf(stderr, "kv: a=%" PRId64 " want %" PRId64 "\n", a, n);
        return 1;
    }
    printf("kvspace-c n=%" PRId64 " ns=%" PRIu64 " ns/iter=%.3f a=%" PRId64 "\n",
           n, elapsed, (double)elapsed / (double)n, a);
    return 0;
}
