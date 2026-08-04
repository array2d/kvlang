#include <math.h>
#include <stdio.h>

int main(void) {
    volatile int lhs = 10;
    volatile int rhs = 3;
    volatile double base = 2.0;
    volatile double exponent = 5.0;
    volatile double radicand = 144.0;

    printf("add: %d\n", lhs + rhs);
    printf("sub: %d\n", lhs - rhs);
    printf("mul: %d\n", lhs * rhs);
    printf("mul(×): %d\n", lhs * rhs);
    printf("div: %d\n", lhs / rhs);
    printf("div(÷): %d\n", lhs / rhs);
    printf("mod: %d\n", lhs % rhs);
    printf("pow: %.1f\n", pow(base, exponent));
    printf("sqrt: %.1f\n", sqrt(radicand));
    printf("sqrt(√): %.1f\n", sqrt(radicand));
    return 0;
}
