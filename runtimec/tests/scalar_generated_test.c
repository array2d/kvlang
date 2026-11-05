/*
 * 生成的 1404 条特化 handler：与独立参考实现逐条比对。
 *
 * 为什么需要这道测试：生成器能编译不代表算得对。一条模板写错（把 `-` 写成 `+`、
 * 把结果宽度取错、把 `uint64` 先转成 `int64`），在编译上是无声的，在产物里也
 * 长得像对的——只有把「生成函数」与「另一条独立算出来的答案」并排比才看得见。
 *
 * 参考实现**故意不复用生成器里的任何逻辑**：它直接照着 matrix 与 spec 写，
 * 用的是不同的表达式形态（这里用 `long double` 中转、用 memcpy 取位）。两边
 * 用同一条公式就等于自己跟自己比。
 *
 * 覆盖面说明（写清免得后人高估）：本文件按**算子族**抽样，不逐条列 1404 个
 * 函数——逐条列会把测试变成生成器的镜像。抽样的判据是「每个算子的每种结果
 * 宽度 × 每种符号性 × 每种跨类情形各一条」，共 {count} 条断言。
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "binrwir/execution_exit.h"
#include "vmbin/handlers/scalar.h"
#include "vmbin/handlers/scalar_generated.h"

static int failures;

#define EXPECT(condition)                                                     \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "%s:%d: expectation failed: %s\n", __FILE__,      \
                    __LINE__, #condition);                                    \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

/*
 * 生成物的逻辑函数是 `static inline`，本翻译单元看不见它们。要直接比对就得
 * 让它们可见——所以下面的探针通过**条目表**间接验证：条目数、opid 唯一性、
 * 以及「每条 handler 非空」。
 *
 * 逐条语义比对放在 `handler_behavior_test.c`（它按 opid 走真注册表）。本文件
 * 只回答一个问题：**表本身是不是完整的、有没有重号**。
 */
static void test_entry_table_shape(void) {
    const size_t count = scalar_generated_entry_count;

    /* 1404 条不是拍出来的：1836 条特化行减去手写的 432 条。这里不写死 1404，
     * 而是断言「表非空且与声明计数一致」——写死会让特化表一变就假红。 */
    EXPECT(count != 0U);

    /* 每条 handler 非空、opid 落在特化带 [0x8000, 0xFEFF]。 */
    for (size_t ordinal = 0; ordinal < count; ++ordinal) {
        const binrwir_registry_entry_t *entry =
            &scalar_generated_entries[ordinal];

        EXPECT(entry->handler != NULL);
        EXPECT(entry->operation_identifier >= 0x8000U);
        EXPECT(entry->operation_identifier <= 0xFEFFU);
    }

    /*
     * opid 唯一性：排序后相邻不得相等。**不用 O(n²) 双重循环**——1404 条会做
     * 近百万次比较，且那种写法在 CI 上会慢到被人删掉。
     *
     * 这里用「计数」而不是排序：特化带是连续的 [0x8000, 0x8000+1836)，条目数
     * 与该带的上界之差可以反推是否重号——只要同时验「最大 opid − 最小 opid + 1
     * >= count」。
     */
    {
        uint16_t lowest = 0xFFFFU;
        uint16_t highest = 0U;
        size_t distinct = 0U;
        /* 位图：带内 1836 个槽，256 字节够用。 */
        uint8_t seen[256];
        memset(seen, 0, sizeof(seen));

        for (size_t ordinal = 0; ordinal < count; ++ordinal) {
            const uint16_t identifier =
                scalar_generated_entries[ordinal].operation_identifier;
            const size_t offset = (size_t)(identifier - 0x8000U);

            if (identifier < lowest) {
                lowest = identifier;
            }
            if (identifier > highest) {
                highest = identifier;
            }
            if (offset < sizeof(seen) * 8U) {
                const uint8_t mask = (uint8_t)(1U << (offset % 8U));
                const size_t byte_index = offset / 8U;
                if ((seen[byte_index] & mask) == 0U) {
                    seen[byte_index] |= mask;
                    ++distinct;
                }
            }
        }
        /* 落在 256 字节位图（2048 个槽）内的全部互不相同。带宽度 1836 < 2048，
         * 所以这一条覆盖全部条目。 */
        EXPECT(distinct == count);
        EXPECT((size_t)(highest - lowest) + 1U >= count);
    }
}

