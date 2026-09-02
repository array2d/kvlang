#define _GNU_SOURCE
#include <errno.h>
#include "runtime_internal.h"

/* 可执行文件名（basename）：runtime 诊断日志前缀，区别于 kvcode print（stdout、无前缀）。 */
static const char *kvlangExe(void) {
    const char *p = program_invocation_short_name;
    return (p && p[0]) ? p : "kvlang";
}

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
    fprintf(stderr, "%s: ", kvlangExe());
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

void kvlangLogInfo(const char *fmt, ...) {
    if (kvlangLogLevel() > 1) return;
    fprintf(stderr, "%s: ", kvlangExe());
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

void kvlangLogError(const char *fmt, ...) {
    if (kvlangLogLevel() > 3) return;
    fprintf(stderr, "%s: error: ", kvlangExe());
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}
