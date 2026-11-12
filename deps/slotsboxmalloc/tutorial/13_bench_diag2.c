#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <slotsboxmalloc/slotsboxobj.h>

#define OPS 50000
#define META_SIZE (256 * 1024)
#define DATA_SIZE (15 * 8 * 16)

static _Atomic long total_ops = 0, total_tries = 0, total_spins = 0;

void* worker(void *a) {
    uint8_t *meta = (uint8_t *)a;
    long ops = 0, tries = 0;
    for (int i = 0; i < OPS; i++) {
        uint64_t off = sbo_alloc(meta, 8);
        if (off != (uint64_t)-1) {
            sbo_free(meta, off);
            ops++;
        }
        tries++;
        
    }
    total_ops += ops;
    total_tries += tries;
    return NULL;
}

int main(int argc, char **argv) {
    const int n = (argc > 1) ? atoi(argv[1]) : 8;
    uint8_t *mem = malloc(META_SIZE + DATA_SIZE);
    memset(mem, 0, META_SIZE);
    sbo_init(mem, META_SIZE, DATA_SIZE);
    sbo_meta_t *meta = (sbo_meta_t *)mem;

    struct timespec t0, t1;
    pthread_t *tids = malloc(n * sizeof(pthread_t));
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < n; i++) pthread_create(&tids[i], NULL, worker, mem);
    for (int i = 0; i < n; i++) pthread_join(tids[i], NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double e = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;

    printf("thr=%d  ops=%ld  time=%.3fs  rate=%.0f ops/s\n",
           n, (long)total_ops, e, total_ops / e);
    printf("alloc_seq=%lu (slot distribution)\n",
           (unsigned long)atomic_load(&meta->alloc_seq));
    free(tids); free(mem);
    return 0;
}
