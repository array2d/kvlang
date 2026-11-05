/*
 * 标量特例 helper：除零、溢出、NaN、饱和、回绕。
 *
 * 为什么单测这一层而不是只测 handler：这些判据的失败形态是「算出一个错的值」，
 * 而经 thunk 之后，错值与正确值在输出字节上长得一样——测试要能直接看见判据本身。
 *
 * 三条必测的边界，缺一条都不算验过：
 *   1. 每种失败**输出槽不变**（我们写过 0 或 max 就会把错误伪装成值）；
 *   2. 两个失败态**码不同**（除零 vs 溢出），否则读日志的人分不出该改哪一侧；
 *   3. 饱和与回绕的对照——`float→int8` 饱和到 127，`int 300→int8` 回绕到 44。
 *
 * ── 告警形态：本目标可能以**进程崩溃**而不是断言失败报警 ────────────────
 *
 * `test_divide_overflow` 验的是 `INT64_MIN / -1` 被拦住。那一行在 C 里是未定义
 * 行为：x86 的 `idiv` 直接触发 `#DE`，进程收到 SIGFPE 就地死掉，**不会有任何
 * 断言输出、也不会有 core 之外的线索**。
 *
 * 这一点已实测确认：把 `scalar_special.c` 里的溢出判据删掉后，本测试目标以
 * `Floating point exception (core dumped)`、exit 136 结束，而不是打印
 * `expectation failed`。
 *
 * 所以：**CI 上看到本目标 core dump / exit 136，第一反应应当是「溢出判据被删了」，
 * 不是「测试框架坏了」。** 这个测试是那条判据唯一的形式化守卫——它守的不是
 * 「结果对不对」，是「进程还活着」。
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "binrwir/execution_exit.h"
#include "vmbin/handlers/scalar.h"
#include "vmbin/handlers/scalar_special.h"

static int failures;

#define EXPECT(condition)                                                     \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "%s:%d: expectation failed: %s\n", __FILE__,      \
                    __LINE__, #condition);                                    \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

/* 哨兵：`输出槽不变` 的判据必须用**数据域之外**的值，否则分不出「没写」与
 * 「写了同一个值」。0x5A5A… 不是任何一条路径会产出的数。 */
#define UNTOUCHED INT64_C(0x5A5A5A5A5A5A5A5A)

static void test_float_min_max_nan(void) {
    const double nan_value = NAN;

    /* matrix:69 —— NaN 让位于数，且与顺序无关。两个方向都必须成立：
     * 只验一个方向时，「a < b ? a : b」这种错误实现也能绿一条。 */
    EXPECT(runtimec_scalar_float_min(nan_value, 1.0) == 1.0);
    EXPECT(runtimec_scalar_float_min(1.0, nan_value) == 1.0);
    EXPECT(runtimec_scalar_float_max(nan_value, 1.0) == 1.0);
    EXPECT(runtimec_scalar_float_max(1.0, nan_value) == 1.0);

    /* 两个 NaN 得 NaN —— **让位无处可让**，必须是 NaN。
     * 这条最容易写错：`isnan(a) ? b : ...` 在 b 也是 NaN 时返回 b，碰巧对；
     * 但 `isnan(a) ? b : (isnan(b) ? a : ...)` 若写成返回 0 就错了。
     * 同时验「两个方向 + 结果确实是 NaN 而非某个数」。 */
    EXPECT(isnan(runtimec_scalar_float_min(nan_value, nan_value)));
    EXPECT(isnan(runtimec_scalar_float_max(nan_value, nan_value)));
    /* 不能只验 isnan：一个返回 0.0 的实现也能通过 `isnan` 的否定式误判，
     * 所以再钉一条「不是任何有限值」。 */
    EXPECT(!(runtimec_scalar_float_min(nan_value, nan_value) < 1e308));
    EXPECT(!(runtimec_scalar_float_max(nan_value, nan_value) > -1e308));

    /* 普通情形仍然对；相等时任一方向取同一个值。 */
    EXPECT(runtimec_scalar_float_min(2.0, 3.0) == 2.0);
    EXPECT(runtimec_scalar_float_max(2.0, 3.0) == 3.0);
    EXPECT(runtimec_scalar_float_min(2.0, 2.0) == 2.0);
    /* ±0：C 的 `<` 对 +0 与 -0 都假，两条路径给出不同符号的零——都是 0，
     * 数值上相等。这里只钉「等于 0」，不去断言符号位。 */
    EXPECT(runtimec_scalar_float_min(0.0, -0.0) == 0.0);
    EXPECT(runtimec_scalar_float_max(0.0, -0.0) == 0.0);
    /* ±Inf。 */
    EXPECT(runtimec_scalar_float_min(-INFINITY, 1.0) == -INFINITY);
    EXPECT(runtimec_scalar_float_max(INFINITY, 1.0) == INFINITY);
}

