#include "runtime_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;

#define CHECK(c) do { \
    if (!(c)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
        failures++; \
    } \
} while (0)

int main(void) {
    char dir[] = "/tmp/kvs-invalid-head-XXXXXX";
    char dsn[256];
    if (!mkdtemp(dir))
        return 1;
    const char *scheme = getenv("KVLANG_TEST_SCHEME");
    snprintf(dsn, sizeof dsn, "%s://%s/s", scheme ? scheme : "shm", dir);
    kvspace_t *kv = kvlangKvspaceConnect(dsn);
    if (!kv)
        return 1;

    kvlangXvalue_t good;
    kvlangXvalueNewInt64(&good, 42);
    kvlangKvPair_t pair = {.key = "/value", .value = good};
    char err[128] = {0};
    CHECK(kvlangKvspaceSet(kv, &pair, 1, err, sizeof err) == 0);

    kvlangXvalue_t got;
    CHECK(kvlangKvspaceGetOne(kv, pair.key, &got) == 0);
    uint8_t before[128];
    uint32_t before_len = got.length;
    if (!got.bytes || before_len == 0 || before_len > sizeof before) {
        kvlangKvspaceDisconnect(kv);
        return 1;
    }
    memcpy(before, got.bytes, before_len);

    uint8_t incompatible[128] = {6, 1};
    pair.value = (kvlangXvalue_t){.bytes = incompatible, .length = before_len};
    err[0] = 0;
    CHECK(kvlangKvspaceSet(kv, &pair, 1, err, sizeof err) != 0);
    CHECK(strstr(err, "invalid XValue") != NULL);
    CHECK(kvlangKvspaceGetOne(kv, pair.key, &got) == 0);
    CHECK(got.bytes && got.length == before_len &&
          memcmp(got.bytes, before, before_len) == 0);

    /* 值短于 head 声称的 body（head+body 放不进）：head 不自洽 → 拒绝。
     * 注意：v2 值尾是自描述 attr 区（#571），只截 attr 尾后端不拒。 */
    pair.value = (kvlangXvalue_t){.bytes = before,
                                  .length = KVSPACE_XVALUE_HEAD_LEN + 7};
    err[0] = 0;
    CHECK(kvlangKvspaceSet(kv, &pair, 1, err, sizeof err) != 0);
    CHECK(strstr(err, "invalid XValue") != NULL);
    CHECK(kvlangKvspaceGetOne(kv, pair.key, &got) == 0);
    CHECK(got.bytes && got.length == before_len &&
          memcmp(got.bytes, before, before_len) == 0);

    pair.key = "/missing";
    pair.value = (kvlangXvalue_t){.bytes = incompatible, .length = before_len};
    err[0] = 0;
    CHECK(kvlangKvspaceSet(kv, &pair, 1, err, sizeof err) != 0);
    CHECK(strstr(err, "invalid XValue") != NULL);
    CHECK(kvlangKvspaceGetOne(kv, pair.key, &got) == 0 && got.length == 0);

    kvlangXvalue_t changed;
    kvlangXvalueNewFloat64(&changed, 3.5);
    pair.key = "/value";
    pair.value = changed;
    CHECK(kvlangKvspaceSet(kv, &pair, 1, err, sizeof err) == 0);
    CHECK(kvlangKvspaceGetOne(kv, pair.key, &got) == 0);
    CHECK(kvlangXvalueKindIs(&got, KVSPACE_KIND_FLOAT64));
    CHECK(kvlangKvspaceSetChar(kv, pair.key, "12345678") == 0);
    CHECK(kvlangKvspaceGetOne(kv, pair.key, &got) == 0);
    CHECK(kvlangXvalueKindIs(&got, KVSPACE_KIND_CHAR_UTF8));

    kvlangKvspaceDisconnect(kv);
    return failures ? 1 : 0;
}
