#include <kvspace/kvspace_xvalue.h>
#include <kvlang/xvalue.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 固定 head 16 字节 [bodylen:u64@0][attrslen:u16@8][padding:6@10-15]，
   body 偏移恒 16，attr 区紧随 body 且**无**自带长度前缀（长度只在 head 里）。
   单 key 整体——无 attr key。head 不含 storetype/ptr/layout/reserved；
   bodylen 恒为有效长度，容量是存储后端的物理分配属性。tag 2（旧 reallen）
   已删除，出现即拒绝。 */

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

/* 校验 head、body、langtype、有效长度。lt 是构造后的完整 langtype（含 * / @）。 */
static int value_ok(const uint8_t *v, uint64_t total, uint64_t body_len,
                    const char *lt) {
    size_t want = strlen(lt);
    kvspaceXvalueHead h;
    memcpy(&h, v, sizeof h);
    if (kvspaceXvalueHeadLen(&h) != body_len ||
        !kvspaceXvalueHeadPaddingIsZero(&h))
        return 0;
    if (kvspaceXvalueHeadTotalLen(&h) != total)
        return 0;
    const uint64_t alen = kvspaceXvalueHeadAttrsLen(&h);
    if (total != KVSPACE_XVALUE_HEAD_LEN + body_len + alen)
        return 0;
    const uint8_t *ltp = NULL;
    uint16_t n = kvlangXvalueHeadLangtypeBounded(v, total, &ltp);
    if (n != want || (want && (!ltp || memcmp(ltp, lt, want) != 0)))
        return 0;
    uint64_t eff = UINT64_MAX;
    if (kvspaceXvalueHeadEffectiveLen(v, total, &eff) != 0 || eff != body_len)
        return 0;
    if (want) {
        const uint8_t *a = v + KVSPACE_XVALUE_HEAD_LEN + body_len;
        if (a[0] != KVLANG_XVALUE_ATTR_TAG_LANGTYPE || a[1] != want ||
            a[2] != 0)
            return 0;
    }
    return 1;
}

static void test_none(void) {
    uint8_t *buf = NULL;
    uint64_t n = 0;
    CHECK(kvlangXvalueHeadNewNone(&buf, &n) == 0, "none new");
    CHECK(n == 16, "none len %llu", (unsigned long long)n);
    kvspaceXvalueHead h;
    memcpy(&h, buf, sizeof h);
    CHECK(kvspaceXvalueHeadLen(&h) == 0, "none head");
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
    CHECK(kvlangXvalueHeadNewScalar(raw, 8, "int64", &buf, &n) == 0,
          "i64 new");
    CHECK(n == 32, "i64 len %llu", (unsigned long long)n);
    CHECK(memcmp(buf + KVSPACE_XVALUE_HEAD_LEN, raw, 8) == 0, "i64 body@16");
    CHECK(value_ok(buf, n, 8, "int64"), "i64 langtype attr");
    free(buf);

    CHECK(kvlangXvalueHeadNewScalar(raw, 8, "", &buf, &n) == 0,
          "scalar no-langtype");
    CHECK(n == 24, "no-langtype len %llu", (unsigned long long)n);
    CHECK(value_ok(buf, n, 8, ""), "no-langtype empty attr");
    free(buf);

    CHECK(kvlangXvalueHeadNewScalar(NULL, 1, "", &buf, &n) != 0 && buf == NULL,
          "scalar NULL body rejected");
}

static void test_slack(void) {
    const uint8_t hi[] = {'h', 'i'};
    uint8_t *buf = NULL;
    uint64_t n = 0;
    CHECK(kvlangXvalueHeadNewSlack(hi, sizeof hi, "[]char/utf8", &buf, &n) ==
              0,
          "slack new");
    CHECK(n == 16 + 2 + 14, "slack len %llu", (unsigned long long)n);
    CHECK(memcmp(buf + KVSPACE_XVALUE_HEAD_LEN, hi, 2) == 0, "slack body");
    CHECK(value_ok(buf, n, 2, "[]char/utf8"), "slack langtype attr");
    free(buf);

    CHECK(kvlangXvalueHeadNewSlack(NULL, 0, "", &buf, &n) == 0,
          "empty slack");
    CHECK(n == 16, "empty slack len %llu", (unsigned long long)n);
    CHECK(value_ok(buf, n, 0, ""), "empty slack attr");
    free(buf);

    CHECK(kvlangXvalueHeadNewSlack(NULL, 1, "", &buf, &n) != 0 && buf == NULL,
          "slack NULL body rejected");
}