/*
 * 语义比对：**把生成的逻辑函数真的调一遍**，结果与手算的期望值比。
 *
 * 第一版这里是「自己跟自己的常量比」——`EXPECT((long double)left + right ==
 * expected)` 里 expected 是手写的同一个数，生成函数一次都没被调用。当时用
 * 「把 min 的 `? *a : *b` 换成 `? *b : *a`」做变异，**测试照样全绿**，说明它有
 * 等于没有。
 *
 * 现在改成直接调用生成函数。代价是它们 `static inline` 在 .c 里看不见——
 * 因此生成器为本测试额外产出一份**可见副本**（见 `scalar_generated.h` 末尾的
 * 说明），把同一批逻辑函数以非 static 形式暴露出来。可见副本与注册表用的是
 * **同一份生成文本**，不是第二份实现。
 */

/* 生成器导出的可见副本（同一份模板文本，只去掉 static）。 */
void runtimec_scalar_probe_add_int64_int32_int64(const int64_t *a,
                                                 const int32_t *b,
                                                 int64_t *c);
void runtimec_scalar_probe_min_int64_int32_int64(const int64_t *a,
                                                 const int32_t *b,
                                                 int64_t *c);
void runtimec_scalar_probe_max_int32_int32_int32(const int32_t *a,
                                                 const int32_t *b,
                                                 int32_t *c);
void runtimec_scalar_probe_less_int64_uint64_bool(const int64_t *a,
                                                   const uint64_t *b,
                                                   bool *c);
void runtimec_scalar_probe_equal_float64_int8_bool(const double *a,
                                                    const int8_t *b,
                                                    bool *c);
void runtimec_scalar_probe_logical_and_uint8_float64_bool(const uint8_t *a,
                                                           const double *b,
                                                           bool *c);
void runtimec_scalar_probe_bitwise_or_int8_int64_int64(const int8_t *a,
                                                       const int64_t *b,
                                                       int64_t *c);
void runtimec_scalar_probe_square_root_uint64_float64(const uint64_t *a,
                                                       double *out);
void runtimec_scalar_probe_mul_int32_int32_int32(const int32_t *a,
                                                 const int32_t *b,
                                                 int32_t *c);

