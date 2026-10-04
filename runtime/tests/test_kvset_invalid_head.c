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
    kvlangKv_t *kv = kvlangKvConnect(dsn);
    if (!kv)
        return 1;

    kvlangXvalue_t good;
    kvlangXvalueNewInt64(&good, 42);
    kvlangKvPair_t pair = {.key = "/value", .val = good};
    char err[128] = {0};
    CHECK(kvlangKvSet(kv, &pair, 1, err, sizeof err) == 0);
    kvlangXvalueFree(&good);

    kvlangXvalue_t got;
    CHECK(kvlangKvGetOne(kv, pair.key, &got) == 0);
    uint8_t before[128];
    uint32_t before_len = got.len;
    if (!got.data || before_len == 0 || before_len > sizeof before) {
        kvlangKvDisconnect(kv);
        return 1;
    }
    memcpy(before, got.data, before_len);
    kvlangKvReadReset(kv);

    uint8_t incompatible[128] = {6, 1};
    pair.val = (kvlangXvalue_t){.data = incompatible, .len = before_len};
    err[0] = 0;
    CHECK(kvlangKvSet(kv, &pair, 1, err, sizeof err) != 0);
    CHECK(strstr(err, "invalid XValue") != NULL);
    CHECK(kvlangKvGetOne(kv, pair.key, &got) == 0);
    CHECK(got.data && got.len == before_len &&
          memcmp(got.data, before, before_len) == 0);
    kvlangKvReadReset(kv);

    pair.val = (kvlangXvalue_t){.data = before, .len = before_len - 1};
    err[0] = 0;
    CHECK(kvlangKvSet(kv, &pair, 1, err, sizeof err) != 0);
    CHECK(strstr(err, "invalid XValue") != NULL);
    CHECK(kvlangKvGetOne(kv, pair.key, &got) == 0);
    CHECK(got.data && got.len == before_len &&
          memcmp(got.data, before, before_len) == 0);
    kvlangKvReadReset(kv);

    pair.key = "/missing";
    pair.val = (kvlangXvalue_t){.data = incompatible, .len = before_len};
    err[0] = 0;
    CHECK(kvlangKvSet(kv, &pair, 1, err, sizeof err) != 0);
    CHECK(strstr(err, "invalid XValue") != NULL);
    CHECK(kvlangKvGetOne(kv, pair.key, &got) == 0 && got.len == 0);
    kvlangKvReadReset(kv);

    kvlangXvalue_t changed;
    kvlangXvalueNewFloat64(&changed, 3.5);
    pair.key = "/value";
    pair.val = changed;
    CHECK(kvlangKvSet(kv, &pair, 1, err, sizeof err) == 0);
    kvlangXvalueFree(&changed);
    CHECK(kvlangKvGetOne(kv, pair.key, &got) == 0);
    CHECK(kvlangXvalueKindIs(&got, KVSPACE_KIND_FLOAT64));
    kvlangKvReadReset(kv);
    CHECK(kvlangKvSetChar(kv, pair.key, "12345678") == 0);
    CHECK(kvlangKvGetOne(kv, pair.key, &got) == 0);
    CHECK(kvlangXvalueKindIs(&got, KVSPACE_KIND_CHAR_UTF8));
    kvlangKvReadReset(kv);

    kvlangKvDisconnect(kv);
    return failures ? 1 : 0;
}
