#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define M 100003

int main(void) {
    long n = atol(getenv("BENCH_SCALE"));
    static long h[M];
    for (long k = 0; k < M; k++)
        h[k] = -1;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (long i = 0; i < n; i++) {
        long key = (i * 2654435761L) % M;
        h[key] = i;
    }
    long sum = 0, hits = 0;
    for (long i = 0; i < n; i++) {
        long key = (i * 2654435761L) % M;
        if (h[key] != -1) {
            hits++;
            sum += h[key];
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    long ns = (t1.tv_sec - t0.tv_sec) * 1000000000L + (t1.tv_nsec - t0.tv_nsec);
    printf("hash: hits = %ld sum = %ld\n", hits, sum);
    printf("__bench_ns: %ld\n", ns);
    return 0;
}
