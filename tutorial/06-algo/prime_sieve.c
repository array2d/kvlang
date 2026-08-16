#include <stdbool.h>
#include <stdio.h>
#include <time.h>

static void prime_sieve(int limit) {
    printf("primes up to %d\n", limit);
    int count = 0;
    for (int n = 2; n <= limit; ++n) {
        bool is_prime = true;
        for (int divisor = 2; divisor < n; ++divisor)
            if (n % divisor == 0) { is_prime = false; break; }
        if (is_prime) { printf("  prime: %d\n", n); ++count; }
    }
    printf("total primes up to %d = %d\n", limit, count);
}

int main(void) {
    struct timespec t0, t1;
    volatile int limit = 200;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    prime_sieve(limit);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    long us = (t1.tv_sec-t0.tv_sec)*1000000L + (t1.tv_nsec-t0.tv_nsec)/1000L;
    printf("__bench_us: %ld\n", us);
    return 0;
}
