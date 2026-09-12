#define _GNU_SOURCE
#include <errno.h>
#include <stdlib.h>   /* getprogname（macOS/BSD） */
#include "runtime_internal.h"

/* 可执行文件名（basename）：runtime 诊断日志前缀，区别于 kvcode print（stdout、无前缀）。 */
static const char *kvlangExe(void) {
#if defined(__APPLE__)
    /* macOS/BSD 无 glibc 的 program_invocation_short_name，用 getprogname()。 */
    const char *p = getprogname();
#else
    const char *p = program_invocation_short_name;
#endif
    return (p && p[0]) ? p : "kvlang";
}

static int kvlangLogLevel(void) {
    /* LOG_LEVEL 运行期不变：首调解析后缓存，免每条日志（每指令一次）重复 getenv+strcmp。 */
    static int cached = -1;
    if (cached >= 0) return cached;
    const char *lv = getenv("LOG_LEVEL");
    if (!lv || !lv[0] || strcmp(lv, "warn") == 0) cached = 2;
    else if (strcmp(lv, "debug") == 0) cached = 0;
    else if (strcmp(lv, "info") == 0) cached = 1;
    else if (strcmp(lv, "error") == 0) cached = 3;
    else cached = 2;
    return cached;
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
