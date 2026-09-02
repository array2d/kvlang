// builtin_random —— random·* 随机数 rwir

#include "builtin_internal.h"

static uint64_t crypto_rand_u64(void) {
    uint64_t v = 0;
    FILE *fp = fopen("/dev/urandom", "rb");
    if (fp) { fread(&v, 8, 1, fp); fclose(fp); }
    return v;
}

int kvlangBuiltinRandUint64(kvlangFrame_t *f) {
    uint64_t v = crypto_rand_u64();
    uint8_t r[8]; memcpy(r, &v, 8);
    kvlangXvalue_t e; kvlangXvalueNewTlv(&e, KVSPACE_KIND_UINT64, r, 8, 1);
    int rc = kvlangBuiltinWriteResult(f, &e); kvlangXvalueFree(&e);
    return rc;
}

int kvlangBuiltinRandInt63(kvlangFrame_t *f) {
    int64_t v = (int64_t)(crypto_rand_u64() >> 1);
    kvlangXvalue_t e; kvlangXvalueNewInt64(&e, v);
    int rc = kvlangBuiltinWriteResult(f, &e); kvlangXvalueFree(&e);
    return rc;
}

int kvlangBuiltinRandIntn(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    if (n < 1 || kvlangXvalueNone(&in[0])) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "TypeError: random.intn requires 1 int64 arg"); }
    uint64_t m = (uint64_t)kvlangXvalueAsInt64(&in[0]);
    uint64_t v = m == 0 ? 0 : crypto_rand_u64() % m;
    uint8_t r[8]; memcpy(r, &v, 8);
    kvlangXvalue_t e; kvlangXvalueNewTlv(&e, KVSPACE_KIND_UINT64, r, 8, 1);
    int rc = kvlangBuiltinWriteResult(f, &e); kvlangXvalueFree(&e); kvlangBuiltinFreeInputs(in, n);
    return rc;
}