static void test_divide_by_zero(void) {
    binrwir_execution_exit_t exit;
    int64_t value = UNTOUCHED;

    exit = (binrwir_execution_exit_t){0};
    runtimec_scalar_divide_signed(7, 0, 64U, &value, &exit);
    EXPECT(exit.kind == BINRWIR_EXECUTION_EXIT_ERROR);
    EXPECT(exit.detail_code == RUNTIMEC_SCALAR_DIAG_DIVIDE_BY_ZERO);
    EXPECT(value == UNTOUCHED);

    exit = (binrwir_execution_exit_t){0};
    value = UNTOUCHED;
    runtimec_scalar_divide_signed(-7, 0, 8U, &value, &exit);
    EXPECT(exit.detail_code == RUNTIMEC_SCALAR_DIAG_DIVIDE_BY_ZERO);
    EXPECT(value == UNTOUCHED);

    exit = (binrwir_execution_exit_t){0};
    value = UNTOUCHED;
    runtimec_scalar_remainder_signed(7, 0, 64U, &value, &exit);
    EXPECT(exit.detail_code == RUNTIMEC_SCALAR_DIAG_DIVIDE_BY_ZERO);
    EXPECT(value == UNTOUCHED);

    {
        uint64_t unsigned_value = UINT64_C(0x5A5A5A5A5A5A5A5A);
        exit = (binrwir_execution_exit_t){0};
        runtimec_scalar_divide_unsigned(7U, 0U, &unsigned_value, &exit);
        EXPECT(exit.detail_code == RUNTIMEC_SCALAR_DIAG_DIVIDE_BY_ZERO);
        EXPECT(unsigned_value == UINT64_C(0x5A5A5A5A5A5A5A5A));
    }
}

static void test_divide_overflow(void) {
    binrwir_execution_exit_t exit;
    int64_t value = UNTOUCHED;

    /* INT64_MIN / -1：C 是 UB，x86 直接 SIGFPE。必须被拦，且码不同于除零。 */
    exit = (binrwir_execution_exit_t){0};
    runtimec_scalar_divide_signed(INT64_MIN, -1, 64U, &value, &exit);
    EXPECT(exit.kind == BINRWIR_EXECUTION_EXIT_ERROR);
    EXPECT(exit.detail_code == RUNTIMEC_SCALAR_DIAG_DIVIDE_OVERFLOW);
    EXPECT(exit.detail_code != RUNTIMEC_SCALAR_DIAG_DIVIDE_BY_ZERO);
    EXPECT(value == UNTOUCHED);

    /* 同一条判据在窄宽度上：int8 的 -128 / -1 同样是溢出。 */
    exit = (binrwir_execution_exit_t){0};
    value = UNTOUCHED;
    runtimec_scalar_divide_signed(-128, -1, 8U, &value, &exit);
    EXPECT(exit.detail_code == RUNTIMEC_SCALAR_DIAG_DIVIDE_OVERFLOW);
    EXPECT(value == UNTOUCHED);

    /* 反向控制：-127 / -1 = 127 必须**算出来**，不是一并拒绝。 */
    exit = (binrwir_execution_exit_t){0};
    value = UNTOUCHED;
    runtimec_scalar_divide_signed(-127, -1, 8U, &value, &exit);
    EXPECT(exit.kind == BINRWIR_EXECUTION_EXIT_NONE);
    EXPECT(value == 127);

    /* 余数同源。 */
    exit = (binrwir_execution_exit_t){0};
    value = UNTOUCHED;
    runtimec_scalar_remainder_signed(INT64_MIN, -1, 64U, &value, &exit);
    EXPECT(exit.detail_code == RUNTIMEC_SCALAR_DIAG_DIVIDE_OVERFLOW);
    EXPECT(value == UNTOUCHED);
}

static void test_divide_quotient_rounds_toward_zero(void) {
    binrwir_execution_exit_t exit = {0};
    int64_t value = UNTOUCHED;

    /* matrix:22 —— 商向零截断，不是向下取整。 */
    runtimec_scalar_divide_signed(-7, 3, 64U, &value, &exit);
    EXPECT(exit.kind == BINRWIR_EXECUTION_EXIT_NONE);
    EXPECT(value == -2);

    /* matrix:68 —— 余数随被除数符号。 */
    exit = (binrwir_execution_exit_t){0};
    value = UNTOUCHED;
    runtimec_scalar_remainder_signed(-7, 3, 64U, &value, &exit);
    EXPECT(value == -1);
    exit = (binrwir_execution_exit_t){0};
    value = UNTOUCHED;
    runtimec_scalar_remainder_signed(7, -3, 64U, &value, &exit);
    EXPECT(value == 1);
}

