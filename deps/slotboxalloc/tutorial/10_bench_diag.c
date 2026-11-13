#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <slotboxalloc/slotboxobj.h>

#define OPS 200000
#define META_SIZE (1024 * 1024)
#define DATA_SIZE (8ULL * 64 * 64 * 64 * 64)
static const size_t k4[] = {8, 64, 512, 4096};

typedef struct { uint8_t *meta; int sz_idx; } arg_t;
static _Atomic long total = 0;

void* bench_1size(void *a) {
    arg_t *aa = (arg_t*)a;
    long n = 0;
    size_t sz = k4[aa->sz_idx];
    for (int i = 0; i < OPS; i++) {
        uint64_t off = sbo_alloc(aa->meta, sz);
        if (off != (uint64_t)-1) { sbo_free(aa->meta, off); n++; }
    }
    total += n; return NULL;
}

void* bench_4size(void *a) {
    uint8_t *meta = (uint8_t*)a;
    long n = 0;
    for (int i = 0; i < OPS; i++) {
        size_t sz = k4[i & 3];
        uint64_t off = sbo_alloc(meta, sz);
        if (off != (uint64_t)-1) { sbo_free(meta, off); n++; }
    }
    total += n; return NULL;
}

static void run(const char *label, void* (*fn)(void*), void *arg, int nthr) {
    total = 0;
    struct timespec t0, t1;
    pthread_t *tids = malloc(nthr * sizeof(pthread_t));
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < nthr; i++) pthread_create(&tids[i], NULL, fn, arg);
    for (int i = 0; i < nthr; i++) pthread_join(tids[i], NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double e = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    printf("%-24s thr=%d  ops=%ld  time=%.3fs  rate=%.0f ops/s\n", label, nthr, (long)total, e, total/e);
    free(tids);
}

int main() {
    uint8_t *mem = malloc(META_SIZE + DATA_SIZE);
    for (int si = 0; si <= 3; si++) {
        memset(mem, 0, META_SIZE);
        sbo_init(mem, META_SIZE, DATA_SIZE);
        arg_t a = {mem, si};
        char buf[32]; snprintf(buf, sizeof(buf), "1size(%zuB)", k4[si]);
        for (int n = 1; n <= 8; n *= 2)
            run(buf, bench_1size, &a, n);
    }
    
    memset(mem, 0, META_SIZE);
    sbo_init(mem, META_SIZE, DATA_SIZE);
    for (int n = 1; n <= 8; n *= 2)
        run("4size(8-4096B)", bench_4size, mem, n);
    free(mem);
    return 0;
}
