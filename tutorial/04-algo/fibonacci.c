#include <stdio.h>

static long long fibonacci(int n) {
    if (n <= 1) {
        return n;
    }
    long long a = 0;
    long long b = 1;
    for (int i = 2; i <= n; ++i) {
        long long next = a + b;
        a = b;
        b = next;
    }
    return b;
}

int main(void) {
    volatile int n = 10;
    printf("fib = %lld\n", fibonacci(n));
    return 0;
}
