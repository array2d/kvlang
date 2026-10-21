/* 标量栈编码 vs 正典 codec 的逐字节一致性。
 * 覆盖 runtime 标量快路径用到的全部 kind：int8/16/32/64、uint8/16/32/64、
 * float32/float64、bool。新增 kind 必须在此登记，防止第二份 head 编码静默漂移。 */

#include "runtime_internal.h"
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(c)                                                               \
    do {                                                                       \
        if (!(c)) {                                                            \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);       \
        }                                                                      \
    } while (0)

/* raw 低位字节为 0x2a，其余 0；与 kvspaceTlvEncode(ndim=0) 逐字节比对。 */
static void check_kind(const char *kind, uint32_t len) {
    uint8_t raw[8] = {0x2a, 0, 0, 0, 0, 0, 0, 0};
    uint8_t buf[64];
    uint32_t bl = kvlangScalarEncode(kind, raw, len, buf, sizeof buf);
    uint8_t *ref = NULL;
    uint32_t rl = 0;
    int32_t dims[1] = {0};
    CHECK(kvspaceTlvEncode(kind, raw, len, dims, 0, &ref, &rl) == 0);
    CHECK(bl == rl);
    CHECK(bl > 0 && memcmp(buf, ref, bl) == 0);
    free(ref);
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
            kvlangScalarEncode(KVSPACE_KIND_INT64, raw, 8, buf, sizeof buf);
        uint8_t *ref = NULL;
        uint32_t rl = 0;
        CHECK(kvspaceNewInt64(ints[i], &ref, &rl) == 0);
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
            kvlangScalarEncode(KVSPACE_KIND_FLOAT64, raw, 8, buf, sizeof buf);
        uint8_t *ref = NULL;
        uint32_t rl = 0;
        CHECK(kvspaceNewFloat64(floats[i], &ref, &rl) == 0);
        CHECK(bl == rl && bl > 0 && memcmp(buf, ref, bl) == 0);
        free(ref);
    }
    for (int b = 0; b <= 1; b++) {
        uint8_t raw = (uint8_t)b, buf[64];
        uint32_t bl =
            kvlangScalarEncode(KVSPACE_KIND_BOOL, &raw, 1, buf, sizeof buf);
        uint8_t *ref = NULL;
        uint32_t rl = 0;
        CHECK(kvspaceNewBool(raw, &ref, &rl) == 0);
        CHECK(bl == rl && bl > 0 && memcmp(buf, ref, bl) == 0);
        free(ref);
    }
    if (failures) {
        fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
