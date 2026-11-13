#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <slotboxalloc/slotboxobj.h>

#define OPS_PER_THREAD 20000
#define META_SIZE (1024 * 1024)
#define DATA_SIZE (8ULL * 64 * 64 * 64 * 64)  
static const size_t kSizes[] = {8, 16, 32, 64, 128, 256, 512, 1024};

static uint8_t *meta, *data;
static _Atomic long total_ops = 0;

void* bench_thread(void *arg) {
    uint64_t live[8];
    int live_count = 0;
    long ops = 0;
    for (int i = 0; i < OPS_PER_THREAD; i++) {
        if (live_count >= 4 && (i & 1)) {
            int idx = (int)(((uint64_t)i * 1103515245 + 12345) % (uint64_t)live_count);
            sbo_free(meta, live[idx]);
            live[idx] = live[--live_count];
        } else {
            size_t sz = kSizes[i & 7];
            uint64_t off = sbo_alloc(meta, sz);
            if (off == (uint64_t)-1) continue;
            *(uint64_t*)(data + off) = (uint64_t)i;
            if (live_count < 8) live[live_count++] = off;
        }
        ops++;
    }
    for (int i = 0; i < live_count; i++) sbo_free(meta, live[i]);
    total_ops += ops;
    return NULL;
}

int main(int argc, char **argv) {
    int n_threads = 8;
    if (argc > 1) n_threads = atoi(argv[1]);
    if (n_threads < 1) n_threads = 1;

    meta = malloc(META_SIZE); data = malloc(DATA_SIZE);
    memset(meta, 0, META_SIZE); memset(data, 0, DATA_SIZE);
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
    free(threads); free(meta); free(data);
    return 0;
}
