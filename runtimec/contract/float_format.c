/*
 * 组件：contract/float_format 的实现
 * 作用：浮点值 → 文本。完整契约见头注释；本文件按**三件正交的事**分段实现。
 * 约束：纯函数、不分配；缓冲不足返回 false 且不写部分内容。
 *
 * ── 为什么分三段，而不是「%.*g 之后再判断要不要改」────────────────────
 *
 * 浮点文本化是三个**互相独立**的维度，各自有各自的判据来源：
 *
 *   | 维度       | 决定什么            | 谁定                        |
 *   |------------|---------------------|-----------------------------|
 *   | 最短往返   | 有效数字几位        | 值本身（按宽度定往返域）    |
 *   | 定点/指数  | `100.0` vs `1e+02`  | 阈值 `[1e-4, 1e16)`         |
 *   | `.0` 规则  | `12` vs `12.0`      | 整数形态必须有小数点        |
 *
 * 把它们压进一个 `%.*g` 再打补丁，就是**把三维问题压回一维**——`%g` 一位参数
 * 同时决定有效数字位数与定点/指数切换，两者没法分开调。所以这里让 `%g` 完全
 * 不参与判断：先用 `%.*e` 拿到「有效数字串 + 十进制指数」这一份中性数据，
 * 再由三段各自决定输出形态。
 */

#include "float_format.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* double 的十进制往返上限是 16 位小数（= 17 位有效数字）；float 是 8 位（9 位有效）。 */
#define RUNTIMEC_FLOAT_FORMAT_MAX_FRACTION_DIGITS 17

/*
 * 定点与指数的分界：十进制指数落在 **[EXPONENT_FIXED_MIN, EXPONENT_FIXED_MAX)**
 * 时用定点，之外用指数。
 *
 * 这两个数是**语言行为，不是实现细节**：`100.0` 打 `100.0` 还是 `1e+02` 会被
 * 用户直接看到。阈值取 Python `repr` 的那一对。
 *
 * **与宽度无关**：float32 与 float64 用同一对阈值。理由是这两个数回答的是
 * 「人类怎么读这个量级」，不是「这个值占几个字节」——float32 的 `1e16`
 * 与 float64 的 `1e16` 在读者眼里是同一个量级。下一个人不要以为它们是从
 * 宽度推出来的。
 */
#define RUNTIMEC_FLOAT_FORMAT_FIXED_EXPONENT_MIN (-4)
#define RUNTIMEC_FLOAT_FORMAT_FIXED_EXPONENT_MAX 16

/*
 * 非有限值写死。
 *
 * **不调 `%g`**：NaN 的符号位在 IEEE 754 里未指定，`printf` 的输出因此依赖平台
 * 与优化级别——`0.0/0.0` 可能得到 `nan` 也可能得到 `-nan`。那违反「同输入同输出」，
 * 而且测不出来：两次跑恰好相同是常态，坏的那次看起来像偶然。
 *
 * `inf` 虽然能被 `%g` 正确打印，也一并走这里——判据是「非有限值不做十进制往返」，
 * 比「inf 恰好能往返」更稳。
 */
static bool runtimec_float_format_non_finite(double value, char *output,
                                             size_t capacity,
                                             size_t *output_length) {
    const char *text;

    if (!isnan(value) && !isinf(value)) {
        return false;
    }
    /* NaN 的符号位无意义：`nan` 与 `-nan` 是同一个值，输出必须相同。 */
    if (isnan(value)) {
        text = "nan";
    } else {
        text = signbit(value) != 0 ? "-inf" : "inf";
    }
    {
        const size_t length = strlen(text);

        if (length >= capacity) {
            return false;
        }
        memcpy(output, text, length);
        output[length] = '\0';
        *output_length = length;
    }
    return true;
}

/*
 * ── 第一段：最短往返 ──────────────────────────────────────────────────
 *
 * 只回答「有效数字要几位」。产出两份中性数据：
 *   `digits` —— 有效数字串（不含小数点，如 `"1234567890123"`）；
 *   `exponent` —— 十进制指数（值 = 0.digits × 10^(exponent+1)，即首位数字的权重）。
 *
 * **往返域由 `domain` 决定，不由参数类型决定**：`value` 恒是 double，但 float32
 * 值的往返判据必须是 `strtof`——否则会打出那些在 float32 里根本不存在的尾数。
 */
