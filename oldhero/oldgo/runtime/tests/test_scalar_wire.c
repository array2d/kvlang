/* 标量栈写 wire vs 正典 codec 的逐字节一致性。
 * 覆盖 runtime 标量快路径用到的全部 kind：int8/16/32/64、uint8/16/32/64、
 * float32/float64、bool。新增 kind 必须在此登记，防止第二份 head 写静默漂移。 */

#include "runtime_internal.h"
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

/* raw 低位字节为 0x2a，其余 0；与 kvspaceXvalueHeadNewScalar 逐字节比对。 */
static void check_kind(const char *kind, uint32_t len) {
    uint8_t raw[8] = {0x2a, 0, 0, 0, 0, 0, 0, 0};
    uint8_t buf[64];
    uint32_t bl = kvlangScalarToWire(kind, raw, len, buf, sizeof buf);
    uint8_t *ref = NULL;
    uint64_t rl64 = 0;
    CHECK(kvspaceXvalueHeadNewScalar(KVSPACE_XVALUE_ST_SHORT, raw, len, kind,
                                     &ref, &rl64) == 0);
    CHECK(rl64 <= UINT32_MAX);
    uint32_t rl = (uint32_t)rl64;
    CHECK(bl == rl);
    CHECK(bl > 0 && memcmp(buf, ref, bl) == 0);
    free(ref);
}

/* NextPc 写出的槽：解出的 b 即槽容量，再按该容量与正典 slack codec 逐字节比对。
 * 不写死实现档位——容量是实现自选的，布局与内容才是契约。 */
static void check_member(kvspace_t *kv, bool is_status, const char *s) {
    char key[KV_KEY_MAX];
    if (is_status)
        kvlangKeytreeVthreadStatus("slk", key, sizeof key);
    else
        kvlangKeytreeVthreadPc("slk", key, sizeof key);
    kvspaceKeyOff ko;
    kvspaceKeyOffBind(&ko, kv, key);
    kvlangFrame_t f = {.kvspace = kv, .vthread_id = "slk"};
    if (is_status) {
        f.status_off = &ko;
        f.status_known = NULL;
        kvlangVthreadNextPc(&f, "/vthread/slk/‥pc", s);
    } else {
        f.pc_off = &ko;
        kvlangVthreadNextPc(&f, s, "running");
    }
    CHECK(!f.persist_failed);
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    CHECK(kvlangKvspaceGetOne(kv, key, &v) == 0);
    CHECK(v.length >= KVSPACE_XVALUE_HEAD_LEN);
    kvspaceXvalueHead xh;
    memcpy(&xh, v.bytes, sizeof xh);
    CHECK(xh.storetype == KVSPACE_XVALUE_ST_SLACK);
    uint32_t blen = (uint32_t)kvspaceXvalueHeadLen(&xh);
    CHECK(blen == (uint32_t)strlen(s));
    uint8_t *ref = NULL;
    uint64_t rl = 0;
    /* pc/status 是系统键：空 langtype（空 attr）。 */
    CHECK(kvspaceXvalueHeadNewSlack((const uint8_t *)s, strlen(s), strlen(s),
                                    "", &ref, &rl) == 0);
    CHECK(v.length == (uint32_t)rl);
    /* 等价值写（part-write）只保证 [0, 8+a) 是对的；旧字节不属于值
     * （spec 05/02：值语义只取前 a 字节）。整值写时两者自然相同。 */
    size_t prefix = KVSPACE_XVALUE_HEAD_LEN + (size_t)blen;
    CHECK(prefix <= (size_t)rl && prefix <= (size_t)v.length);
    CHECK(prefix > 0 && memcmp(v.bytes, ref, prefix) == 0);
    free(ref);
}

static void check_slack(kvspace_t *kv, const char *s) {
    check_member(kv, false, s);
}

