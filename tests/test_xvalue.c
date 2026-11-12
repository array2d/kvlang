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
static char store_path[128];
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

/* 每个构造点一个独立的 key：值直接落在 kvspace 里，复用 key 会覆盖前一个值。 */
static const char *next_key(void) {
    static char buffer[32];
    snprintf(buffer, sizeof buffer, "/test/%u", key_sequence++);
    return buffer;
}

static uint64_t wire_total(const uint8_t *wire) {
    return kvspaceXvalueHeadTotalLen((const kvspaceXvalueHead *)(const void *)wire);
}

/* 校验 head、body、langtype、有效长度。lt 是构造后的完整 langtype（含 *）。 */
static int value_ok(const uint8_t *v, uint64_t total, uint64_t body_len,
                    const char *lt) {
    size_t want = strlen(lt);
    kvspaceXvalueHead h;
    memcpy(&h, v, sizeof h);
    if (kvspaceXvalueHeadLen(&h) != body_len || !kvspaceXvalueHeadPaddingIsZero(&h))
        return 0;
    if (kvspaceXvalueHeadTotalLen(&h) != total)
        return 0;
    const uint64_t alen = kvspaceXvalueHeadAttrsLen(&h);
    if (total != KVSPACE_XVALUE_HEAD_LEN + body_len + alen)
        return 0;
    const uint8_t *ltp = NULL;
    uint16_t n = kvlangXvalueAttrLangtypeBounded(v, total, &ltp);
    if (n != want || (want && (!ltp || memcmp(ltp, lt, want) != 0)))
        return 0;
    uint64_t eff = UINT64_MAX;
    if (kvspaceXvalueHeadEffectiveLen(v, total, &eff) != 0 || eff != body_len)
        return 0;
    if (want) {
        const uint8_t *a = v + KVSPACE_XVALUE_HEAD_LEN + body_len;
        if (a[0] != KVLANG_XVALUE_ATTR_TAG_LANGTYPE || a[1] != want || a[2] != 0)
            return 0;
    }
    return 1;
}

static void test_scalar(void) {
    uint8_t raw[8] = {0x2a, 0, 0, 0, 0, 0, 0, 0};
    uint8_t *wire = kvlangXvalueNewT(store, next_key(), raw, 8, "int64");
    CHECK(wire != NULL, "i64 new");
    if (wire) {
        CHECK(wire_total(wire) == 32, "i64 len");
        CHECK(memcmp(wire + KVSPACE_XVALUE_HEAD_LEN, raw, 8) == 0, "i64 body@16");
        CHECK(value_ok(wire, wire_total(wire), 8, "int64"), "i64 langtype attr");
    }

    wire = kvlangXvalueNewT(store, next_key(), raw, 8, "");
    CHECK(wire != NULL, "scalar no-langtype");
    if (wire)
        CHECK(value_ok(wire, wire_total(wire), 8, ""), "no-langtype empty attr");

    /* NewT 允许 NULL body（零填，Reserve 语义）；拒绝由各类型构造器做。 */
}

static void test_slack(void) {
    const uint8_t hi[] = {'h', 'i'};
    uint8_t *wire = kvlangXvalueNewBufferArray(store, next_key(), hi, sizeof hi,
                                               "[]char/utf8");
    CHECK(wire != NULL, "buffer array new");
    if (wire) {
        CHECK(wire_total(wire) == 16 + 2 + 14, "buffer array len");
        CHECK(memcmp(wire + KVSPACE_XVALUE_HEAD_LEN, hi, 2) == 0, "buffer body");
        CHECK(value_ok(wire, wire_total(wire), 2, "[]char/utf8"), "buffer attr");
    }

    wire = kvlangXvalueNewBufferArray(store, next_key(), NULL, 0, "[]uint8");
    CHECK(wire != NULL, "empty buffer array");
    if (wire)
        CHECK(value_ok(wire, wire_total(wire), 0, "[]uint8"),
              "empty buffer attr");

    CHECK(kvlangXvalueNewBufferArray(store, next_key(), NULL, 1, "[]uint8") == NULL,
          "buffer NULL body rejected");
    /* 只收 []基础类型：非数组/未知元素类型一律拒绝。 */
    CHECK(kvlangXvalueNewBufferArray(store, next_key(), hi, sizeof hi, "int64") ==
              NULL,
          "buffer rejects non-array langtype");
    CHECK(kvlangXvalueNewBufferArray(store, next_key(), hi, sizeof hi,
                                     "[]Point") == NULL,
          "buffer rejects unknown element type");
}

