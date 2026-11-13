#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <slotboxalloc/slotboxobj.h>

#define OPS 200000
#define META_SIZE (16 * 1024 * 1024)
#define DATA_SIZE (8ULL * 64 * 64 * 64 * 64)
static const size_t kSizes[] = {8, 64, 512, 4096};

static _Atomic long total = 0;

void* alloc_only(void *a) {
    uint8_t *meta = (uint8_t *)a;
    long n = 0;
    for (int i = 0; i < OPS; i++) {
        uint64_t off = sbo_alloc(meta, kSizes[i & 3]);
        if (off != (uint64_t)-1) n++;
        else break;
    }
    total += n;
    return NULL;
}

void* alloc_free(void *a) {
    uint8_t *meta = (uint8_t *)a;
    long n = 0;
    for (int i = 0; i < OPS; i++) {
        uint64_t off = sbo_alloc(meta, kSizes[i & 3]);
        if (off != (uint64_t)-1) { sbo_free(meta, off); n++; }
    }
    total += n;
    return NULL;
}

void run(const char *label, void*(*fn)(void*), uint8_t *mem, int nthr) {
    memset(mem, 0, META_SIZE);
    sbo_init(mem, META_SIZE, DATA_SIZE);
    total = 0;
    struct timespec t0, t1;
    pthread_t *tids = malloc(nthr * sizeof(pthread_t));
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < nthr; i++) pthread_create(&tids[i], NULL, fn, mem);
    for (int i = 0; i < nthr; i++) pthread_join(tids[i], NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double e = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    printf("%-18s thr=%d  ops=%ld  time=%.3fs  rate=%.0f ops/s\n",
           label, nthr, (long)total, e, total / e);
    free(tids);
}

int main() {
    uint8_t *mem = malloc(META_SIZE + DATA_SIZE);
    for (int n = 1; n <= 8; n++) run("alloc-only", alloc_only, mem, n);
    for (int n = 1; n <= 8; n++) run("alloc+free", alloc_free, mem, n);
    free(mem);
    return 0;
}