int main(void) {
    check_kind(KVSPACE_KIND_INT8, 1);
    check_kind(KVSPACE_KIND_INT16, 2);
    check_kind(KVSPACE_KIND_INT32, 4);
    check_kind(KVSPACE_KIND_INT64, 8);
    check_kind(KVSPACE_KIND_UINT8, 1);
    check_kind(KVSPACE_KIND_UINT16, 2);
    check_kind(KVSPACE_KIND_UINT32, 4);
    check_kind(KVSPACE_KIND_UINT64, 8);
    check_kind(KVSPACE_KIND_FLOAT32, 4);
    check_kind(KVSPACE_KIND_FLOAT64, 8);

    /* int64/float64/bool 与 kvspaceNew* 入口再比一次（更贴近实际写入路径）。 */
    const int64_t ints[] = {0, 1, -1, 42, INT64_MAX, INT64_MIN};
    for (size_t i = 0; i < sizeof ints / sizeof ints[0]; i++) {
        uint8_t raw[8], buf[64];
        for (int j = 0; j < 8; j++)
            raw[j] = (uint8_t)((uint64_t)ints[i] >> (j * 8));
        uint32_t bl =
            kvlangScalarToWire(KVSPACE_KIND_INT64, raw, 8, buf, sizeof buf);
        uint8_t *ref = NULL;
        uint32_t rl = 0;
        CHECK(kvspaceNewInt64(ints[i], KVSPACE_KIND_INT64, &ref, &rl) == 0);
        CHECK(bl == rl && bl > 0 && memcmp(buf, ref, bl) == 0);
        free(ref);
    }
    const double floats[] = {0.0, 1.5, -2.25};
    for (size_t i = 0; i < sizeof floats / sizeof floats[0]; i++) {
        uint8_t raw[8], buf[64];
        uint64_t u;
        memcpy(&u, &floats[i], 8);
        for (int j = 0; j < 8; j++)
            raw[j] = (uint8_t)(u >> (j * 8));
        uint32_t bl =
            kvlangScalarToWire(KVSPACE_KIND_FLOAT64, raw, 8, buf, sizeof buf);
        uint8_t *ref = NULL;
        uint32_t rl = 0;
        CHECK(kvspaceNewFloat64(floats[i], KVSPACE_KIND_FLOAT64, &ref, &rl) ==
              0);
        CHECK(bl == rl && bl > 0 && memcmp(buf, ref, bl) == 0);
        free(ref);
    }
    for (int b = 0; b <= 1; b++) {
        uint8_t raw = (uint8_t)b, buf[64];
        uint32_t bl =
            kvlangScalarToWire(KVSPACE_KIND_BOOL, &raw, 1, buf, sizeof buf);
        uint8_t *ref = NULL;
        uint32_t rl = 0;
        CHECK(kvspaceNewBool(raw, KVSPACE_KIND_BOOL, &ref, &rl) == 0);
        CHECK(bl == rl && bl > 0 && memcmp(buf, ref, bl) == 0);
        free(ref);
    }
    /* pc/status 的变长 char：固定 cap（含多字节 UTF-8）逐字节对齐正典。 */
    char dir[] = "/tmp/kvlang-slack-XXXXXX";
    if (!mkdtemp(dir)) {
        fprintf(stderr, "mkdtemp failed\n");
        return 1;
    }
    const char *scheme = getenv("KVLANG_TEST_SCHEME");
    char dsn[256];
    snprintf(dsn, sizeof dsn, "%s://%s/s", scheme ? scheme : "shm", dir);
    kvspace_t *kv = kvlangKvspaceConnect(dsn);
    CHECK(kv != NULL);
    const char *texts[] = {"", "init", "/vthread/v1/lib/main/[3,0]",
                           "/lib/名字/[7,1]", "running"};
    for (size_t i = 0; i < sizeof texts / sizeof texts[0]; i++)
        check_slack(kv, texts[i]);
    /* 超过 pc 常态档 → 全尺寸槽（不截断）；status 单独一档。 */
    char long_pc[301], long_status[41], max_pc[KV_KEY_MAX];
    memset(long_pc, 'a', 300);
    long_pc[300] = 0;
    memset(long_status, 'b', 40);
    long_status[40] = 0;
    memset(max_pc, 'c', KV_KEY_MAX - 1);
    max_pc[KV_KEY_MAX - 1] = 0;
    check_slack(kv, long_pc);
    check_slack(kv, max_pc);
    check_member(kv, true, "running");
    check_member(kv, true, "done");
    check_member(kv, true, long_status);
    if (kv)
        kvlangKvspaceDisconnect(kv);
    if (failures) {
        fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
