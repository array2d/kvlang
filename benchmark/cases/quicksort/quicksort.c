#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static void swap(long *a, long i, long j) {
    long t = a[i];
    a[i] = a[j];
    a[j] = t;
}

static void qsort_r(long *a, long lo, long hi) {
    if (lo < hi) {
        long pivot = a[hi];
        long i = lo - 1;
        for (long j = lo; j < hi; j++) {
            if (a[j] <= pivot) {
                i++;
                swap(a, i, j);
            }
        }
        swap(a, i + 1, hi);
        long p = i + 1;
        qsort_r(a, lo, p - 1);
        qsort_r(a, p + 1, hi);
    }
}

int main(void) {
    long n = atol(getenv("BENCH_SCALE"));
    long arr[n];
    long seed = 1;
    for (long i = 0; i < n; i++) {
        seed = (seed * 1103515245 + 12345) % 2147483648;
        arr[i] = seed % 100;
    }
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    qsort_r(arr, 0, n - 1);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    long ns = (t1.tv_sec - t0.tv_sec) * 1000000000L + (t1.tv_nsec - t0.tv_nsec);
    printf("qsort: a0 = %ld amid = %ld alast = %ld\n", arr[0], arr[n / 2], arr[n - 1]);
    printf("__bench_ns: %ld\n", ns);
    return 0;
}
