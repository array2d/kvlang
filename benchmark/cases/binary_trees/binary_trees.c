#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct Node {
    struct Node *l, *r;
} Node;

Node *make(int d) {
    Node *n = malloc(sizeof(Node));
    if (d == 0) {
        n->l = NULL;
        n->r = NULL;
    } else {
        n->l = make(d - 1);
        n->r = make(d - 1);
    }
    return n;
}

long check(Node *n) {
    if (n->l == NULL)
        return 1;
    return 1 + check(n->l) + check(n->r);
}

int main(void) {
    int depth = atoi(getenv("BENCH_SCALE"));
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    Node *root = make(depth);
    long count = check(root);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    long ns = (t1.tv_sec - t0.tv_sec) * 1000000000L + (t1.tv_nsec - t0.tv_nsec);
    printf("bintree: nodes = %ld\n", count);
    printf("__bench_ns: %ld\n", ns);
    return 0;
}
