#include "runtime_internal.h"

static void kvlangStrbufReserve(kvlangStrbuf_t *b, size_t need) {
    if (b->cap >= need) return;
    size_t cap = b->cap ? b->cap * 2 : 32;
    while (cap < need) cap *= 2;
    b->p = realloc(b->p, cap);
    b->cap = cap;
}

void kvlangStrbufPutc(kvlangStrbuf_t *b, char c) {
    kvlangStrbufReserve(b, b->len + 2);
    b->p[b->len++] = c;
    b->p[b->len] = '\0';
}

void kvlangStrbufPutn(kvlangStrbuf_t *b, const char *s, size_t n) {
    if (n == 0) {
        if (!b->p) { b->p = calloc(1, 1); b->cap = 1; }
        return;
    }
    kvlangStrbufReserve(b, b->len + n + 1);
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = '\0';
}

void kvlangStrbufPrintf(kvlangStrbuf_t *b, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return; }
    kvlangStrbufReserve(b, b->len + (size_t)n + 1);
    vsnprintf(b->p + b->len, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    b->len += (size_t)n;
}

char *kvlangStrbufDetach(kvlangStrbuf_t *b) {
    if (b->p == NULL) return strdup("");
    char *p = b->p;
    b->p = NULL; b->len = b->cap = 0;
    return p;
}
