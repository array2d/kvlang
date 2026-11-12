#include <kvspace/kvspace.h>
#include <kvspace/kvspace_xvalue.h>
#include "kvlangxvalue.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
static void *store;
static unsigned key_sequence;

#define CHECK(x, ...)                                                          \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "  FAIL %s:%d: ", __FILE__, __LINE__);             \
            fprintf(stderr, __VA_ARGS__);                                      \
            fprintf(stderr, "\n");                                             \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

static const char *next_key(void) {
    static char buffer[32];
    snprintf(buffer, sizeof buffer, "/codec/%u", key_sequence++);
    return buffer;
}

/* 校验 head 与 attr 区：bodylen/attrslen/padding 正确，langtype 记录与 lt 一致。 */
static int attr_ok(const uint8_t *value, uint64_t len, uint64_t body_len,
                   const char *langtype) {
    size_t lt = strlen(langtype);
    kvspaceXvalueHead h;
    memcpy(&h, value, sizeof h);
    if (kvspaceXvalueHeadLen(&h) != body_len || !kvspaceXvalueHeadPaddingIsZero(&h))
        return 0;
    const uint64_t alen = kvspaceXvalueHeadAttrsLen(&h);
    if (len != KVSPACE_XVALUE_HEAD_LEN + body_len + alen)
        return 0;
    const uint8_t *a = value + KVSPACE_XVALUE_HEAD_LEN + body_len;
    if (lt == 0)
        return alen == 0;
    if (alen != 3 + lt)
        return 0;
    return a[0] == KVLANG_XVALUE_ATTR_TAG_LANGTYPE && a[1] == lt && a[2] == 0 &&
           memcmp(a + 3, langtype, lt) == 0;
}

int main(void) {
    char dsn[128];
    char path[128];
    snprintf(path, sizeof path, "/tmp/kvlangxvalue-codec-%ld", (long)getpid());
    snprintf(dsn, sizeof dsn, "shm://%s", path);
    unlink(path);
    store = kvspaceConnect(dsn);
    if (!store) {
        fprintf(stderr, "cannot connect %s\n", dsn);
        return 1;
    }

    uint8_t *value = kvlangXvalueNewInt64(store, next_key(), 42, "int64");
    CHECK(value != NULL, "int64 new");
    if (value) {
        const uint64_t len = kvspaceXvalueHeadTotalLen(
            (const kvspaceXvalueHead *)(const void *)value);
        kvspaceXvalueHead h;
        memcpy(&h, value, sizeof h);
        CHECK(len == 32, "int64 len");
        CHECK(kvspaceXvalueHeadLen(&h) == 8, "int64 bodylen");
        CHECK(attr_ok(value, len, 8, "int64"), "int64 attr");
        uint64_t v = 0;
        for (int i = 7; i >= 0; i--)
            v = (v << 8) | value[KVSPACE_XVALUE_HEAD_LEN + i];
        CHECK(v == 42, "int64 value");
    }

    value = kvlangXvalueNewBool(store, next_key(), 1, "bool");
    CHECK(value != NULL, "bool new");
    if (value) {
        kvspaceXvalueHead h;
        memcpy(&h, value, sizeof h);
        CHECK(kvspaceXvalueHeadTotalLen(&h) == 24, "bool len");
        CHECK(kvspaceXvalueHeadLen(&h) == 1 && value[16] == 1, "bool body");
        CHECK(attr_ok(value, kvspaceXvalueHeadTotalLen(&h), 1, "bool"),
              "bool attr");
    }
    value = kvlangXvalueNewBool(store, next_key(), 0, "bool");
    CHECK(value != NULL && value[16] == 0, "bool false body");

    value = kvlangXvalueNewFloat64(store, next_key(), 2.5, "float64");
    CHECK(value != NULL, "float64 new");
    if (value) {
        const uint64_t len = kvspaceXvalueHeadTotalLen(
            (const kvspaceXvalueHead *)(const void *)value);
        CHECK(len == 34, "float64 len");
        CHECK(attr_ok(value, len, 8, "float64"), "float64 attr");
        uint64_t bits = 0;
        for (int i = 7; i >= 0; i--)
            bits = (bits << 8) | value[KVSPACE_XVALUE_HEAD_LEN + i];
        double d;
        memcpy(&d, &bits, sizeof d);
        CHECK(d == 2.5, "float64 value");
    }

    const uint8_t utf8[] = {0xe4, 0xbd, 0xa0, 0xf0, 0x9f, 0x98, 0x80};
    value = kvlangXvalueNewChar(store, next_key(), utf8, sizeof utf8,
                                "char/utf8");
    CHECK(value != NULL, "char new");
    if (value) {
        const uint64_t len = kvspaceXvalueHeadTotalLen(
            (const kvspaceXvalueHead *)(const void *)value);
        CHECK(len == 16 + sizeof utf8 + 12, "char len");
        CHECK(memcmp(value + KVSPACE_XVALUE_HEAD_LEN, utf8, sizeof utf8) == 0,
              "char body");
        CHECK(attr_ok(value, len, sizeof utf8, "char/utf8"), "char attr");
    }
    value = kvlangXvalueNewChar(store, next_key(), NULL, 0, "");
    CHECK(value != NULL &&
              kvspaceXvalueHeadTotalLen(
                  (const kvspaceXvalueHead *)(const void *)value) == 16,
          "char empty len");

    value = kvlangXvalueNewPtrFromKindexpr(store, next_key(), "int64", "/x");
    CHECK(value != NULL, "ptr new");
    if (value) {
        const uint64_t len = kvspaceXvalueHeadTotalLen(
            (const kvspaceXvalueHead *)(const void *)value);
        kvspaceXvalueHead h;
        memcpy(&h, value, sizeof h);
        /* langtype 记录带 `*` 前缀：record = "*int64"（6 字节）。 */
        CHECK(len == KVSPACE_XVALUE_HEAD_LEN + 2 + 3 + 6, "ptr len");
        CHECK(kvspaceXvalueHeadLen(&h) == 2, "ptr bodylen");
        CHECK(memcmp(value + KVSPACE_XVALUE_HEAD_LEN, "/x", 2) == 0, "ptr body");
        CHECK(attr_ok(value, len, 2, "*int64"), "ptr attr");
    }

    /* 空 kindexpr = 不约束：Ptr 目标允许为空；`any` 不是类型名，创建即拒。 */
    value = kvlangXvalueNewPtrFromKindexpr(store, next_key(), "", "/x");
    CHECK(value != NULL, "ptr empty kindexpr");
    if (value) {
        const uint64_t len = kvspaceXvalueHeadTotalLen(
            (const kvspaceXvalueHead *)(const void *)value);
        CHECK(len == KVSPACE_XVALUE_HEAD_LEN + 2 + 3 + 1, "ptr bare star len");
        CHECK(attr_ok(value, len, 2, "*"), "ptr bare star attr");
    }
    CHECK(kvlangXvalueNewPtrFromKindexpr(store, next_key(), "any", "/x") == NULL,
          "any kindexpr rejected");

    kvspaceClose(store);
    unlink(path);
    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("ok");
    return 0;
}
