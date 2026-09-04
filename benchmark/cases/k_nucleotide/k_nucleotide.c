#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(void) {
    const char *base =
        "GGTATTGAGCACTGGCAATTGACGTCAGGTATCCGAATTGCACGTTAGCATGCATGCATGCACGT";
    int rep = atoi(getenv("BENCH_SCALE"));
    char s[strlen(base) * rep + 1];
    s[0] = '\0';
    for (int r = 0; r < rep; r++)
        strcat(s, base);
    long h[128] = {0};
    long n = strlen(s);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (long i = 0; i < n; i++)
        h[(int)s[i]]++;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    long ns = (t1.tv_sec - t0.tv_sec) * 1000000000L + (t1.tv_nsec - t0.tv_nsec);
    printf("knuc: A = %ld C = %ld G = %ld T = %ld\n", h[65], h[67], h[71],
           h[84]);
    printf("__bench_ns: %ld\n", ns);
    return 0;
}