static void test_tensor(void) {
    uint64_t dims[2] = {2, 3};
    uint8_t raw[24];
    uint8_t *buf = NULL;
    uint64_t n = 0;
    memset(raw, 0xab, sizeof raw);
    CHECK(kvlangXvalueHeadNewTensor(dims, 2, raw, sizeof raw,
                                     "[2,3]float32", &buf, &n) == 0,
          "tensor new");
    CHECK(n == 55, "tensor len %llu", (unsigned long long)n);
    CHECK(value_ok(buf, n, sizeof raw, "[2,3]float32"), "tensor attr");
    free(buf);
    CHECK(kvlangXvalueHeadNewTensor(NULL, 2, raw, sizeof raw, "", &buf, &n) !=
              0,
          "tensor NULL dims rejected");
    CHECK(kvlangXvalueHeadNewTensor(dims, 0, raw, sizeof raw, "", &buf, &n) !=
              0,
          "tensor ndim=0 rejected");
}

static void test_ptr_ext(void) {
    uint8_t *buf = NULL;
    uint64_t n = 0;
    CHECK(kvlangXvalueHeadNewPtr((const uint8_t *)"/lib/f", 6, "[]char/utf8",
                                  &buf, &n) == 0,
          "ptr new");
    CHECK(n == 37, "ptr len %llu", (unsigned long long)n);
    CHECK(value_ok(buf, n, 6, "*[]char/utf8"), "ptr attr");
    free(buf);

    CHECK(kvlangXvalueHeadNewPtr((const uint8_t *)"/x", 2, "", &buf, &n) == 0,
          "ptr empty kindexpr");
    CHECK(value_ok(buf, n, 2, "*"), "ptr bare star");
    free(buf);

    CHECK(kvlangXvalueHeadNewPtr((const uint8_t *)"/x", 2, "*int64", &buf,
                                  &n) != 0,
          "ptr double prefix rejected");

    CHECK(kvlangXvalueHeadNewExt((const uint8_t *)"s3://b/k", 8, "", &buf,
                                  &n) == 0,
          "ext new");
    CHECK(n == 28, "ext len %llu", (unsigned long long)n);
    CHECK(value_ok(buf, n, 8, "@"), "ext attr");
    free(buf);

    CHECK(kvlangXvalueHeadNewExt(NULL, 1, "", &buf, &n) != 0,
          "ext NULL locator rejected");
    CHECK(kvlangXvalueHeadNewExt((const uint8_t *)"x", 1, "@x", &buf, &n) != 0,
          "ext double prefix rejected");
}

static void test_code_reserve(void) {
    const uint8_t body[5] = {1, 2, 3, 4, 5};
    uint8_t *buf = NULL;
    uint64_t n = 0;
    CHECK(kvlangXvalueHeadNewCode(body, sizeof body, "rwir", &buf, &n) == 0,
          "code new");
    CHECK(n == 16 + 5 + 7, "code len %llu", (unsigned long long)n);
    CHECK(value_ok(buf, n, 5, "rwir"), "code attr");
    free(buf);

    CHECK(kvlangXvalueHeadReserve(4, "[]u8", &buf, &n) == 0, "reserve new");
    CHECK(n == 27, "reserve len %llu", (unsigned long long)n);
    for (int i = 0; i < 4; i++)
        CHECK(buf[16 + i] == 0, "reserve body byte %d zero", i);
    CHECK(value_ok(buf, n, 4, "[]u8"), "reserve attr");
    free(buf);
}

static void test_padding_rejected(void) {
    uint8_t *buf = NULL;
    uint64_t n = 0;
    CHECK(kvlangXvalueHeadNewScalar((const uint8_t *)"abcd", 4, "int64",
                                     &buf, &n) == 0,
          "padding build");
    const uint8_t *lt = (const uint8_t *)"sentinel";
    CHECK(kvlangXvalueHeadLangtypeBounded(buf, n, &lt) == 5 && lt != NULL,
          "padding zero accepted");
    for (int at = 10; at <= 15; at++) {
        buf[at] = 1;
        kvspaceXvalueHead h;
        memcpy(&h, buf, sizeof h);
        lt = (const uint8_t *)"sentinel";
        CHECK(kvlangXvalueHeadLangtypeBounded(buf, n, &lt) == 0 && lt == NULL,
              "nonzero padding byte %d rejected", at);
        CHECK(!kvspaceXvalueHeadPaddingIsZero(&h),
              "helper sees padding byte %d", at);
        CHECK(kvspaceXvalueHeadTotalLen(&h) == 0,
              "total len refuses padding byte %d", at);
        uint64_t eff = 0;
        CHECK(kvspaceXvalueHeadEffectiveLen(buf, n, &eff) != 0,
              "effective len refuses padding byte %d", at);
        buf[at] = 0;
    }
    free(buf);
}

