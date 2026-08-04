#include <stdio.h>

static long long factorial(int n) {
    long long result = 1;
    for (int i = 1; i <= n; ++i) {
        result *= i;
    }
    return result;
}

int main(void) {
    volatile int n = 10;
    printf("fact = %lld\n", factorial(n));
    return 0;
}
