#include "runtime_internal.h"

static int log_level(void) {
    const char *lv = getenv("LOG_LEVEL");
    if (!lv || !lv[0] || strcmp(lv, "warn") == 0) return 2;
    if (strcmp(lv, "debug") == 0) return 0;
    if (strcmp(lv, "info") == 0) return 1;
    if (strcmp(lv, "error") == 0) return 3;
    return 2;
}

void log_debug(const char *fmt, ...) {
    if (log_level() > 0) return;
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

void log_info(const char *fmt, ...) {
    if (log_level() > 1) return;
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

void log_error(const char *fmt, ...) {
    if (log_level() > 3) return;
    fputs("error: ", stderr);
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}
