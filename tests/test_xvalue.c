#include <pathspace/pathspace_xvalue.h>
#include <pathlang/xvalue.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            fprintf(stderr, "  FAIL %s:%d: ", __FILE__, __LINE__);             \
            fprintf(stderr, __VA_ARGS__);                                      \
            fprintf(stderr, "\n");                                             \
        }                                                                      \
    } while (0)

static int value_ok(const uint8_t *v, uint64_t total, uint64_t body_len,
                    const char *lt) {
    size_t want = strlen(lt);
    pathspaceXvalueHead h;
    memcpy(&h, v, sizeof h);
    if (pathspaceXvalueHeadLen(&h) != body_len ||
        !pathspaceXvalueHeadPaddingIsZero(&h))
        return 0;
    if (pathspaceXvalueHeadTotalLen(&h) != total)
        return 0;
    const uint64_t alen = pathspaceXvalueHeadAttrsLen(&h);
    if (total != PATHSPACE_XVALUE_HEAD_LEN + body_len + alen)
        return 0;
    const uint8_t *ltp = NULL;
    uint16_t n = pathlangXvalueHeadLangtypeBounded(v, total, &ltp);
    if (n != want || (want && (!ltp || memcmp(ltp, lt, want) != 0)))
        return 0;
    uint64_t eff = UINT64_MAX;
    if (pathspaceXvalueHeadEffectiveLen(v, total, &eff) != 0 || eff != body_len)
        return 0;
    if (want) {
        const uint8_t *a = v + PATHSPACE_XVALUE_HEAD_LEN + body_len;
        if (a[0] != PATHLANG_XVALUE_ATTR_TAG_LANGTYPE || a[1] != want ||
            a[2] != 0)
            return 0;
    }
    return 1;
}

static void test_none(void) {
    uint8_t *buf = NULL;
    uint64_t n = 0;
    CHECK(pathlangXvalueHeadNewNone(&buf, &n) == 0, "none new");
    CHECK(n == 16, "none len %llu", (unsigned long long)n);
    pathspaceXvalueHead h;
    memcpy(&h, buf, sizeof h);
    CHECK(pathspaceXvalueHeadLen(&h) == 0, "none head");
    CHECK(buf[0] == 0 && buf[7] == 0 && buf[8] == 0 && buf[9] == 0,
          "none bodylen/attrslen zero");
    for (int at = 10; at <= 15; at++)
        CHECK(buf[at] == 0, "none padding byte %d", at);
    CHECK(value_ok(buf, n, 0, ""), "none empty attr");
    free(buf);
}

static void test_scalar(void) {
    uint8_t raw[8] = {0x2a, 0, 0, 0, 0, 0, 0, 0};
    uint8_t *buf = NULL;
    uint64_t n = 0;
    CHECK(pathlangXvalueHeadNewScalar(raw, 8, "int64", &buf, &n) == 0,
          "i64 new");
    CHECK(n == 32, "i64 len %llu", (unsigned long long)n);
    CHECK(memcmp(buf + PATHSPACE_XVALUE_HEAD_LEN, raw, 8) == 0, "i64 body@16");
    CHECK(value_ok(buf, n, 8, "int64"), "i64 langtype attr");
    free(buf);

    CHECK(pathlangXvalueHeadNewScalar(raw, 8, "", &buf, &n) == 0,
          "scalar no-langtype");
    CHECK(n == 24, "no-langtype len %llu", (unsigned long long)n);
    CHECK(value_ok(buf, n, 8, ""), "no-langtype empty attr");
    free(buf);

    CHECK(pathlangXvalueHeadNewScalar(NULL, 1, "", &buf, &n) != 0 && buf == NULL,
          "scalar NULL body rejected");
}