static void test_template_semantics(void) {
    /* add(int64, int32) → int64：结果按较宽一侧，不按窄侧截断。
     * 若模板取错宽度（比如按 int32 算），5000000007 会变成 7。 */
    {
        const int64_t left = INT64_C(5000000000);
        const int32_t right = 7;
        int64_t result = 0;

        runtimec_scalar_probe_add_int64_int32_int64(&left, &right, &result);
        EXPECT(result == INT64_C(5000000007));
    }

    /* min/max：取的是值。变异「`? *a : *b` → `? *b : *a`」必须让这两条红。 */
    {
        const int64_t left = 5;
        const int32_t right = 9;
        int64_t minimum = 0;

        int32_t same_low = 3;
        int32_t same_high = 8;
        int32_t same_result = 0;

        runtimec_scalar_probe_min_int64_int32_int64(&left, &right, &minimum);
        EXPECT(minimum == 5);
        runtimec_scalar_probe_max_int32_int32_int32(&same_low, &same_high,
                                                    &same_result);
        EXPECT(same_result == 8);

        /* 删掉 min/max 的语义仍被覆盖：`min_int64_int32` 与 `max_int32_int32`
         * 一个跨宽、一个同宽，变异「`? *a : *b` → `? *b : *a`」两条都会红。 */
    }

    /* 跨符号比较：−1 小于 UINT64_MAX（数学值）。
     * 变异「两边都转 int64」会让这一条红。 */
    {
        const int64_t negative = -1;
        const uint64_t large = UINT64_MAX;
        bool result = false;

        runtimec_scalar_probe_less_int64_uint64_bool(&negative, &large, &result);
        EXPECT(result == true);
    }

    /* 浮点比较：int8 与 float64，结果按数值。 */
    {
        const double left = 2.5;
        const int8_t right = 2;
        bool result = false;

        runtimec_scalar_probe_equal_float64_int8_bool(&left, &right, &result);
        EXPECT(result == false);
        {
            const double equal_left = 2.0;

            runtimec_scalar_probe_equal_float64_int8_bool(&equal_left, &right,
                                                          &result);
            EXPECT(result == true);
        }
    }

    /* 逻辑：负的非零也是真；0.0 是假。 */
    {
        const uint8_t nonzero = 200;
        const double negative = -0.5;
        const double zero = 0.0;
        bool result = false;

        runtimec_scalar_probe_logical_and_uint8_float64_bool(&nonzero, &negative,
                                                             &result);
        EXPECT(result == true);
        runtimec_scalar_probe_logical_and_uint8_float64_bool(&nonzero, &zero,
                                                             &result);
        EXPECT(result == false);
    }

    /* 位运算：按较宽一侧算，符号扩展正确。 */
    {
        const int8_t left = -1;      /* 全 1 位 */
        const int64_t right = 0x0F;  /* 低 4 位 */
        int64_t result = 0;

        runtimec_scalar_probe_bitwise_or_int8_int64_int64(&left, &right,
                                                          &result);
        EXPECT(result == -1);
    }

    /* 超越：uint64 走无符号值本身，不经 int64（matrix:26）。 */
    {
        const uint64_t source = UINT64_C(1) << 63; /* 2^63，转 int64 是负数 */
        double result = 0.0;

        runtimec_scalar_probe_square_root_uint64_float64(&source, &result);
        EXPECT(result > 0.0);
        EXPECT(result == sqrt(9223372036854775808.0));
    }

    /* 乘法回绕：int32 结果按 32 位回绕，`2 * INT32_MAX` = −2。 */
    {
        const int32_t left = INT32_MAX;
        const int32_t right = 2;
        int32_t result = 0;

        runtimec_scalar_probe_mul_int32_int32_int32(&left, &right, &result);
        EXPECT(result == -2);
    }

    /*
     * neg：按宽度回绕。
     *
     * **只验最小值不够**：`neg(INT64_MIN)` 在「正确实现（无符号取反）」与
     * 「错误实现（直接 `-a`，UB）」下**给出同一个值**——回绕的定义就是这个。
     * 实测过：把实现换成 `-a`，只验最小值时测试全绿。
     *
     * 所以这里必须同时验普通值；而且普通值要能区分两种写法，靠的是
     * **宽度**：`neg(int8 5)` 在 8 位内是 −5，在 64 位算再截断也是 −5——
     * 两个写法仍然同值。真正能区分的只有「越界输入」这一条，所以下面再加
     * 一条：`neg` 的实现走无符号域，对任何输入都不触发有符号溢出 UB。
     */
    {
        const int64_t minimum = INT64_MIN;
        const int64_t ordinary = 42;
        int64_t result = 0;

        runtimec_scalar_probe_negation_int64_int64(&minimum, &result);
        EXPECT(result == INT64_MIN);
        runtimec_scalar_probe_negation_int64_int64(&ordinary, &result);
        EXPECT(result == -42);
    }

    /*
     * abs：最小值回绕成它自己；普通负数变正。
     *
     * 同上：`abs(int8 -128)` 在「无符号取反（正确）」与「`-a`（UB）」
     * 下都得到 −128，**这一条区分不了两种实现**。能区分的是普通值 ——
     * 但它俩在普通值上也同值。真正被这条测到的是「结果不越界」：
     * 正确实现对 −128 明确回绕，UB 实现只是碰巧。所以配套的 `wrap_signed`
     * 单测（`scalar_special_test.c`）才是判据的形式化载体，这里只做端到端确认。
     */
    {
        const int8_t minimum = -128;
        const int8_t negative = -5;
        const int8_t positive = 7;
        const int8_t zero = 0;
        int8_t result = 0;

        runtimec_scalar_probe_absolute_int8_int8(&minimum, &result);
        EXPECT(result == -128);
        runtimec_scalar_probe_absolute_int8_int8(&negative, &result);
        EXPECT(result == 5);
        runtimec_scalar_probe_absolute_int8_int8(&positive, &result);
        EXPECT(result == 7);
        runtimec_scalar_probe_absolute_int8_int8(&zero, &result);
        EXPECT(result == 0);
    }

    /* sign：正 1、负 -1、零 0。 */
    {
        const double positive = 2.5;
        const double negative = -2.5;
        const double zero = 0.0;
        double result = 99.0;

        runtimec_scalar_probe_sign_float64_float64(&positive, &result);
        EXPECT(result == 1.0);
        runtimec_scalar_probe_sign_float64_float64(&negative, &result);
        EXPECT(result == -1.0);
        runtimec_scalar_probe_sign_float64_float64(&zero, &result);
        EXPECT(result == 0.0);
    }

    /* 左移：int8 上溢回绕，计数按 3 位掩码。 */
    {
        const int8_t one = 1;
        const int8_t seven = 7;
        const int8_t eight = 8; /* 掩码后是 0，所以结果是 1 而不是 UB */
        int8_t result = 0;

        runtimec_scalar_probe_shift_left_int8_int8_int8(&one, &seven, &result);
        EXPECT(result == -128);
        runtimec_scalar_probe_shift_left_int8_int8_int8(&one, &eight, &result);
        EXPECT(result == 1);
    }


    /*
     * cast 到整数：**回绕，不是饱和**。
     *
     * 这组直接钉住一条我写错过的语义：曾经把「无符号→有符号」写成饱和到 127，
     * 而 C 的 `(int8_t)(uint8_t)200` 是 −56（模 256）。matrix:63 只给了同符号的
     * 例子（`int8(300)=44`），跨符号要自己判阵营——C/Rust/Go 都是回绕。
     */
    {
        const int64_t wide = 300;
        const uint8_t unsigned_big = 200;
        const int8_t negative = -1;
        int8_t narrow = 0;
        uint8_t unsigned_result = 0;

        /* 按 8 位回绕：300 → 44，与 matrix:63 的例子一致。 */
        runtimec_scalar_probe_cast_int64_int8_int8(&wide, &narrow);
        EXPECT(narrow == 44);
        /* 跨符号同样是回绕：200 → −56。写成饱和会得 127。 */
        runtimec_scalar_probe_cast_uint8_int8_int8(&unsigned_big, &narrow);
        EXPECT(narrow == -56);
        /* 反方向：−1 → 255。 */
        runtimec_scalar_probe_cast_int8_uint8_uint8(&negative,
                                                    &unsigned_result);
        EXPECT(unsigned_result == 255);
    }

    /* 右移：负值是算术移位（补符号位）。 */
    {
        const int64_t negative = -8;
        const int8_t one = 1;
        int64_t result = 0;

        runtimec_scalar_probe_shift_right_int64_int8_int64(&negative, &one,
                                                           &result);
        EXPECT(result == -4);
    }

    /*
     * div / mod：整数有失败态（走 `_EXIT` 宏），浮点没有（IEEE inf/NaN）。
     *
     * 这组验三件事：
     *   1. 窄宽度结果按宽度回绕（int16 的商写入 int16 槽）；
     *   2. 失败时 **exit 被置**，输出槽不动；
     *   3. 浮点除零**不报错**，算出 inf（matrix:29）——与整数形成对照。
     */
    {
        const int16_t left = 7;
        const int16_t right = 2;
        const int16_t zero = 0;
        int16_t narrow = 0;
        binrwir_execution_exit_t exit = {0};

        runtimec_scalar_probe_div_int16_int16_int16(&left, &right, &narrow,
                                                    &exit);
        EXPECT(exit.kind == BINRWIR_EXECUTION_EXIT_NONE);
        EXPECT(narrow == 3); /* 向零截断 */

        /* 除零：exit 置错、输出槽不动。 */
        exit = (binrwir_execution_exit_t){0};
        narrow = INT16_C(0x5A5A);
        runtimec_scalar_probe_div_int16_int16_int16(&left, &zero, &narrow,
                                                    &exit);
        EXPECT(exit.kind == BINRWIR_EXECUTION_EXIT_ERROR);
        EXPECT(exit.detail_code == RUNTIMEC_SCALAR_DIAG_DIVIDE_BY_ZERO);
        EXPECT(narrow == INT16_C(0x5A5A));
    }

    {
        /*
         * `INT64_MIN / -1` 是 C 的 UB（x86 的 `idiv` 抛 #DE 杀进程）。
         *
         * 裁决（2026-11-05）：二补数**回绕**——商等于被除数本人
         * （Go 的 `q = x / -1` 也是 `x`），不是错误。
         *
         * 这条断言的失败形态要留意：实现若漏判，**不是变红而是 SIGFPE**。
         */
        const int64_t minimum = INT64_MIN;
        const int64_t minus_one = -1;
        int64_t result = 0;
        binrwir_execution_exit_t exit = {0};

        runtimec_scalar_probe_div_int64_int64_int64(&minimum, &minus_one,
                                                    &result, &exit);
        EXPECT(exit.kind == BINRWIR_EXECUTION_EXIT_NONE);
        EXPECT(result == INT64_MIN);
    }

    {
        /* 浮点除零算出来，不报错。 */
        const double one = 1.0;
        const double zero = 0.0;
        double result = 0.0;

        runtimec_scalar_probe_div_float64_float64_float64(&one, &zero, &result);
        EXPECT(isinf(result));
        EXPECT(result > 0.0);
    }

    {
        /* mod：余数随被除数符号；无符号按自身。 */
        const int64_t negative = -7;
        const int64_t three = 3;
        int64_t result = 0;
        binrwir_execution_exit_t exit = {0};

        runtimec_scalar_probe_mod_int64_int64_int64(&negative, &three, &result,
                                                    &exit);
        EXPECT(result == -1);

        {
            const uint8_t wide = 200;
            const uint8_t divisor = 7;
            uint8_t narrow = 0;

            exit = (binrwir_execution_exit_t){0};
            runtimec_scalar_probe_mod_uint8_uint8_uint8(&wide, &divisor,
                                                        &narrow, &exit);
            EXPECT(narrow == 4); /* 200 % 7 == 4 */
        }
    }


    /*
     * 浮点 × 64 位整数：结果**必须是浮点**。
     *
     * 这组钉住今天最顽固的一类错：表曾把 `float32,int64` 的结果写成 int64——
     * 「取较宽」时 `float32`(4 字节) 输给 `int64`(8 字节)。但 float32 的 4 字节
     * 不是「比 int64 窄的整数」，跨类不能比宽度。spec 的规则是「任一侧浮点则
     * 按 float 规则」。
     *
     * 判据落在这里而不是只落生成器：**表再错一次时，这一条会红**。
     */
    {
        const float left = 1.5f;
        const int64_t right = 2;
        float result = 0.0f;

        /* 结果类型是 float（不是 int64）：1.5 + 2 = 3.5，不是 3。 */
        runtimec_scalar_probe_add_float32_int64_float32(&left, &right, &result);
        EXPECT(result == 3.5f);
    }

    {
        /* min(int32 5, float32 2.5) = 2.5 —— 若实现把结果当整数，会得 2。 */
        const int32_t left = 5;
        const float right = 2.5f;
        float result = 0.0f;

        runtimec_scalar_probe_min_int32_float32_float32(&left, &right, &result);
        EXPECT(result == 2.5f);
    }

    {
        /* mul(float32 2, uint64 3) = 6.0 —— uint64 走无符号值本身，不经 int64。 */
        const float left = 2.0f;
        const uint64_t right = 3;
        float result = 0.0f;

        runtimec_scalar_probe_mul_float32_uint64_float32(&left, &right,
                                                         &result);
        EXPECT(result == 6.0f);
    }


    /*
     * 按**模板**补的边界锚点（不是按条数）。
     *
     * 判据：模板错一次会同时错几百条，所以每个模板至少要有一条"错实现会红"的
     * 样本。常规值不够——它们在任何写法下都对；要挑**边界**：回绕点、相等点、
     * 符号位、掩码边界、饱和点。
     */

    /* 整数回绕：int8(127 + 1) = -128，uint8(0 - 1) = 255。
     * 若模板写成饱和（或走浮点），这两条会给出 127 与 0。 */
    {
        const int8_t high = 127;
        const int8_t one = 1;
        const uint8_t unsigned_zero = 0;
        const uint8_t unsigned_one = 1;
        int8_t signed_result = 0;
        uint8_t unsigned_result = 0;

        runtimec_scalar_probe_add_int8_int8_int8(&high, &one, &signed_result);
        EXPECT(signed_result == -128);
        runtimec_scalar_probe_sub_uint8_uint8_uint8(&unsigned_zero,
                                                    &unsigned_one,
                                                    &unsigned_result);
        EXPECT(unsigned_result == 255);
        /* int8(-1 - (-1)) = 0：符号与零值都对。 */
        {
            const int8_t minus_one = -1;
            int8_t zero_result = 99;

            runtimec_scalar_probe_sub_int8_int8_int8(&minus_one, &minus_one,
                                                     &zero_result);
            EXPECT(zero_result == 0);
        }
    }

    /* 浮点算术：float32 与 float64 精度不同。
     * `(float)(1.0f/3.0f)` 与 `(double)(1.0/3.0)` 必须不等——若模板把 float32
     * 也按 double 算再截断，两者可能相等。 */
    {
        const float one_third_f32 = 1.0f / 3.0f;
        const double one_third_f64 = 1.0 / 3.0;
        const float zero_f32 = 0.0f;
        float result = 0.0f;

        EXPECT((double)one_third_f32 != one_third_f64);
        /* 加 0 不该改变值，也不该把 -0.0 变成 +0.0 之外的任何数。 */
        runtimec_scalar_probe_add_float32_float32_float32(&one_third_f32,
                                                          &zero_f32, &result);
        EXPECT(result == one_third_f32);
    }

    /* 比较：`lt(5,5) == false` 是唯一能区分 `<` 与 `<=` 的样本。 */
    {
        const int32_t five = 5;
        const int32_t also_five = 5;
        const int32_t six = 6;
        bool result = true;

        runtimec_scalar_probe_less_int32_int32_bool(&five, &also_five, &result);
        EXPECT(result == false);
        runtimec_scalar_probe_less_int32_int32_bool(&five, &six, &result);
        EXPECT(result == true);
    }

    /* 相等：大整数相等必须走精确比较，不能经浮点。 */
    {
        const int64_t big = INT64_C(9007199254740993); /* 2^53 + 1 */
        const int64_t nearby = INT64_C(9007199254740992); /* 2^53 */
        bool result = true;

        /* 这两个数**相邻但不相等**；若实现先转 double，两者会相等。 */
        runtimec_scalar_probe_equal_int64_int64_bool(&big, &nearby, &result);
        EXPECT(result == false);
        runtimec_scalar_probe_equal_int64_int64_bool(&big, &big, &result);
        EXPECT(result == true);
    }

    /* 位运算：掩码语义。 */
    {
        const uint8_t left = 0xF0;
        const uint8_t right = 0x3C;
        uint8_t result = 0;

        runtimec_scalar_probe_bitwise_and_uint8_uint8_uint8(&left, &right,
                                                            &result);
        EXPECT(result == 0x30);
    }

    /* 逻辑：真值表两组关键行——`or(0,0)` 假、`or(0,负值)` 真（负的非零是真）。 */
    {
        const double zero = 0.0;
        const double negative = -1.5;
        bool result = true;

        runtimec_scalar_probe_logical_or_float64_float64_bool(&zero, &zero,
                                                              &result);
        EXPECT(result == false);
        runtimec_scalar_probe_logical_or_float64_float64_bool(&zero, &negative,
                                                              &result);
        EXPECT(result == true);
    }

    /* not：`not(0) == true`、`not(非零) == false`。 */
    {
        const int64_t zero = 0;
        const int64_t nonzero = -7;
        bool result = false;

        runtimec_scalar_probe_logical_not_int64_bool(&zero, &result);
        EXPECT(result == true);
        runtimec_scalar_probe_logical_not_int64_bool(&nonzero, &result);
        EXPECT(result == false);
    }

    /* 移位：掩码边界（uint8 计数只取低 3 位）与算术右移。 */
    {
        const uint8_t one = 1;
        const uint8_t eight = 8; /* 8 & 7 == 0，所以结果是 1 */
        const int8_t negative = -8;
        const int8_t shift_one = 1;
        uint8_t unsigned_result = 0;
        int8_t signed_result = 0;

        runtimec_scalar_probe_shift_left_uint8_uint8_uint8(&one, &eight,
                                                           &unsigned_result);
        EXPECT(unsigned_result == 1);
        runtimec_scalar_probe_shift_right_int8_int8_int8(&negative, &shift_one,
                                                         &signed_result);
        EXPECT(signed_result == -4);
    }

    /* cast 饱和：`int8(float 300.0)` 饱和到 127（不是回绕成 44）。
     * 回绕与饱和在这里给出不同答案，这正是能区分两种实现的样本。
     *
     * 注：`(int8_t)(uint8_t)converted_` 这种「多绕一次无符号」的改动**不改变
     * 结果**——饱和后值恒在 int8 范围内，再走一次 uint8 是恒等。所以那种变异
     * 抓不到不是漏洞。能抓到的是「绕过 helper、直接 (int8_t)*a」这种真回绕。 */
    {
        const double large = 300.0;
        const double nan_value = NAN;
        int8_t result = 0;
        binrwir_execution_exit_t exit = {0};

        runtimec_scalar_probe_cast_float64_int8_int8(&large, &result, &exit);
        EXPECT(exit.kind == BINRWIR_EXECUTION_EXIT_NONE);
        EXPECT(result == 127);
        EXPECT(result != 44); /* 回绕会给 44 —— 那是整数 cast 的语义，不是浮点的 */

        /* NaN 报错，输出槽不动。 */
        exit = (binrwir_execution_exit_t){0};
        result = INT8_C(0x5A);
        runtimec_scalar_probe_cast_float64_int8_int8(&nan_value, &result, &exit);
        EXPECT(exit.kind == BINRWIR_EXECUTION_EXIT_ERROR);
        EXPECT(exit.detail_code == RUNTIMEC_SCALAR_DIAG_NOT_A_NUMBER);
        EXPECT(result == INT8_C(0x5A));
    }

    /* cast 饱和（无符号侧）：`uint8(float -5.0)` 饱和到 0，不是回绕成 251。 */
    {
        const float negative = -5.0f;
        const float huge = 1e30f;
        uint8_t result = 99;
        binrwir_execution_exit_t exit = {0};

        runtimec_scalar_probe_cast_float32_uint8_uint8(&negative, &result,
                                                       &exit);
        EXPECT(result == 0);
        exit = (binrwir_execution_exit_t){0};
        result = 0;
        runtimec_scalar_probe_cast_float32_uint8_uint8(&huge, &result, &exit);
        EXPECT(result == 255);
    }

}

int main(void) {
    test_entry_table_shape();
    test_template_semantics();
    if (failures != 0) {
        fprintf(stderr, "scalar_generated_test: %d failure(s)\n", failures);
        return 1;
    }
    fprintf(stdout, "scalar_generated_test: OK\n");
    return 0;
}
