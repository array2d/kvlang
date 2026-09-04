#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static void prime_sieve(long limit) {
    printf("primes up to %ld\n", limit);
    long count = 0;
    for (long n = 2; n <= limit; ++n) {
        bool is_prime = true;
        for (long d = 2; d < n; ++d)
            if (n % d == 0) {
                is_prime = false;
                break;
            }
        if (is_prime) {
            printf("  prime: %ld\n", n);
            ++count;
        }
    }
    printf("total primes up to %ld = %ld\n", limit, count);
}

int main(void) {
    long scale = atol(getenv("BENCH_SCALE"));
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    prime_sieve(scale);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    long ns = (t1.tv_sec - t0.tv_sec) * 1000000000L + (t1.tv_nsec - t0.tv_nsec);
    printf("__bench_ns: %ld\n", ns);
    return 0;
}