static void test_slack(void) {
    const uint8_t hi[] = {'h', 'i'};
    uint8_t *buf = NULL;
    uint64_t n = 0;
    CHECK(pathlangXvalueHeadNewSlack(hi, sizeof hi, "[]char/utf8", &buf, &n) ==
              0,
          "slack new");
    CHECK(n == 16 + 2 + 14, "slack len %llu", (unsigned long long)n);
    CHECK(memcmp(buf + PATHSPACE_XVALUE_HEAD_LEN, hi, 2) == 0, "slack body");
    CHECK(value_ok(buf, n, 2, "[]char/utf8"), "slack langtype attr");
    free(buf);

    CHECK(pathlangXvalueHeadNewSlack(NULL, 0, "", &buf, &n) == 0,
          "empty slack");
    CHECK(n == 16, "empty slack len %llu", (unsigned long long)n);
    CHECK(value_ok(buf, n, 0, ""), "empty slack attr");
    free(buf);

    CHECK(pathlangXvalueHeadNewSlack(NULL, 1, "", &buf, &n) != 0 && buf == NULL,
          "slack NULL body rejected");
}

static void test_tensor(void) {
    uint64_t dims[2] = {2, 3};
    uint8_t raw[24];
    uint8_t *buf = NULL;
    uint64_t n = 0;
    memset(raw, 0xab, sizeof raw);
    CHECK(pathlangXvalueHeadNewTensor(dims, 2, raw, sizeof raw,
                                     "[2,3]float32", &buf, &n) == 0,
          "tensor new");
    CHECK(n == 55, "tensor len %llu", (unsigned long long)n);
    CHECK(value_ok(buf, n, sizeof raw, "[2,3]float32"), "tensor attr");
    free(buf);
    CHECK(pathlangXvalueHeadNewTensor(NULL, 2, raw, sizeof raw, "", &buf, &n) !=
              0,
          "tensor NULL dims rejected");
    CHECK(pathlangXvalueHeadNewTensor(dims, 0, raw, sizeof raw, "", &buf, &n) !=
              0,
          "tensor ndim=0 rejected");
}

static void test_ptr_ext(void) {
    uint8_t *buf = NULL;
    uint64_t n = 0;
    CHECK(pathlangXvalueHeadNewPtr((const uint8_t *)"/lib/f", 6, "[]char/utf8",
                                  &buf, &n) == 0,
          "ptr new");
    CHECK(n == 37, "ptr len %llu", (unsigned long long)n);
    CHECK(value_ok(buf, n, 6, "*[]char/utf8"), "ptr attr");
    free(buf);

    CHECK(pathlangXvalueHeadNewPtr((const uint8_t *)"/x", 2, "", &buf, &n) == 0,
          "ptr empty kindexpr");
    CHECK(value_ok(buf, n, 2, "*"), "ptr bare star");
    free(buf);

    CHECK(pathlangXvalueHeadNewPtr((const uint8_t *)"/x", 2, "*int64", &buf,
                                  &n) != 0,
          "ptr double prefix rejected");

    CHECK(pathlangXvalueHeadNewExt((const uint8_t *)"s3://b/k", 8, "", &buf,
                                  &n) == 0,
          "ext new");
    CHECK(n == 28, "ext len %llu", (unsigned long long)n);
    CHECK(value_ok(buf, n, 8, "@"), "ext attr");
    free(buf);

    CHECK(pathlangXvalueHeadNewExt(NULL, 1, "", &buf, &n) != 0,
          "ext NULL locator rejected");
    CHECK(pathlangXvalueHeadNewExt((const uint8_t *)"x", 1, "@x", &buf, &n) != 0,
          "ext double prefix rejected");
}

static void test_code_reserve(void) {
    const uint8_t body[5] = {1, 2, 3, 4, 5};
    uint8_t *buf = NULL;
    uint64_t n = 0;
    CHECK(pathlangXvalueHeadNewCode(body, sizeof body, "rwir", &buf, &n) == 0,
          "code new");
    CHECK(n == 16 + 5 + 7, "code len %llu", (unsigned long long)n);
    CHECK(value_ok(buf, n, 5, "rwir"), "code attr");
    free(buf);

    CHECK(pathlangXvalueHeadReserve(4, "[]u8", &buf, &n) == 0, "reserve new");
    CHECK(n == 27, "reserve len %llu", (unsigned long long)n);
    for (int i = 0; i < 4; i++)
        CHECK(buf[16 + i] == 0, "reserve body byte %d zero", i);
    CHECK(value_ok(buf, n, 4, "[]u8"), "reserve attr");
    free(buf);
}

