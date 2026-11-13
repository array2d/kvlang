#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>

#define NLOCKS 16
#define OPS 500000
static atomic_int locks[NLOCKS];
static _Atomic long total = 0;

void* bench_trylock(void *arg) {
    long n = 0;
    for (int i = 0; i < OPS; i++) {
        int si = (i * 1103515245ULL) % NLOCKS;
        if (atomic_exchange(&locks[si], 1) == 0) {
            atomic_store(&locks[si], 0);
            n++;
        }
    }
    total += n; return NULL;
}

void* bench_lock(void *arg) {
    int mylock = (int)(uintptr_t)arg % NLOCKS;
    long n = 0;
    for (int i = 0; i < OPS; i++) {
        int expected = 0;
        while (!atomic_compare_exchange_weak(&locks[mylock], &expected, 1))
            expected = 0;
        
        for (volatile int j = 0; j < 20; j++) ;
        atomic_store(&locks[mylock], 0);
        n++;
    }
    total += n; return NULL;
}

void run(const char *label, void*(*fn)(void*), int nthr) {
    memset(locks, 0, sizeof(locks));
    total = 0;
    struct timespec t0, t1;
    pthread_t *tids = malloc(nthr * sizeof(pthread_t));
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < nthr; i++)
        pthread_create(&tids[i], NULL, fn, (void*)(uintptr_t)i);
    for (int i = 0; i < nthr; i++)
        pthread_join(tids[i], NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double e = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    printf("%-24s thr=%d  ops=%ld  time=%.3fs  rate=%.0f ops/s\n",
           label, nthr, (long)total, e, total / e);
    free(tids);
}

int main() {
    for (int n = 1; n <= 8; n++) run("trylock-only", bench_trylock, n);
    for (int n = 1; n <= 8; n++) run("spinlock+20iters", bench_lock, n);
    return 0;
}
