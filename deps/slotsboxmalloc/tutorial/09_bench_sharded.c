#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <slotsboxmalloc/slotsboxobj.h>

#define OPS_PER_THREAD 50000
#define META_SIZE (256 * 1024)
#define DATA_SIZE (8ULL * 1024 * 1024)  
static const size_t kSizes[] = {8, 64, 512, 4096};

static _Atomic long total_ops = 0;

typedef struct { uint8_t *meta, *data; } instance_t;

void* bench_thread(void *arg) {
    instance_t *inst = (instance_t *)arg;
    long ops = 0;
    for (int i = 0; i < OPS_PER_THREAD; i++) {
        size_t sz = kSizes[i & 3];
        uint64_t off = sbo_alloc(inst->meta, sz);
        if (off == (uint64_t)-1) continue;
        sbo_free(inst->meta, off);
        ops++;
    }
    total_ops += ops;
    return NULL;
}

int main(int argc, char **argv) {
    int n_threads = 8;
    if (argc > 1) n_threads = atoi(argv[1]);
    if (n_threads < 1) n_threads = 1;

    instance_t *instances = malloc(n_threads * sizeof(instance_t));
    for (int i = 0; i < n_threads; i++) {
        instances[i].meta = malloc(META_SIZE);
        instances[i].data = malloc(DATA_SIZE);
        memset(instances[i].meta, 0, META_SIZE);
        sbo_init(instances[i].meta, META_SIZE, DATA_SIZE);
    }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    pthread_t *threads = malloc(n_threads * sizeof(pthread_t));
    for (int i = 0; i < n_threads; i++)
        pthread_create(&threads[i], NULL, bench_thread, &instances[i]);
    for (int i = 0; i < n_threads; i++)
        pthread_join(threads[i], NULL);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;

    double ideal = 1e9 * total_ops / elapsed / n_threads;
    printf("threads=%d  ops=%ld  time=%.3fs  total_throughput=%.0f ops/s  per_thread=%.0f ops/s\n",
           n_threads, (long)total_ops, elapsed, total_ops / elapsed, ideal);

    for (int i = 0; i < n_threads; i++) { free(instances[i].meta); free(instances[i].data); }
    free(instances); free(threads);
    return 0;
}