static bool runtimec_float_format_shortest_digits(
    double value, runtimec_float_domain_t domain, char *digits,
    size_t digits_capacity, int *exponent, bool *negative) {
    int fraction;
    bool found = false;

    for (fraction = 0; fraction <= RUNTIMEC_FLOAT_FORMAT_MAX_FRACTION_DIGITS;
         ++fraction) {
        char candidate[64];
        const int written = snprintf(candidate, sizeof(candidate), "%.*e",
                                     fraction, value);

        if (written <= 0 || (size_t)written >= sizeof(candidate)) {
            return false;
        }
        if (domain == RUNTIMEC_FLOAT_DOMAIN_FLOAT32) {
            found = strtof(candidate, NULL) == (float)value;
        } else {
            found = strtod(candidate, NULL) == value;
        }
        if (!found) {
            continue;
        }
        /* `%.*e` 恒形如 `d[.ddd]e±xx`：整数部分一位，之后是 '.' 与小数、再是 'e'。 */
        {
            /*
             * `%.*e` 对负值形如 `-1.23e+02`：**负号在最前面**，不在有效数字里。
             * 有效数字从 `sign` 之后开始——把负号当首位数字，会拼出 `-.0` 这种串
             * （实测过一次：所有负值的输出全错）。
             */
            const char *sign = candidate[0] == '-' ? candidate + 1 : candidate;

            *negative = candidate[0] == '-';
            const char *dot = strchr(sign, '.');
            const char *mark = strchr(sign, 'e');
            size_t integral = (size_t)((dot != NULL ? dot : mark) - sign);
            size_t count = 0U;

            if (integral >= digits_capacity) {
                return false;
            }
            digits[count++] = sign[0];
            if (dot != NULL && mark != NULL) {
                const size_t fractional = (size_t)(mark - dot - 1);

                if (count + fractional >= digits_capacity) {
                    return false;
                }
                memcpy(digits + count, dot + 1, fractional);
                count += fractional;
            }
            digits[count] = '\0';
            *exponent = mark != NULL ? atoi(mark + 1) : 0;
            return true;
        }
        return true;
    }
    /* 17 位小数都读不回来，说明实现有错——不降级、不猜。 */
    return false;
}

/* 去掉有效数字串尾部的零；保留至少一位（`1000` → `1`）。 */
static void runtimec_float_format_trim_digits(char *digits) {
    size_t length = strlen(digits);

    while (length > 1U && digits[length - 1U] == '0') {
        digits[length - 1U] = '\0';
        --length;
    }
}

/*
 * ── 第二段：定点还是指数 ──────────────────────────────────────────────
 *
 * 纯阈值判断，只看十进制指数。**与值、与宽度都无关**——这是它能被单独写一段的
 * 原因：判据只依赖 `exponent` 一个整数。
 */
static bool runtimec_float_format_use_fixed(int exponent) {
    return exponent >= RUNTIMEC_FLOAT_FORMAT_FIXED_EXPONENT_MIN &&
           exponent < RUNTIMEC_FLOAT_FORMAT_FIXED_EXPONENT_MAX;
}

/*
 * ── 第三段：拼串 ──────────────────────────────────────────────────────
 *
 * 输入是前两段的产物（有效数字 + 指数 + 用不用定点），输出最终文本。
 * 三段里只有这一段接触输出缓冲。
 */

/* 指数形态：`d[.ddd]e±XX`，指数至少两位（`1e+02`、`5e-324`）。 */
static bool runtimec_float_format_write_exponential(const char *digits,
                                                    int exponent,
                                                    bool negative, char *output,
                                                    size_t capacity,
                                                    size_t *output_length) {
    char tail[32];
    int written;

    if (digits[1] != '\0') {
        written = snprintf(tail, sizeof(tail), "e%c%02d",
                           exponent < 0 ? '-' : '+',
                           exponent < 0 ? -exponent : exponent);
    } else {
        written = snprintf(tail, sizeof(tail), "e%c%02d",
                           exponent < 0 ? '-' : '+',
                           exponent < 0 ? -exponent : exponent);
    }
    if (written <= 0 || (size_t)written >= sizeof(tail)) {
        return false;
    }
    {
        const size_t digits_length = strlen(digits);
        const size_t tail_length = (size_t)written;
        const size_t separated = digits[1] != '\0' ? 1U : 0U; /* '.' */
        const size_t sign_length = negative ? 1U : 0U;
        const size_t total = sign_length + digits_length + separated + tail_length;

        if (total >= capacity) {
            return false;
        }
        {
            size_t cursor = 0U;

            if (negative) {
                output[cursor++] = '-';
            }
            output[cursor++] = digits[0];

            if (separated != 0U) {
                output[cursor++] = '.';
                memcpy(output + cursor, digits + 1, digits_length - 1U);
                cursor += digits_length - 1U;
            }
            memcpy(output + cursor, tail, tail_length);
            cursor += tail_length;
            output[cursor] = '\0';
            *output_length = cursor;
        }
    }
    return true;
}

/*
 * 定点形态：按指数把小数点放在正确位置，缺位补零。
 *
 * 同时承担 `.0` 规则：**整数形态必须带小数点**，否则 `12` 会被读成 int64。
 * 这条判据是「值的 kind 是浮点」，在拼串这一步落地——所以 `.0` 不是独立一段，
 * 它只在这一段里多写两个字符。
 */
