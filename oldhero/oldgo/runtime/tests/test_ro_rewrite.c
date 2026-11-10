/* ro=1 键可重复覆盖：ISA manifest 的发布路径依赖「同一 key 用 ro=1 重写」。
 * 覆盖 shm/fs/redis（由 KVLANG_TEST_SCHEME 选；redis 不可达时跳过）。 */

#include <kvspace/kvspace.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(c)                                                               \
    do {                                                                       \
        if (!(c)) {                                                            \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);       \
        }                                                                      \
    } while (0)

static void mk(char *out, size_t n, char fill) {
    for (size_t i = 0; i + 1 < n; i++)
        out[i] = fill;
    out[n - 1] = 0;
}

int main(void) {
    char dir[] = "/tmp/kvlang-ro-XXXXXX";
    char dsn[256];
    const char *scheme = getenv("KVLANG_TEST_SCHEME");
    if (!mkdtemp(dir))
        return 2;
    if (scheme && strcmp(scheme, "redis") == 0)
        snprintf(dsn, sizeof dsn, "redis://127.0.0.1:6379");
    else
        snprintf(dsn, sizeof dsn, "%s://%s/s", scheme ? scheme : "shm", dir);
    void *h = kvspaceConnect(dsn);
    if (!h)
        return 2;
    char a[128], b[160];
    mk(a, sizeof a, 'a');
    mk(b, sizeof b, 'b');
    uint8_t *v1 = NULL, *v2 = NULL;
    uint32_t l1 = 0, l2 = 0;
    /* ‥core 是系统键：langtype 传空串。 */
    CHECK(kvspaceNewChar((const uint8_t *)a, (uint32_t)strlen(a), "", &v1,
                         &l1) == 0);
    CHECK(kvspaceNewChar((const uint8_t *)b, (uint32_t)strlen(b), "", &v2,
                         &l2) == 0);
    char key[128];
    snprintf(key, sizeof key, "/isa-ro-%d/‥core", (int)getpid());
    int rc1 = kvspaceSetValue(h, key, v1, l1, NULL, 0);
    int rc2 = kvspaceSetValue(h, key, v2, l2, NULL, 0);
    CHECK(rc1 == 0);
    CHECK(rc2 == 0);
    uint8_t *out = NULL;
    uint32_t olen = 0;
    CHECK(kvspaceGet(h, key, 0, &out, &olen) == 0);
    CHECK(out && olen == l2 && memcmp(out, v2, l2) == 0);
    kvspaceClose(h);
    free(v1);
    free(v2);
    if (failures) {
        fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    printf("ok: ro=1 rewrite (%s)\n", scheme ? scheme : "shm");
    return 0;
}
