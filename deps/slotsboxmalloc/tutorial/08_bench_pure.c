#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <slotsboxmalloc/slotsboxobj.h>

#define OPS_PER_THREAD 50000
#define META_SIZE (1024 * 1024)
#define DATA_SIZE (8ULL * 64 * 64 * 64 * 64)
static const size_t kSizes[] = {8, 64, 512, 4096};

static uint8_t *meta, *data;
static _Atomic long total_ops = 0;

void* bench_thread(void *arg) {
    long ops = 0;
    for (int i = 0; i < OPS_PER_THREAD; i++) {
        size_t sz = kSizes[i & 3];
        uint64_t off = sbo_alloc(meta, sz);
        if (off == (uint64_t)-1) continue;
        sbo_free(meta, off);
        ops++;
    }
    total_ops += ops;
    return NULL;
}

int main(int argc, char **argv) {
    int n_threads = 8;
    if (argc > 1) n_threads = atoi(argv[1]);
    if (n_threads < 1) n_threads = 1;

    meta = malloc(META_SIZE); data = malloc(DATA_SIZE);
    memset(meta, 0, META_SIZE);
    if (sbo_init(meta, META_SIZE, DATA_SIZE) != 0) { printf("init failed\n"); return 1; }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    pthread_t *threads = malloc(n_threads * sizeof(pthread_t));
    for (int i = 0; i < n_threads; i++)
        pthread_create(&threads[i], NULL, bench_thread, NULL);
    for (int i = 0; i < n_threads; i++)
        pthread_join(threads[i], NULL);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;

    printf("threads=%d  ops=%ld  time=%.3fs  throughput=%.0f ops/s\n",
           n_threads, (long)total_ops, elapsed, total_ops / elapsed);

    if (n_threads == 1)
        printf("efficiency=1.00 (baseline)\n");

    free(threads); free(meta); free(data);
    return 0;
}
