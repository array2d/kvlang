#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static long nq(long cols, long d1, long d2, long all) {
    if (cols == all)
        return 1;
    long cnt = 0;
    long avail = ((cols | d1 | d2) & all) ^ all;
    while (avail != 0) {
        long p = avail & (-avail);
        avail ^= p;
        cnt += nq(cols | p, (d1 | p) << 1, (d2 | p) >> 1, all);
    }
    return cnt;
}

int main(void) {
    long scale = atol(getenv("BENCH_SCALE"));
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    long all = (1L << scale) - 1;
    long ans = nq(0, 0, 0, all);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    long ns = (t1.tv_sec - t0.tv_sec) * 1000000000L + (t1.tv_nsec - t0.tv_nsec);
    printf("queens = %ld\n", ans);
    printf("__bench_ns: %ld\n", ns);
    return 0;
}