static void test_float_to_integer(void) {
    binrwir_execution_exit_t exit = {0};
    int64_t value = UNTOUCHED;

    /* 截断向零。 */
    runtimec_scalar_float_to_signed(3.9, 64U, &value, &exit);
    EXPECT(value == 3);
    exit = (binrwir_execution_exit_t){0};
    value = UNTOUCHED;
    runtimec_scalar_float_to_signed(-3.9, 64U, &value, &exit);
    EXPECT(value == -3);

    /* 饱和：1e300 → int8 得 127，不是 UB、不是回绕。 */
    exit = (binrwir_execution_exit_t){0};
    value = UNTOUCHED;
    runtimec_scalar_float_to_signed(1e300, 8U, &value, &exit);
    EXPECT(exit.kind == BINRWIR_EXECUTION_EXIT_NONE);
    EXPECT(value == 127);
    exit = (binrwir_execution_exit_t){0};
    value = UNTOUCHED;
    runtimec_scalar_float_to_signed(-1e300, 8U, &value, &exit);
    EXPECT(value == -128);

    /* ±Inf 同饱和。 */
    exit = (binrwir_execution_exit_t){0};
    value = UNTOUCHED;
    runtimec_scalar_float_to_signed(INFINITY, 32U, &value, &exit);
    EXPECT(value == INT32_MAX);

    /* NaN 是错误，输出槽不动（matrix:21）。 */
    exit = (binrwir_execution_exit_t){0};
    value = UNTOUCHED;
    runtimec_scalar_float_to_signed(NAN, 64U, &value, &exit);
    EXPECT(exit.kind == BINRWIR_EXECUTION_EXIT_ERROR);
    EXPECT(exit.detail_code == RUNTIMEC_SCALAR_DIAG_NOT_A_NUMBER);
    EXPECT(value == UNTOUCHED);

    {
        uint64_t unsigned_value = UINT64_C(0x5A5A5A5A5A5A5A5A);
        exit = (binrwir_execution_exit_t){0};
        runtimec_scalar_float_to_unsigned(-5.0, 32U, &unsigned_value, &exit);
        EXPECT(unsigned_value == 0U);
        exit = (binrwir_execution_exit_t){0};
        runtimec_scalar_float_to_unsigned(1e300, 32U, &unsigned_value, &exit);
        EXPECT(unsigned_value == UINT32_MAX);
    }
}

static void test_integer_wrap(void) {
    /* matrix:63 —— 窄化是回绕：int8(300) 是 44。饱和会得到 127，那是另一个语义。 */
    EXPECT(runtimec_scalar_wrap_signed(300, 64U, 8U) == 44);
    EXPECT(runtimec_scalar_wrap_signed(300, 64U, 8U) != 127);
    /* -300 mod 256 = 212，按 8 位补码解释是 -44。 */
    EXPECT(runtimec_scalar_wrap_signed(-300, 64U, 8U) == -44);
    EXPECT(runtimec_scalar_wrap_signed(-1, 64U, 8U) == -1);
    EXPECT(runtimec_scalar_wrap_signed(127, 64U, 8U) == 127);
    EXPECT(runtimec_scalar_wrap_signed(128, 64U, 8U) == -128);
    EXPECT(runtimec_scalar_wrap_unsigned(300U, 8U) == 44U);
    EXPECT(runtimec_scalar_wrap_unsigned(UINT64_MAX, 8U) == 255U);
    /* 同宽是恒等。 */
    EXPECT(runtimec_scalar_wrap_signed(INT64_MIN, 64U, 64U) == INT64_MIN);
    EXPECT(runtimec_scalar_wrap_unsigned(UINT64_MAX, 64U) == UINT64_MAX);
}

/*
 * 跨符号比较（matrix:28）：按精确数学值，不是按转型后的位。
 *
 * 这里每一条都必须能在「两边都转 int64」的错误实现下变红——`UINT64_MAX` 会变成
 * −1，于是「UINT64_MAX 比 1 大」这条判据会失败。常规样本（都小于 2^63）在错误
 * 实现下照样绿，所以**边界两侧各挑一条**是必须的。
 */