static void test_tensor(void) {
    uint64_t dims[2] = {2, 3};
    uint8_t raw[24];
    memset(raw, 0xab, sizeof raw);
    uint8_t *wire = kvlangXvalueNewCompactArray(store, next_key(), dims, 2, raw,
                                                sizeof raw, "[2,3]float32");
    CHECK(wire != NULL, "compact array new");
    if (wire) {
        CHECK(wire_total(wire) == 55, "compact len");
        CHECK(value_ok(wire, wire_total(wire), sizeof raw, "[2,3]float32"),
              "compact attr");
    }
    CHECK(kvlangXvalueNewCompactArray(store, next_key(), NULL, 2, raw,
                                      sizeof raw, "") == NULL,
          "compact NULL dims rejected");
    CHECK(kvlangXvalueNewCompactArray(store, next_key(), dims, 0, raw,
                                      sizeof raw, "") == NULL,
          "compact ndim=0 rejected");
    /* 变长 char/utf8 拒绝；定宽 char/utf32、char/ascii 保留。 */
    CHECK(kvlangXvalueNewCompactArray(store, next_key(), dims, 2, raw,
                                      sizeof raw, "[2,3]char/utf8") == NULL,
          "compact rejects char/utf8 element");
    uint8_t *wide = kvlangXvalueNewCompactArray(store, next_key(), dims, 2,
                                                raw, sizeof raw, "[2,3]char/utf32");
    CHECK(wide != NULL, "compact accepts char/utf32 element");
    if (wide)
        CHECK(value_ok(wide, wire_total(wide), sizeof raw, "[2,3]char/utf32"),
              "compact char/utf32 attr");
    uint8_t *ascii = kvlangXvalueNewCompactArray(store, next_key(), dims, 2,
                                                 raw, sizeof raw, "[2,3]char/ascii");
    CHECK(ascii != NULL, "compact accepts char/ascii element");
    if (ascii)
        CHECK(value_ok(ascii, wire_total(ascii), sizeof raw, "[2,3]char/ascii"),
              "compact char/ascii attr");
    CHECK(kvlangXvalueNewCompactArray(store, next_key(), dims, 2, raw,
                                      sizeof raw, "[2,3]Point") == NULL,
          "compact rejects unknown element type");
    CHECK(kvlangXvalueNewCompactArray(store, next_key(), dims, 2, raw,
                                      sizeof raw, "float32") == NULL,
          "compact rejects missing dims");
    CHECK(kvlangXvalueNewCompactArray(store, next_key(), dims, 2, raw,
                                      sizeof raw, "[2,]float32") == NULL,
          "compact rejects empty dim");
}

static void test_ptr(void) {
    uint8_t *wire = kvlangXvalueNewPtr(store, next_key(),
                                       (const uint8_t *)"/lib/f", 6,
                                       "[]char/utf8");
    CHECK(wire != NULL, "ptr new");
    if (wire) {
        CHECK(wire_total(wire) == 37, "ptr len");
        CHECK(value_ok(wire, wire_total(wire), 6, "*[]char/utf8"), "ptr attr");
    }

    wire = kvlangXvalueNewPtr(store, next_key(), (const uint8_t *)"/x", 2, "");
    CHECK(wire != NULL, "ptr empty kindexpr");
    if (wire)
        CHECK(value_ok(wire, wire_total(wire), 2, "*"), "ptr bare star");

    CHECK(kvlangXvalueNewPtr(store, next_key(), (const uint8_t *)"/x", 2,
                             "*int64") == NULL,
          "ptr double prefix rejected");
}