static bool runtimec_float_format_write_fixed(const char *digits, int exponent,
                                              bool negative, char *output,
                                              size_t capacity,
                                              size_t *output_length) {
    const size_t digits_length = strlen(digits);
    const size_t sign_length = negative ? 1U : 0U;
    size_t cursor = 0U;
    size_t total;
    int ordinal;

    if (exponent >= 0) {
        const size_t integral = (size_t)exponent + 1U;
        const size_t fractional = digits_length > integral
                                      ? digits_length - integral
                                      : 0U;

        /* 整数形态补 `.0`；否则写小数部分。 */
        total = sign_length + integral +
                (fractional != 0U ? 1U + fractional : 2U);
    } else {
        const size_t leading_zeros = (size_t)(-exponent) - 1U;

        total = sign_length + 2U + leading_zeros + digits_length;
    }
    /*
     * **先算总长、够才写**：先写一部分再发现放不下，会在输出里留下半个结果，
     * 而调用方只看返回值的话不知道缓冲已被污染。契约要求「不足即失败且不写部分」。
     */
    if (total >= capacity) {
        return false;
    }
    if (negative) {
        output[cursor++] = '-';
    }
    if (exponent >= 0) {
        const size_t integral = (size_t)exponent + 1U;

        for (ordinal = 0; ordinal < (int)integral; ++ordinal) {
            output[cursor++] = (size_t)ordinal < digits_length
                                   ? digits[ordinal]
                                   : '0';
        }
        if (digits_length > integral) {
            const size_t fractional = digits_length - integral;

            output[cursor++] = '.';
            memcpy(output + cursor, digits + integral, fractional);
            cursor += fractional;
        } else {
            /* 整数形态：补 `.0`，否则读者分不出这是浮点。 */
            output[cursor++] = '.';
            output[cursor++] = '0';
        }
    } else {
        const size_t leading_zeros = (size_t)(-exponent) - 1U;
        size_t pad;

        output[cursor++] = '0';
        output[cursor++] = '.';
        for (pad = 0U; pad < leading_zeros; ++pad) {
            output[cursor++] = '0';
        }
        memcpy(output + cursor, digits, digits_length);
        cursor += digits_length;
    }
    output[cursor] = '\0';
    *output_length = cursor;
    return true;
}

/*
 * 核心算法出口：**只转发**，不复制实现。
 *
 * 这两个函数存在的原因是分层，不是新能力：显示层与键层要共用「最短往返 + 指数」
 * 与「非有限值固定串」，但那两段今天是 `static`。开出口比复制一份好——复制就是
 * 同一规则的两处真相，两边迟早漂开，而漂开的表现是「同一个值在两处显示不同」。
 *
 * 本文件的 `shortest` 与两个 `static` 一行未改：改它们的门禁是「println 输出
 * 零变化」，那要用 ctest 证明，不是顺手重构。
 */
bool runtimec_float_format_core(double value, runtimec_float_domain_t domain,
                                char *digits, size_t digits_capacity,
                                int *exponent, bool *negative) {
    int computed_exponent = 0;
    bool computed_negative = false;

    if (digits == NULL || exponent == NULL || negative == NULL ||
        digits_capacity == 0U) {
        return false;
    }
    digits[0] = '\0';
    if (!runtimec_float_format_shortest_digits(value, domain, digits,
                                               digits_capacity,
                                               &computed_exponent,
                                               &computed_negative)) {
        return false;
    }
    /* 去尾零属于「最短」，不属于呈现层——两个呈现层都要这一步。 */
    runtimec_float_format_trim_digits(digits);
    *exponent = computed_exponent;
    *negative = computed_negative;
    return true;
}

bool runtimec_float_format_non_finite_text(double value, char *output,
                                           size_t capacity,
                                           size_t *output_length) {
    if (output_length == NULL) {
        return false;
    }
    *output_length = 0U;
    return runtimec_float_format_non_finite(value, output, capacity,
                                            output_length);
}

bool runtimec_float_format_shortest(double value, runtimec_float_domain_t domain,
                                    char *output, size_t capacity,
                                    size_t *output_length) {
    char digits[32];
    int exponent = 0;
    bool negative = false;

    if (output == NULL || output_length == NULL) {
        return false;
    }
    *output_length = 0U;
    if (capacity == 0U) {
        return false;
    }
    /* 第零步：非有限值不走十进制往返，直接写死。 */
    if (runtimec_float_format_non_finite(value, output, capacity,
                                         output_length)) {
        return true;
    }
    /* 第一段：最短往返 → 有效数字 + 指数。 */
    if (!runtimec_float_format_shortest_digits(value, domain, digits,
                                               sizeof(digits), &exponent,
                                               &negative)) {
        return false;
    }
    runtimec_float_format_trim_digits(digits);
    /* 第二段：阈值决定形态。 */
    if (runtimec_float_format_use_fixed(exponent)) {
        /* 第三段：拼串（含 `.0` 规则）。 */
        return runtimec_float_format_write_fixed(digits, exponent, negative,
                                                  output, capacity,
                                                  output_length);
    }
    return runtimec_float_format_write_exponential(digits, exponent, negative,
                                                    output, capacity,
                                                    output_length);
}
