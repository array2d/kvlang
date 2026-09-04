#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static long iops(long n) {
    long a = 0;
    for (long i = 1; i <= n; ++i)
        a = a + 1;
    return a;
}

int main(void) {
    long scale = atol(getenv("BENCH_SCALE"));
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    volatile long ans = iops(scale);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    long ns = (t1.tv_sec - t0.tv_sec) * 1000000000L + (t1.tv_nsec - t0.tv_nsec);
    printf("iops a = %ld\n", ans);
    printf("__bench_ns: %ld\n", ns);
    return 0;
}