static void test_padding_rejected(void) {
    uint8_t *buf = NULL;
    uint64_t n = 0;
    CHECK(pathlangXvalueHeadNewScalar((const uint8_t *)"abcd", 4, "int64",
                                     &buf, &n) == 0,
          "padding build");
    const uint8_t *lt = (const uint8_t *)"sentinel";
    CHECK(pathlangXvalueHeadLangtypeBounded(buf, n, &lt) == 5 && lt != NULL,
          "padding zero accepted");
    for (int at = 10; at <= 15; at++) {
        buf[at] = 1;
        pathspaceXvalueHead h;
        memcpy(&h, buf, sizeof h);
        lt = (const uint8_t *)"sentinel";
        CHECK(pathlangXvalueHeadLangtypeBounded(buf, n, &lt) == 0 && lt == NULL,
              "nonzero padding byte %d rejected", at);
        CHECK(!pathspaceXvalueHeadPaddingIsZero(&h),
              "helper sees padding byte %d", at);
        CHECK(pathspaceXvalueHeadTotalLen(&h) == 0,
              "total len refuses padding byte %d", at);
        uint64_t eff = 0;
        CHECK(pathspaceXvalueHeadEffectiveLen(buf, n, &eff) != 0,
              "effective len refuses padding byte %d", at);
        buf[at] = 0;
    }
    free(buf);
}

static void test_tag_two_rejected(void) {
    const uint8_t body[] = {'a', 'b', 'c'};
    uint8_t *base = NULL;
    uint64_t base_len = 0;
    CHECK(pathlangXvalueHeadNewSlack(body, sizeof body, "[]char/utf8", &base,
                                    &base_len) == 0,
          "tag2 base build");
    const uint64_t extra = 11;
    const uint64_t total = base_len + extra;
    uint8_t *wire = (uint8_t *)calloc(1, (size_t)total);
    CHECK(wire != NULL, "tag2 allocation");
    if (!wire) {
        free(base);
        return;
    }
    memcpy(wire, base, (size_t)base_len);
    pathspaceXvalueHead h;
    memcpy(&h, wire, sizeof h);
    pathspaceXvalueHeadSetAttrsLen(&h,
                                 (uint16_t)(pathspaceXvalueHeadAttrsLen(&h) + extra));
    memcpy(wire, &h, sizeof h);
    
    uint8_t *a = wire + PATHSPACE_XVALUE_HEAD_LEN + pathspaceXvalueHeadLen(&h);
    a[0] = 2;
    a[1] = 8;
    a[2] = 0;
    uint64_t reallen = 2;
    for (int i = 0; i < 8; i++)
        a[3 + i] = (uint8_t)(reallen >> (8 * i));

    const uint8_t *lt = (const uint8_t *)"sentinel";
    CHECK(pathlangXvalueHeadLangtypeBounded(wire, total, &lt) == 0 && lt == NULL,
          "tag 2 rejected by scan");
    uint64_t eff = UINT64_MAX;
    CHECK(pathspaceXvalueHeadEffectiveLen(wire, total, &eff) != 0,
          "tag 2 rejected by effective len");
    free(wire);
    free(base);
}

