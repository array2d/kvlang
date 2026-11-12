#include <kvspace/kvspace.h>
#include <kvspace/kvspace_xvalue.h>
#include <kvlang/xvalue.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "failed: %s\n", #x); return 1; } } while (0)

static int attr_ok(const uint8_t *value, uint32_t len, uint64_t body_len,
                   const char *langtype) {
    size_t lt = strlen(langtype);
    kvspaceXvalueHead h;
    memcpy(&h, value, sizeof h);
    if (kvspaceXvalueHeadLen(&h) != body_len ||
        !kvspaceXvalueHeadPaddingIsZero(&h))
        return 0;
    const uint64_t alen = kvspaceXvalueHeadAttrsLen(&h);
    if ((uint64_t)len != KVSPACE_XVALUE_HEAD_LEN + body_len + alen)
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
    uint8_t *value = NULL;
    uint32_t len = 0;
    kvspaceXvalueHead h;

    CHECK(kvlangXvalueNewInt64(42, "int64", &value, &len) == 0);
    CHECK(len == 32);
    memcpy(&h, value, sizeof h);
    CHECK(kvspaceXvalueHeadLen(&h) == 8);
    CHECK(attr_ok(value, len, 8, "int64"));
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--)
        v = (v << 8) | value[KVSPACE_XVALUE_HEAD_LEN + i];
    CHECK(v == 42);
    free(value);

    CHECK(kvlangXvalueNewBool(1, "bool", &value, &len) == 0);
    CHECK(len == 24);
    memcpy(&h, value, sizeof h);
    CHECK(kvspaceXvalueHeadLen(&h) == 1 && value[KVSPACE_XVALUE_HEAD_LEN] == 1);
    CHECK(attr_ok(value, len, 1, "bool"));
    free(value);
    CHECK(kvlangXvalueNewBool(0, "bool", &value, &len) == 0);
    CHECK(value[KVSPACE_XVALUE_HEAD_LEN] == 0);
    free(value);

    CHECK(kvlangXvalueNewFloat64(2.5, "float64", &value, &len) == 0);
    CHECK(len == 34);
    memcpy(&h, value, sizeof h);
    CHECK(kvspaceXvalueHeadLen(&h) == 8);
    CHECK(attr_ok(value, len, 8, "float64"));
    uint64_t bits = 0;
    for (int i = 7; i >= 0; i--)
        bits = (bits << 8) | value[KVSPACE_XVALUE_HEAD_LEN + i];
    double d;
    memcpy(&d, &bits, sizeof d);
    CHECK(d == 2.5);
    free(value);

    const uint8_t utf8[] = {0xe4, 0xbd, 0xa0, 0xf0, 0x9f, 0x98, 0x80};
    CHECK(kvlangXvalueNewChar(utf8, sizeof utf8, "char/utf8", &value, &len) == 0);
    CHECK(len == 16 + sizeof utf8 + 12);
    memcpy(&h, value, sizeof h);
    CHECK(kvspaceXvalueHeadLen(&h) == sizeof utf8);
    CHECK(memcmp(value + KVSPACE_XVALUE_HEAD_LEN, utf8, sizeof utf8) == 0);
    CHECK(attr_ok(value, len, sizeof utf8, "char/utf8"));
    free(value);
    CHECK(kvlangXvalueNewChar(NULL, 0, "", &value, &len) == 0);
    CHECK(len == 16);
    free(value);

    CHECK(kvlangXvalueNewPtr("int64", "/x", &value, &len) == 0);
    
    CHECK(len == KVSPACE_XVALUE_HEAD_LEN + 2 + 3 + 6);
    memcpy(&h, value, sizeof h);
    CHECK(kvspaceXvalueHeadLen(&h) == 2);
    CHECK(memcmp(value + KVSPACE_XVALUE_HEAD_LEN, "/x", 2) == 0);
    CHECK(attr_ok(value, len, 2, "*int64"));
    free(value);
    
    CHECK(kvlangXvalueNewPtr("", "/x", &value, &len) == 0);
    
    CHECK(len == KVSPACE_XVALUE_HEAD_LEN + 2 + 3 + 1);
    memcpy(&h, value, sizeof h);
    CHECK(kvspaceXvalueHeadLen(&h) == 2);
    
    CHECK(attr_ok(value, len, 2, "*"));
    free(value);
    CHECK(kvlangXvalueNewPtr("any", "/x", &value, &len) != 0);

    puts("ok");
    return 0;
}
