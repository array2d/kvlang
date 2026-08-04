#include <stdio.h>

static long long gcd(long long a, long long b) {
    if (b == 0) {
        return a;
    }
    return gcd(b, a % b);
}

int main(void) {
    volatile long long a = 48;
    volatile long long b = 18;
    printf("gcd = %lld\n", gcd(a, b));
    return 0;
}
