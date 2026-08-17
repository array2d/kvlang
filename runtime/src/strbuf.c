#include "runtime_internal.h"

static void sb_reserve(sbuf_t *b, size_t need) {
    if (b->cap >= need) return;
    size_t cap = b->cap ? b->cap * 2 : 32;
    while (cap < need) cap *= 2;
    b->p = realloc(b->p, cap);
    b->cap = cap;
}

void sb_putc(sbuf_t *b, char c) {
    sb_reserve(b, b->len + 2);
    b->p[b->len++] = c;
    b->p[b->len] = '\0';
}

void sb_putn(sbuf_t *b, const char *s, size_t n) {
    if (n == 0) {
        if (!b->p) { b->p = calloc(1, 1); b->cap = 1; }
        return;
    }
    sb_reserve(b, b->len + n + 1);
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = '\0';
}

void sb_printf(sbuf_t *b, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return; }
    sb_reserve(b, b->len + (size_t)n + 1);
    vsnprintf(b->p + b->len, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    b->len += (size_t)n;
}

char *sb_detach(sbuf_t *b) {
    if (b->p == NULL) return strdup("");
    char *p = b->p;
    b->p = NULL; b->len = b->cap = 0;
    return p;
}
