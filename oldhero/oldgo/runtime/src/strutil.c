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

static void key_append(char *buf, size_t cap, size_t *off, const char *s,
                       size_t n) {
    if (*off + n >= cap)
        kvlangDie("key exceeds %zu bytes", cap);
    memcpy(buf + *off, s, n);
    *off += n;
}

static void key_append_str(char *buf, size_t cap, size_t *off, const char *s) {
    if (!s)
        kvlangDie("key format: null string");
    key_append(buf, cap, off, s, strlen(s));
}

static void key_append_int(char *buf, size_t cap, size_t *off, int v) {
    char tmp[12];
    int n = 0;
    unsigned u = v < 0 ? (unsigned)(-(long long)v) : (unsigned)v;
    do {
        tmp[n++] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    if (v < 0)
        tmp[n++] = '-';
    if (*off + (size_t)n >= cap)
        kvlangDie("key exceeds %zu bytes", cap);
    while (n)
        buf[(*off)++] = tmp[--n];
}

/* 热路径专用：只接受 %s / %d / %.*s（当前所有调用点即此三类）。
 * 其它格式直接 abort，避免静默产出错误的 key；冷路径用 kvlangStringFormat。 */
size_t kvlangKeyFormat(char *buf, size_t cap, const char *fmt, ...) {
    if (!buf || cap == 0)
        kvlangDie("key format: empty buffer");
    va_list ap;
    va_start(ap, fmt);
    size_t off = 0;
    const char *p = fmt;
    while (*p) {
        const char *lit = p;
        while (*p && *p != '%')
            p++;
        key_append(buf, cap, &off, lit, (size_t)(p - lit));
        if (!*p)
            break;
        p++;
        if (*p == '%') {
            key_append(buf, cap, &off, "%", 1);
            p++;
        } else if (*p == 's') {
            key_append_str(buf, cap, &off, va_arg(ap, const char *));
            p++;
        } else if (*p == 'd') {
            key_append_int(buf, cap, &off, va_arg(ap, int));
            p++;
        } else if (p[0] == '.' && p[1] == '*' && p[2] == 's') {
            int n = va_arg(ap, int);
            const char *s = va_arg(ap, const char *);
            if (!s || n < 0)
                kvlangDie("key format: invalid %%.*s");
            key_append(buf, cap, &off, s, (size_t)n);
            p += 3;
        } else {
            kvlangDie("unsupported key format: %s", fmt);
        }
    }
    va_end(ap);
    if (off >= cap)
        kvlangDie("key exceeds %zu bytes", cap);
    buf[off] = 0;
    return off;
}

char *kvlangStringFormat(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *s = NULL;
    int n = vasprintf(&s, fmt, ap);
    va_end(ap);
    if (n < 0 || !s)
        kvlangDie("out of memory formatting string");
    return s;
}

char *kvlangStringCopyN(const char *s, size_t n) {
    char *p = malloc(n + 1);
    if (!p)
        kvlangDie("out of memory copying string");
    if (n)
        memcpy(p, s, n);
    p[n] = 0;
    return p;
}
