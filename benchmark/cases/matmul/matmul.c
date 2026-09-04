#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(void) {
    int n = atoi(getenv("BENCH_SCALE"));
    double A[n * n], B[n * n];
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            int idx = i * n + j;
            A[idx] = (double)((i + j) % 4) * 0.25 + 0.25;
            B[idx] = (double)((i * 2 + j) % 4) * 0.25 + 0.25;
        }
    }
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    double checksum = 0.0;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            double acc = 0.0;
            for (int k = 0; k < n; k++)
                acc += A[i * n + k] * B[k * n + j];
            checksum += acc;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    long ns = (t1.tv_sec - t0.tv_sec) * 1000000000L + (t1.tv_nsec - t0.tv_nsec);
    long out = (long)(checksum * 1000000.0);
    printf("matmul: check = %ld\n", out);
    printf("__bench_ns: %ld\n", ns);
    return 0;
}