static void test_code_reserve(void) {
    const uint8_t body[5] = {1, 2, 3, 4, 5};
    uint8_t *wire = kvlangXvalueNewCode(store, next_key(), body, sizeof body,
                                        "rwir");
    CHECK(wire != NULL, "code new");
    if (wire) {
        CHECK(wire_total(wire) == 16 + 5 + 7, "code len");
        CHECK(value_ok(wire, wire_total(wire), 5, "rwir"), "code attr");
    }

    wire = kvlangXvalueHeadReserve(store, next_key(), 4, "[]u8");
    CHECK(wire != NULL, "reserve new");
    if (wire) {
        CHECK(wire_total(wire) == 27, "reserve len");
        for (int i = 0; i < 4; i++)
            CHECK(wire[16 + i] == 0, "reserve body byte %d zero", i);
        CHECK(value_ok(wire, wire_total(wire), 4, "[]u8"), "reserve attr");
    }
}

static void test_padding_rejected(void) {
    uint8_t *wire = kvlangXvalueNewT(store, next_key(),
                                          (const uint8_t *)"abcd", 4, "int64");
    CHECK(wire != NULL, "padding build");
    if (!wire)
        return;
    const uint64_t n = wire_total(wire);
    const uint8_t *lt = (const uint8_t *)"sentinel";
    CHECK(kvlangXvalueAttrLangtypeBounded(wire, n, &lt) == 5 && lt != NULL,
          "padding zero accepted");
    for (int at = 10; at <= 15; at++) {
        wire[at] = 1;
        kvspaceXvalueHead h;
        memcpy(&h, wire, sizeof h);
        lt = (const uint8_t *)"sentinel";
        CHECK(kvlangXvalueAttrLangtypeBounded(wire, n, &lt) == 0 && lt == NULL,
              "nonzero padding byte %d rejected", at);
        CHECK(!kvspaceXvalueHeadPaddingIsZero(&h),
              "helper sees padding byte %d", at);
        CHECK(kvspaceXvalueHeadTotalLen(&h) == 0,
              "total len refuses padding byte %d", at);
        uint64_t eff = 0;
        CHECK(kvspaceXvalueHeadEffectiveLen(wire, n, &eff) != 0,
              "effective len refuses padding byte %d", at);
        wire[at] = 0;
    }
}

/* tag 2（旧 reallen）已删除：attr 区出现 tag 2 必须拒绝，且与位置无关。 */
static void test_tag_two_position_independent(void) {
    const uint8_t lt_data[] = "[]char/utf8";
    const uint16_t lt_len = (uint16_t)(sizeof lt_data - 1);
    const uint16_t lt_record = (uint16_t)(3 + lt_len);
    const uint16_t tag2_record = 3 + 8;

    for (int tag2_first = 0; tag2_first <= 1; tag2_first++) {
        const uint16_t attrs = (uint16_t)(lt_record + tag2_record);
        const uint64_t total = KVSPACE_XVALUE_HEAD_LEN + 3 + attrs;
        uint8_t *wire = (uint8_t *)calloc(1, (size_t)total);
        CHECK(wire != NULL, "tag2-position allocation");
        if (!wire)
            return;
        kvspaceXvalueHead h = {.padding = {0}};
        kvspaceXvalueHeadSetLen(&h, 3);
        kvspaceXvalueHeadSetAttrsLen(&h, attrs);
        memcpy(wire, &h, sizeof h);
        wire[KVSPACE_XVALUE_HEAD_LEN + 0] = 'a';
        wire[KVSPACE_XVALUE_HEAD_LEN + 1] = 'b';
        wire[KVSPACE_XVALUE_HEAD_LEN + 2] = 'c';

        uint8_t *a = wire + KVSPACE_XVALUE_HEAD_LEN + 3;
        if (tag2_first) {
            a[0] = 2; a[1] = 8; a[2] = 0;
            a += tag2_record;
        }
        a[0] = KVLANG_XVALUE_ATTR_TAG_LANGTYPE;
        a[1] = (uint8_t)lt_len;
        a[2] = 0;
        memcpy(a + 3, lt_data, lt_len);
        a += lt_record;
        if (!tag2_first) {
            a[0] = 2; a[1] = 8; a[2] = 0;
        }

        const uint8_t *data = NULL;
        uint16_t data_len = 0;
        const int rc = kvspaceXvalueGetAttr(wire, total,
                                            KVLANG_XVALUE_ATTR_TAG_LANGTYPE,
                                            &data, &data_len);
        CHECK(rc == -1, "tag 2 位于 langtype %s 时仍须拒绝（rc=%d）",
              tag2_first ? "之前" : "之后", rc);
        CHECK(data == NULL && data_len == 0, "拒绝时输出必须清零");
        free(wire);
    }
}

