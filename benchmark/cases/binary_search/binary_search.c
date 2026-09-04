#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static long bsearch_idx(const long *arr, long n, long target) {
    long lo = 0, hi = n - 1, idx = -1;
    while (lo <= hi) {
        long mid = (lo + hi) / 2;
        long mv = arr[mid];
        if (mv == target) {
            idx = mid;
            lo = hi + 1;
        } else if (mv < target) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return idx;
}

int main(void) {
    long n = atol(getenv("BENCH_SCALE"));
    long arr[n];
    for (long i = 0; i < n; i++)
        arr[i] = i;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    long sum = 0, found = 0;
    for (long q = 0; q < n; q++) {
        long idx = bsearch_idx(arr, n, q);
        if (idx != -1) {
            found++;
            sum += idx;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    long ns = (t1.tv_sec - t0.tv_sec) * 1000000000L + (t1.tv_nsec - t0.tv_nsec);
    printf("bsearch: found = %ld sum = %ld\n", found, sum);
    printf("__bench_ns: %ld\n", ns);
    return 0;
}