/* tag 2（旧 reallen）已删除：attr 区出现 tag 2 必须拒绝。 */
static void test_tag_two_rejected(void) {
    const uint8_t body[] = {'a', 'b', 'c'};
    uint8_t *base = NULL;
    uint64_t base_len = 0;
    CHECK(kvlangXvalueHeadNewSlack(body, sizeof body, "[]char/utf8", &base,
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
    kvspaceXvalueHead h;
    memcpy(&h, wire, sizeof h);
    kvspaceXvalueHeadSetAttrsLen(&h,
                                 (uint16_t)(kvspaceXvalueHeadAttrsLen(&h) + extra));
    memcpy(wire, &h, sizeof h);
    /* attr 区起点是 16 + bodylen，不是整条 wire 的长度 base_len。早先这里写成
     * `wire + base_len`，把 tag 2 记录写到了 wire 末尾之外的位置——旧 Scan 的
     * 循环条件宽松，恰好没读到那段，测试于是「通过」了但什么也没验到。 */
    uint8_t *a = wire + KVSPACE_XVALUE_HEAD_LEN + kvspaceXvalueHeadLen(&h);
    a[0] = 2;
    a[1] = 8;
    a[2] = 0;
    uint64_t reallen = 2;
    for (int i = 0; i < 8; i++)
        a[3 + i] = (uint8_t)(reallen >> (8 * i));

    const uint8_t *lt = (const uint8_t *)"sentinel";
    CHECK(kvlangXvalueHeadLangtypeBounded(wire, total, &lt) == 0 && lt == NULL,
          "tag 2 rejected by scan");
    uint64_t eff = UINT64_MAX;
    CHECK(kvspaceXvalueHeadEffectiveLen(wire, total, &eff) != 0,
          "tag 2 rejected by effective len");
    free(wire);
    free(base);
}

/* tag 2 的位置无关：它在目标 tag **之前**或**之后**都必须让 GetAttr 返回 -1。
 *
 * 这条专门钉住一个真实缺陷：早先 GetAttr 命中目标 tag 后就 return，于是
 * 「tag 1 在前、tag 2 在后」的 wire 被当成合法读出来——tag 2 是退役编号，
 * 见到它就说明写读双方理解不一致，整条 wire 都不能采信，与它在哪无关。
 *
 * 只测一个位置不够：缺陷恰恰只在「tag 2 排在目标之后」时出现。 */
static void test_tag_two_position_independent(void) {
    /* 手工构造 wire：body 3 字节 + 两条 attr 记录，顺序可调。 */
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
        CHECK(rc == -1,
              "tag 2 位于 langtype %s 时仍须拒绝（rc=%d）",
              tag2_first ? "之前" : "之后", rc);
        CHECK(data == NULL && data_len == 0,
              "拒绝时输出必须清零（tag2 在 %s）",
              tag2_first ? "之前" : "之后");
        free(wire);
    }
}

static void test_bounded_and_truncated(void) {
    uint8_t *buf = NULL;
    uint64_t n = 0;
    CHECK(kvlangXvalueHeadNewSlack((const uint8_t *)"abc", 3, "[]char/utf8",
                                    &buf, &n) == 0,
          "bounded build");
    const uint8_t *lt = (const uint8_t *)"sentinel";
    CHECK(kvlangXvalueHeadLangtypeBounded(buf, n - 1, &lt) == 0 && lt == NULL,
          "truncated wire rejected");
    lt = (const uint8_t *)"sentinel";
    CHECK(kvlangXvalueHeadLangtypeBounded(buf, KVSPACE_XVALUE_HEAD_LEN, &lt) ==
                  0 &&
              lt == NULL,
          "wire cut before body/attr rejected");
    free(buf);

    uint8_t short_wire[9] = {0};
    lt = (const uint8_t *)"sentinel";
    CHECK(kvlangXvalueHeadLangtypeBounded(short_wire, 9, &lt) == 0 &&
              lt == NULL,
          "wire shorter than head rejected");
    lt = (const uint8_t *)"sentinel";
    CHECK(kvlangXvalueHeadLangtypeBounded(NULL, 16, &lt) == 0 && lt == NULL,
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
    CHECK(kvlangXvalueHeadNewScalar(NULL, 0, lt, &buf, &n) == 0,
          "attrslen == UINT16_MAX accepted");
    if (buf) {
        kvspaceXvalueHead h;
        memcpy(&h, buf, sizeof h);
        CHECK(kvspaceXvalueHeadAttrsLen(&h) == UINT16_MAX,
              "max attrslen lands in u16");
        free(buf);
    }
    lt[max_langtype] = 'a';
    lt[too_long_langtype] = '\0';
    buf = NULL;
    n = 0;
    CHECK(kvlangXvalueHeadNewScalar(NULL, 0, lt, &buf, &n) != 0 && buf == NULL,
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