static void test_cross_sign_compare(void) {
    /* 负数恒小于任何无符号数，包括 0。 */
    EXPECT(runtimec_scalar_compare_signed_unsigned(-1, 0U) == -1);
    EXPECT(runtimec_scalar_compare_signed_unsigned(-1, UINT64_MAX) == -1);
    EXPECT(runtimec_scalar_compare_signed_unsigned(INT64_MIN, 0U) == -1);

    /* 非负：同域比较。 */
    EXPECT(runtimec_scalar_compare_signed_unsigned(1, 2U) == -1);
    EXPECT(runtimec_scalar_compare_signed_unsigned(2, 2U) == 0);
    EXPECT(runtimec_scalar_compare_signed_unsigned(3, 2U) == 1);
    EXPECT(runtimec_scalar_compare_signed_unsigned(0, 0U) == 0);

    /*
     * 2^63 边界：这是「两边都转 int64」唯一会错的地方。
     * INT64_MAX 是 2^63−1，所以它必然小于 2^63。
     */
    EXPECT(runtimec_scalar_compare_signed_unsigned(INT64_MAX,
                                                   UINT64_C(1) << 63) == -1);
    EXPECT(runtimec_scalar_compare_signed_unsigned(
               INT64_MAX, UINT64_C(0x8000000000000000)) == -1);
    /* INT64_MAX 与它自己相等（同域一侧的无符号表示）。 */
    EXPECT(runtimec_scalar_compare_signed_unsigned(
               INT64_MAX, (uint64_t)INT64_MAX) == 0);
    /* 反向：绝不能因为 UINT64_MAX 转成 −1 就判「相等」或「更小」。 */
    EXPECT(runtimec_scalar_compare_signed_unsigned(-1, UINT64_MAX) == -1);
    EXPECT(runtimec_scalar_compare_signed_unsigned(1, UINT64_MAX) == -1);

    /* 对称方向必须给出相反号——两条独立实现会漂，所以验一致。 */
    EXPECT(runtimec_scalar_compare_unsigned_signed(UINT64_MAX, -1) == 1);
    EXPECT(runtimec_scalar_compare_unsigned_signed(0U, -1) == 1);
    EXPECT(runtimec_scalar_compare_unsigned_signed(2U, 1) == 1);
    EXPECT(runtimec_scalar_compare_unsigned_signed(
               UINT64_C(1) << 63, INT64_MAX) == 1);
}


/*
 * matrix:71 shift-count-masking —— 计数按宽度减一掩码；有符号算术右移、无符号逻辑右移。
 *
 * 三条边界缺一不可：计数 >= 宽度（掩码生效）、计数掩码后为 0（`shl(x, 64)` 是
 * 恒等而不是 UB）、负值右移（算术移位补符号位）。
 */
static void test_shifts(void) {
    /* 计数掩码：64 位操作数只取低 6 位。 */
    EXPECT(runtimec_scalar_shift_left_signed(1, 64, 64) == 1);
    EXPECT(runtimec_scalar_shift_left_signed(1, 65, 64) == 2);
    EXPECT(runtimec_scalar_shift_left_signed(1, 0, 64) == 1);
    /* 8 位操作数只取低 3 位。 */
    EXPECT(runtimec_scalar_shift_left_signed(1, 8, 8) == 1);
    EXPECT(runtimec_scalar_shift_left_signed(1, 9, 8) == 2);
    /* 左移溢出按操作数宽度回绕。 */
    EXPECT(runtimec_scalar_shift_left_signed(1, 7, 8) == -128);

    /* 有符号右移是**算术**移位：负值补 1。 */
    EXPECT(runtimec_scalar_shift_right_signed(-1, 1, 64) == -1);
    EXPECT(runtimec_scalar_shift_right_signed(-8, 1, 64) == -4);
    EXPECT(runtimec_scalar_shift_right_signed(-8, 1, 8) == -4);
    EXPECT(runtimec_scalar_shift_right_signed(-1, 3, 8) == -1);
    /* 无符号右移是**逻辑**移位：补 0。这条与上一条必须给出不同答案，
     * 否则说明实现没按符号性分。 */
    EXPECT(runtimec_scalar_shift_right_unsigned(0x80U, 1, 8) == 0x40U);
    EXPECT(runtimec_scalar_shift_right_unsigned(UINT64_MAX, 1, 64) ==
           (UINT64_MAX >> 1));
    /* 窄宽度下高位不得"漏"进来：uint8 的 0x80 右移必须是 0x40，不是 int64 的 0x40 之外的位。 */
    EXPECT(runtimec_scalar_shift_right_unsigned(0xFFFFU, 4, 8) == 0x0FU);
}

int main(void) {
    test_float_min_max_nan();
    test_divide_by_zero();
    test_divide_overflow();
    test_divide_quotient_rounds_toward_zero();
    test_float_to_integer();
    test_integer_wrap();
    test_cross_sign_compare();
    test_shifts();
    if (failures != 0) {
        fprintf(stderr, "scalar_special_test: %d failure(s)\n", failures);
        return 1;
    }
    fprintf(stdout, "scalar_special_test: OK\n");
    return 0;
}
