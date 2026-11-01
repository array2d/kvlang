/* Unit tests for `intops.h` integer semantics: two's-complement wraparound, `INT64_MIN / -1`
 * and `INT64_MIN % -1` without SIGFPE, truncating division, unsigned `/ % >>`, and `& 63` shifts. */
#include "intops.h"
#include <stdio.h>

static int failures;

#define CHECK_EQ(got, want)                                                    \
    do {                                                                       \
        int64_t g_ = (got), w_ = (want);                                       \
        if (g_ != w_) {                                                        \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: %s = %lld, want %lld\n", __FILE__,    \
                    __LINE__, #got, (long long)g_, (long long)w_);             \
        }                                                                      \
    } while (0)
#define CHECK_U(got, want) CHECK_EQ((got), (int64_t)(uint64_t)(want))

int main(void) {
    const int64_t MIN = INT64_MIN, MAX = INT64_MAX;
    /* Wraparound */
    CHECK_EQ(kvi_add(MAX, 1), MIN);
    CHECK_EQ(kvi_sub(MIN, 1), MAX);
    CHECK_EQ(kvi_mul(MIN, -1), MIN);
    CHECK_EQ(kvi_mul(MAX, 2), -2);
    CHECK_EQ(kvi_neg(MIN), MIN);
    CHECK_EQ(kvi_abs(MIN), MIN);
    CHECK_EQ(kvi_abs(-5), 5);
    /* Signed div/mod */
    CHECK_EQ(kvi_div(MIN, -1, false), MIN);
    CHECK_EQ(kvi_mod(MIN, -1, false), 0);
    CHECK_EQ(kvi_div(-7, 2, false), -3);
    CHECK_EQ(kvi_mod(-7, 2, false), -1);
    CHECK_EQ(kvi_div(7, -1, false), -7);
    CHECK_EQ(kvi_mod(7, -1, false), 0);
    CHECK_EQ(kvi_div(MIN, 2, false), MIN / 2);
    /* Unsigned div/mod (`u64max` bit pattern = -1) */
    CHECK_U(kvi_div(-1, 2, true), 9223372036854775807ull);
    CHECK_U(kvi_mod(-1, 10, true), 5);
    CHECK_U(kvi_div(-1, -1, true), 1);
    CHECK_U(kvi_mod(MIN, -1, true), (uint64_t)MIN);
    CHECK_U(kvi_mul(-1, 3), 18446744073709551613ull);
    CHECK_U(kvi_sub(3, 4), 18446744073709551615ull);
    /* Shifts (`& 63`, logical vs arithmetic right shift) */
    CHECK_U(kvi_shr(-1, 60, true), 15);
    CHECK_EQ(kvi_shr(-1, 60, false), -1);
    CHECK_EQ(kvi_shr(-16, 2, false), -4);
    CHECK_EQ(kvi_shr(MIN, 63, false), -1);
    CHECK_EQ(kvi_shr(16, 2, false), 4);
    CHECK_EQ(kvi_shr(-16, 0, false), -16);
    CHECK_U(kvi_shl(1, 63), 9223372036854775808ull);
    CHECK_EQ(kvi_shl(-1, 1), -2);
    CHECK_EQ(kvi_shl(1, 64), 1);  /* 64 & 63 = 0 */
    CHECK_EQ(kvi_shl(1, 65), 2);  /* 65 & 63 = 1 */
    CHECK_EQ(kvi_shl(3, -1), MIN); /* -1 & 63 = 63 */
    CHECK_EQ(kvi_shr(MIN, 64, false), MIN);
    CHECK_U(kvi_shr(MIN, 127, true), 1);
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("int_ops: ok\n");
    return 0;
}
