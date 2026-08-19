#include "runtime_internal.h"

static int kvlangLogLevel(void) {
    const char *lv = getenv("LOG_LEVEL");
    if (!lv || !lv[0] || strcmp(lv, "warn") == 0) return 2;
    if (strcmp(lv, "debug") == 0) return 0;
    if (strcmp(lv, "info") == 0) return 1;
    if (strcmp(lv, "error") == 0) return 3;
    return 2;
}

void kvlangLogDebug(const char *fmt, ...) {
    if (kvlangLogLevel() > 0) return;
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

void kvlangLogInfo(const char *fmt, ...) {
    if (kvlangLogLevel() > 1) return;
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

void kvlangLogError(const char *fmt, ...) {
    if (kvlangLogLevel() > 3) return;
    fputs("error: ", stderr);
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}