static void test_tag_two_position_independent(void) {
    
    const uint8_t lt_data[] = "[]char/utf8";
    const uint16_t lt_len = (uint16_t)(sizeof lt_data - 1);
    const uint16_t lt_record = (uint16_t)(3 + lt_len);
    const uint16_t tag2_record = 3 + 8;

    for (int tag2_first = 0; tag2_first <= 1; tag2_first++) {
        const uint16_t attrs = (uint16_t)(lt_record + tag2_record);
        const uint64_t total = PATHSPACE_XVALUE_HEAD_LEN + 3 + attrs;
        uint8_t *wire = (uint8_t *)calloc(1, (size_t)total);
        CHECK(wire != NULL, "tag2-position allocation");
        if (!wire)
            return;
        pathspaceXvalueHead h = {.padding = {0}};
        pathspaceXvalueHeadSetLen(&h, 3);
        pathspaceXvalueHeadSetAttrsLen(&h, attrs);
        memcpy(wire, &h, sizeof h);
        wire[PATHSPACE_XVALUE_HEAD_LEN + 0] = 'a';
        wire[PATHSPACE_XVALUE_HEAD_LEN + 1] = 'b';
        wire[PATHSPACE_XVALUE_HEAD_LEN + 2] = 'c';

        uint8_t *a = wire + PATHSPACE_XVALUE_HEAD_LEN + 3;
        if (tag2_first) {
            a[0] = 2; a[1] = 8; a[2] = 0;
            a += tag2_record;
        }
        a[0] = PATHLANG_XVALUE_ATTR_TAG_LANGTYPE;
        a[1] = (uint8_t)lt_len;
        a[2] = 0;
        memcpy(a + 3, lt_data, lt_len);
        a += lt_record;
        if (!tag2_first) {
            a[0] = 2; a[1] = 8; a[2] = 0;
        }

        const uint8_t *data = NULL;
        uint16_t data_len = 0;
        const int rc = pathspaceXvalueGetAttr(wire, total,
                                            PATHLANG_XVALUE_ATTR_TAG_LANGTYPE,
                                            &data, &data_len);
        CHECK(rc == -1,
              "tag 2 placed %s langtype must still be rejected (rc=%d)",
              tag2_first ? "before" : "after", rc);
        CHECK(data == NULL && data_len == 0,
              "outputs must be cleared on rejection (tag 2 %s langtype)",
              tag2_first ? "before" : "after");
        free(wire);
    }
}

static void test_bounded_and_truncated(void) {
    uint8_t *buf = NULL;
    uint64_t n = 0;
    CHECK(pathlangXvalueHeadNewSlack((const uint8_t *)"abc", 3, "[]char/utf8",
                                    &buf, &n) == 0,
          "bounded build");
    const uint8_t *lt = (const uint8_t *)"sentinel";
    CHECK(pathlangXvalueHeadLangtypeBounded(buf, n - 1, &lt) == 0 && lt == NULL,
          "truncated wire rejected");
    lt = (const uint8_t *)"sentinel";
    CHECK(pathlangXvalueHeadLangtypeBounded(buf, PATHSPACE_XVALUE_HEAD_LEN, &lt) ==
                  0 &&
              lt == NULL,
          "wire cut before body/attr rejected");
    free(buf);

    uint8_t short_wire[9] = {0};
    lt = (const uint8_t *)"sentinel";
    CHECK(pathlangXvalueHeadLangtypeBounded(short_wire, 9, &lt) == 0 &&
              lt == NULL,
          "wire shorter than head rejected");
    lt = (const uint8_t *)"sentinel";
    CHECK(pathlangXvalueHeadLangtypeBounded(NULL, 16, &lt) == 0 && lt == NULL,
          "NULL wire rejected");
}

static void test_attrslen_limit(void) {
    const size_t max_langtype = UINT16_MAX - 3u;
    const size_t too_long_langtype = max_langtype + 1u;
    char *lt = (char *)malloc(too_long_langtype + 1u);
    CHECK(lt != NULL, "attrslen limit allocation");
    if (!lt)
        return;
    memset(lt, 'a', too_long_langtype);
    lt[max_langtype] = '\0';
    uint8_t *buf = NULL;
    uint64_t n = 0;
    CHECK(pathlangXvalueHeadNewScalar(NULL, 0, lt, &buf, &n) == 0,
          "attrslen == UINT16_MAX accepted");
    if (buf) {
        pathspaceXvalueHead h;
        memcpy(&h, buf, sizeof h);
        CHECK(pathspaceXvalueHeadAttrsLen(&h) == UINT16_MAX,
              "max attrslen lands in u16");
        free(buf);
    }
    lt[max_langtype] = 'a';
    lt[too_long_langtype] = '\0';
    buf = NULL;
    n = 0;
    CHECK(pathlangXvalueHeadNewScalar(NULL, 0, lt, &buf, &n) != 0 && buf == NULL,
          "attrslen > UINT16_MAX rejected");
    free(lt);
}

int main(void) {
    test_none();
    test_scalar();
    test_slack();
    test_tensor();
    test_ptr_ext();
    test_code_reserve();
    test_padding_rejected();
    test_tag_two_rejected();
    test_tag_two_position_independent();
    test_bounded_and_truncated();
    test_attrslen_limit();
    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("ok");
    return 0;
}