static void test_bounded_and_truncated(void) {
    uint8_t *wire = kvlangXvalueNewBufferArray(store, next_key(),
                                               (const uint8_t *)"abc", 3,
                                               "[]char/utf8");
    CHECK(wire != NULL, "bounded build");
    if (wire) {
        const uint64_t n = wire_total(wire);
        const uint8_t *lt = (const uint8_t *)"sentinel";
        CHECK(kvlangXvalueAttrLangtypeBounded(wire, n - 1, &lt) == 0 && lt == NULL,
              "truncated wire rejected");
        lt = (const uint8_t *)"sentinel";
        CHECK(kvlangXvalueAttrLangtypeBounded(wire, KVSPACE_XVALUE_HEAD_LEN,
                                              &lt) == 0 && lt == NULL,
              "wire cut before body/attr rejected");
    }

    uint8_t short_wire[9] = {0};
    const uint8_t *lt = (const uint8_t *)"sentinel";
    CHECK(kvlangXvalueAttrLangtypeBounded(short_wire, 9, &lt) == 0 && lt == NULL,
          "wire shorter than head rejected");
    lt = (const uint8_t *)"sentinel";
    CHECK(kvlangXvalueAttrLangtypeBounded(NULL, 16, &lt) == 0 && lt == NULL,
          "NULL wire rejected");
}

static void test_attrslen_limit(void) {
    const size_t max_langtype = UINT16_MAX - 3u;
    char *lt = (char *)malloc(max_langtype + 2u);
    CHECK(lt != NULL, "attrslen limit allocation");
    if (!lt)
        return;
    memset(lt, 'a', max_langtype + 1u);
    lt[max_langtype] = '\0';
    uint8_t *wire = kvlangXvalueNewT(store, next_key(), NULL, 0, lt);
    CHECK(wire != NULL, "attrslen == UINT16_MAX accepted");
    if (wire) {
        kvspaceXvalueHead h;
        memcpy(&h, wire, sizeof h);
        CHECK(kvspaceXvalueHeadAttrsLen(&h) == UINT16_MAX,
              "max attrslen lands in u16");
    }
    lt[max_langtype] = 'a';
    lt[max_langtype + 1u] = '\0';
    CHECK(kvlangXvalueNewT(store, next_key(), NULL, 0, lt) == NULL,
          "attrslen > UINT16_MAX rejected");
    free(lt);
}

int main(void) {
    snprintf(store_path, sizeof store_path, "shm:///tmp/kvlangxvalue-test-%ld",
             (long)getpid());
    {
        char file_path[128];
        snprintf(file_path, sizeof file_path, "/tmp/kvlangxvalue-test-%ld",
                 (long)getpid());
        unlink(file_path);
    }
    store = kvspaceConnect(store_path);
    if (!store) {
        fprintf(stderr, "cannot connect %s\n", store_path);
        return 1;
    }

    test_scalar();
    test_slack();
    test_tensor();
    test_ptr();
    test_code_reserve();
    test_padding_rejected();
    test_tag_two_position_independent();
    test_bounded_and_truncated();
    test_attrslen_limit();

    kvspaceClose(store);
    {
        char file_path[128];
        snprintf(file_path, sizeof file_path, "/tmp/kvlangxvalue-test-%ld",
                 (long)getpid());
        unlink(file_path);
    }
    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("ok");
    return 0;
}
