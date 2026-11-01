#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Canonical integer arithmetic semantics shared by rwir_special.c, rwir_int.c, and kvcpu_fast.c:
 *   - Two's-complement wraparound on `uint64_t` (no signed overflow UB; caller narrows width);
 *   - Truncating division with `INT64_MIN / -1 == INT64_MIN` and `INT64_MIN % -1 == 0`;
 *   - Caller checks `b == 0` (`ZeroDivisionError`) beforehand;
 *   - Unsigned kinds (`u == true`) use unsigned `/`, `%`, and logical `>>`;
 *   - Shift counts mask with `& 63`; signed `>>` is arithmetic right shift. */
static inline int64_t kvi_add(int64_t a, int64_t b) {
    return (int64_t)((uint64_t)a + (uint64_t)b);
}
static inline int64_t kvi_sub(int64_t a, int64_t b) {
    return (int64_t)((uint64_t)a - (uint64_t)b);
}
static inline int64_t kvi_mul(int64_t a, int64_t b) {
    return (int64_t)((uint64_t)a * (uint64_t)b);
}
static inline int64_t kvi_neg(int64_t a) {
    return (int64_t)(0u - (uint64_t)a);
}
static inline int64_t kvi_abs(int64_t a) {
    return a < 0 ? kvi_neg(a) : a;
}
/* Precondition: `b != 0`. */
static inline int64_t kvi_div(int64_t a, int64_t b, bool u) {
    if (u)
        return (int64_t)((uint64_t)a / (uint64_t)b);
    return b == -1 ? kvi_neg(a) : a / b;
}
/* Precondition: `b != 0`. */
static inline int64_t kvi_mod(int64_t a, int64_t b, bool u) {
    if (u)
        return (int64_t)((uint64_t)a % (uint64_t)b);
    return b == -1 ? 0 : a % b;
}
static inline int64_t kvi_shl(int64_t a, int64_t n) {
    return (int64_t)((uint64_t)a << ((uint64_t)n & 63u));
}
static inline int64_t kvi_shr(int64_t a, int64_t n, bool u) {
    unsigned s = (unsigned)((uint64_t)n & 63u);
    if (u || a >= 0)
        return (int64_t)((uint64_t)a >> s);
    return (int64_t)~(~(uint64_t)a >> s); /* portable arithmetic right shift */
}
