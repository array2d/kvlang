#include <stdbool.h>
#include <stdio.h>

static void prime_sieve(int limit) {
    printf("primes up to %d\n", limit);
    int count = 0;
    for (int n = 2; n <= limit; ++n) {
        bool is_prime = true;
        for (int divisor = 2; divisor < n; ++divisor) {
            if (n % divisor == 0) {
                is_prime = false;
                break;
            }
        }
        if (is_prime) {
            printf("  prime: %d\n", n);
            ++count;
        }
    }
    printf("total primes up to %d = %d\n", limit, count);
}

int main(void) {
    volatile int limit = 30;
    prime_sieve(limit);
    return 0;
}
