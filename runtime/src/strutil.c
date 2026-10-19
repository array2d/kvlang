#include "runtime_internal.h"

void kvlangDie(const char *fmt, ...) {
    fputs("panic: ", stderr);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    abort();
}

size_t kvlangKeyFmt(char *buf, size_t cap, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= cap)
        kvlangDie("key exceeds %zu bytes", cap);
    return (size_t)n;
}

char *kvlangAstr(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *s = NULL;
    int n = vasprintf(&s, fmt, ap);
    va_end(ap);
    if (n < 0 || !s)
        kvlangDie("out of memory formatting string");
    return s;
}

char *kvlangAstrN(const char *s, size_t n) {
    char *p = malloc(n + 1);
    if (!p)
        kvlangDie("out of memory copying string");
    if (n)
        memcpy(p, s, n);
    p[n] = 0;
    return p;
}
