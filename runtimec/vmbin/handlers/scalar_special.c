/*
 * 组件：vmbin/handlers/scalar_special 的实现
 * 作用：模板表达不了、必须有人读一遍的那些特化标量 handler。
 * 联系：`scalar_generated.c` 由生成器产出一次运算；本文件是它的对立面——
 *       每一条都带一个语义判据（除零、溢出、NaN、饱和），所以手写。
 * 约束：
 *   - **会失败的 helper 直接收 `exit *`，成功才写 `out`，不返回哨兵**
 *     （编码器侧统一约定：哨兵一旦出现在输出槽里，错误就伪装成值）；
 *   - 逻辑函数因此只剩一行调用；判据全部在 helper 里；
 *   - 不写 return status：失败由 `runtimec_scalar_fail` 置 exit，成功路径无 return。
 *
 * 本批六条（判据出处见各函数注释）：
 *   min/max 浮点       matrix:69 floating-min-max-nan
 *   div 整数           matrix:22 signed-integer-division + 同余溢出
 *   mod 整数           matrix:68 integer-remainder-sign + 同余溢出
 *   cast 浮点→整数     matrix:21 floating-to-integer-truncation（饱和 + NaN 报错）
 *   cast 整数→窄整数   matrix:63 cast-target-kind（窄化是回绕，不是饱和）
 *   cast 整数→float32  matrix:27 int-to-float32-single-rounding（单次舍入）
 */

#include "vmbin/handlers/scalar_special.h"

#include <math.h>
#include <stdint.h>

/*
 * 浮点 min/max：NaN 让位于数，两个 NaN 得 NaN，与操作数顺序无关（matrix:69）。
 *
 * 写成 `a < b ? a : b` 在 NaN 参与时是错的：C 的比较遇 NaN 恒假，于是两个顺序
 * 会给出不同答案——那正是 matrix:69 要禁止的「依赖顺序」。
 */
double runtimec_scalar_float_min(double left, double right) {
    if (isnan(left)) {
        return right;
    }
    if (isnan(right)) {
        return left;
    }
    return left < right ? left : right;
}

double runtimec_scalar_float_max(double left, double right) {
    if (isnan(left)) {
        return right;
    }
    if (isnan(right)) {
        return left;
    }
    return left > right ? left : right;
}

/*
 * 整数除法的两个失败态。`width` 是**操作数宽度**（8/16/32/64）。
 *
 *   - 除数为零 → DIVIDE_BY_ZERO；
 *   - 最小可表示值 / -1 → DIVIDE_OVERFLOW：C 里是未定义行为，x86 上直接
 *     SIGFPE（进程没了，不是值错了）。
 *
 * 两者分开是因为修法不同：前者拒除数，后者拒那一对具体取值。
 * 返回值经由 `output_value`，只在成功时写——失败时输出槽一个字节都不动。
 */
void runtimec_scalar_divide_signed(int64_t left, int64_t right,
                                   uint32_t width, int64_t *output_value,
                                   binrwir_execution_exit_t *output_exit) {
    const int64_t minimum = -(INT64_C(1) << (width - 1U));

    if (right == 0) {
        runtimec_scalar_fail(output_exit, RUNTIMEC_SCALAR_DIAG_DIVIDE_BY_ZERO);
        return;
    }
    if (left == minimum && right == -1) {
        /*
         * 二补数回绕：`INT64_MIN / -1` 得 `INT64_MIN`（架构师裁决 2026-11-05，
         * Go 规范「`q = x / -1` 等于 `x`」）。
         *
         * **这不是可选的处理**：C 在这里是未定义行为，x86 的 `idiv` 直接抛 #DE
         * 杀掉进程。所以必须先判，且结果是定义好的回绕值，不是错误。
         */
        *output_value = minimum;
        return;
    }
    *output_value = left / right;
}

void runtimec_scalar_remainder_signed(int64_t left, int64_t right,
                                      uint32_t width, int64_t *output_value,
                                      binrwir_execution_exit_t *output_exit) {
    const int64_t minimum = -(INT64_C(1) << (width - 1U));

    if (right == 0) {
        runtimec_scalar_fail(output_exit, RUNTIMEC_SCALAR_DIAG_DIVIDE_BY_ZERO);
        return;
    }
    if (left == minimum && right == -1) {
        /* 二补数回绕：`INT64_MIN % -1` 得 0（同一条裁决）。数学上余数确实是 0，
         * 但 C 不定义这个表达式，`idiv` 同样会抛 #DE。 */
        *output_value = 0;
        return;
    }
    *output_value = left % right;
}

void runtimec_scalar_divide_unsigned(uint64_t left, uint64_t right,
                                     uint64_t *output_value,
                                     binrwir_execution_exit_t *output_exit) {
    if (right == 0U) {
        runtimec_scalar_fail(output_exit, RUNTIMEC_SCALAR_DIAG_DIVIDE_BY_ZERO);
        return;
    }
    *output_value = left / right;
}

void runtimec_scalar_remainder_unsigned(uint64_t left, uint64_t right,
                                        uint64_t *output_value,
                                        binrwir_execution_exit_t *output_exit) {
    if (right == 0U) {
        runtimec_scalar_fail(output_exit, RUNTIMEC_SCALAR_DIAG_DIVIDE_BY_ZERO);
        return;
    }
    *output_value = left % right;
}

/*
 * 浮点 → 有符号整数（matrix:21）：向零截断；越界与 ±Inf **饱和**到目标宽度；
 * NaN 是错误，不产出数值。
 *
 * 先按 `double` 的比较判边界再转型，绝不「先转 int64 再夹」：`1e300` 转 int64
 * 本身是 UB（C 未定义），而那正是这一步要避开的东西。
 */
void runtimec_scalar_float_to_signed(double source, uint32_t width,
                                     int64_t *output_value,
                                     binrwir_execution_exit_t *output_exit) {
    const int64_t maximum = (int64_t)((UINT64_C(1) << (width - 1U)) - 1U);
    const int64_t minimum = -(INT64_C(1) << (width - 1U));

    if (isnan(source)) {
        runtimec_scalar_fail(output_exit, RUNTIMEC_SCALAR_DIAG_NOT_A_NUMBER);
        return;
    }
    if (source >= (double)maximum) {
        *output_value = maximum;
        return;
    }
    if (source <= (double)minimum) {
        *output_value = minimum;
        return;
    }
    *output_value = (int64_t)source;
}

/* 浮点 → 无符号整数（matrix:21）：同一套判据，下界是 0。 */
void runtimec_scalar_float_to_unsigned(double source, uint32_t width,
                                       uint64_t *output_value,
                                       binrwir_execution_exit_t *output_exit) {
    const uint64_t maximum =
        width == 64U ? UINT64_MAX : ((UINT64_C(1) << width) - 1U);

    if (isnan(source)) {
        runtimec_scalar_fail(output_exit, RUNTIMEC_SCALAR_DIAG_NOT_A_NUMBER);
        return;
    }
    if (source >= (double)maximum) {
        *output_value = maximum;
        return;
    }
    if (source <= 0.0) {
        *output_value = 0U;
        return;
    }
    *output_value = (uint64_t)source;
}

/*
 * 整数 → 窄整数（matrix:63）：**回绕**，不是饱和。
 *
 * `int8(300)` 是 44——先截到目标宽度再按补码解释。写成「先夹再转」会得到 127，
 * 那是另一个语义。
 */
int64_t runtimec_scalar_wrap_signed(int64_t source, uint32_t source_width,
                                    uint32_t target_width) {
    const uint32_t shift = 64U - target_width;

    (void)source_width;
    /* 先左移把高位挤掉，再算术右移回来：符号扩展由编译器负责，没有 UB。 */
    return (int64_t)((uint64_t)source << shift) >> shift;
}

uint64_t runtimec_scalar_wrap_unsigned(uint64_t source,
                                       uint32_t target_width) {
    return target_width == 64U ? source
                               : (source & ((UINT64_C(1) << target_width) - 1U));
}

/*
 * 跨符号比较：`int64` 与 `uint64` 的**精确数学值**比较（matrix:28
 * mixed-signed-unsigned-arithmetic 后半句：算术拒绝混合符号，比较按精确值）。
 *
 * ── 为什么不能两边都转 int64 ─────────────────────────────────────────
 *
 * `uint64` 的值一旦超过 `INT64_MAX`，转成 `int64` 就是**负数**：
 *
 *     (int64_t)UINT64_MAX == -1        →  -1 <  1  读成「UINT64_MAX 比 1 小」
 *     (int64_t)UINT64_MAX == -1        →  -1 == -1 读成「UINT64_MAX 等于 -1」
 *
 * 两个结论都反了，而且**多数用例仍然通过**——只有跨过 2^63 的取值才暴露。
 * 这类 bug 在测试里极难抓到，因为常规样本都在区间内。
 *
 * ── 正确写法：先判符号，再比值 ───────────────────────────────────────
 *
 * 负数恒小于任何无符号数；非负的有符号数转 `uint64` 是保值的，之后同域比较。
 * 这样每个分支都只做**定义明确**的转换，不依赖任何"更宽的域"。
 *
 * 备选方案 `__int128` 也可行，但它是编译器扩展：C11 没有它，MSVC 也没有。
 * 判符号的做法是纯标准 C，且在每个分支上都能一句话说清为什么对。
 */
int runtimec_scalar_compare_signed_unsigned(int64_t signed_value,
                                            uint64_t unsigned_value) {
    if (signed_value < 0) {
        return -1;
    }
    return (uint64_t)signed_value < unsigned_value
               ? -1
               : ((uint64_t)signed_value > unsigned_value ? 1 : 0);
}

int runtimec_scalar_compare_unsigned_signed(uint64_t unsigned_value,
                                            int64_t signed_value) {
    return -runtimec_scalar_compare_signed_unsigned(signed_value,
                                                    unsigned_value);
}

/*
 * 移位（matrix:71 shift-count-masking）：计数按**操作数宽度减一**掩码。
 *
 * C 里「移位数 >= 宽度」是未定义行为——x86 的 `shl` 实际只取低 6 位（64 位操作
 * 数），ARM 上取低 8 位，而编译器有权假定它不发生并据此优化。matrix 选 C/Go 阵营：
 * 明确掩码，结果确定。
 *
 * **负计数：矩阵未定，本实现跟 C，且这是需要 spec 补一条的地方。**
 * matrix:71 只写「按宽度减一掩码」，没说计数是负数时怎么办。C 把计数当无符号
 * 参与掩码，于是 `shl(int64 1, −1)` 在 64 位下等于移 63 位（结果 `INT64_MIN`），
 * 这不是笔误而是二补数的直接后果；Go 的**常量**移位另行报错、**变量**移位在
 * 运行时是同一套掩码。本实现跟 C：先按操作数类型转无符号，再掩码。
 *
 * 这条没有明文的风险是：将来有人觉得「负计数应该报错」，改掉之后
 * `shl(x, -1)` 从 `INT64_MIN` 变成错误——那是语言行为变更，必须先在 spec 上定。
 * 若 spec 决定报错，改动点是这四条 helper 各加一条判据，不是调用方。
 *
 * 右移的语义按**操作数的符号性**分：有符号是算术右移（补符号位），无符号是
 * 逻辑右移（补 0）。C 的 `>>` 对负值是实现定义，但 GCC/Clang/MSVC 一致做算术
 * 右移，且矩阵要求「有符号算术、无符号逻辑」——所以这里**显式分开写**，不靠
 * 编译器默认。
 */
int64_t runtimec_scalar_shift_left_signed(int64_t source, int64_t count,
                                          uint32_t width) {
    const uint64_t mask = (uint64_t)width - 1U;
    /* 计数按**操作数宽度**先回绕再掩码：`int8` 的 −1 是 255，掩码得 7，
     * 直接 `(uint64_t)(-1) & 7` 也恰好得 7——两条路在二进制补码下同值。 */
    const uint64_t shifted =
        (uint64_t)source << ((uint64_t)count & mask);
    return runtimec_scalar_wrap_signed((int64_t)shifted, 64U, width);
}

uint64_t runtimec_scalar_shift_left_unsigned(uint64_t source, uint64_t count,
                                             uint32_t width) {
    const uint64_t mask = (uint64_t)width - 1U;
    return runtimec_scalar_wrap_unsigned(source << (count & mask), width);
}

int64_t runtimec_scalar_shift_right_signed(int64_t source, int64_t count,
                                           uint32_t width) {
    const uint64_t mask = (uint64_t)width - 1U;
    /* 先掩码回宽度内的值，再算**宽度内**的算术右移：直接对 int64 右移会让
     * 一个 int8 值带着 int64 的符号位走，结果与「在 8 位里算」不同。 */
    const int64_t narrowed =
        runtimec_scalar_wrap_signed(source, 64U, width);
    const uint64_t shift = (uint64_t)count & mask;

    if (shift == 0U) {
        return narrowed;
    }
    /* 在 int64 域里做算术右移不安全（负值是实现定义），所以手动补符号位：
     * 有效位宽 `width` 内的逻辑右移，再按最高位决定是否填 1。 */
    {
        const uint64_t raw = (uint64_t)narrowed & runtimec_scalar_wrap_unsigned(
                                                     UINT64_MAX, width);
        const uint64_t logical = raw >> shift;
        const uint64_t sign_bit =
            (raw >> (width - 1U)) & UINT64_C(1);
        const uint64_t extension =
            sign_bit != 0U
                ? (runtimec_scalar_wrap_unsigned(UINT64_MAX, width) <<
                   (width - shift))
                : 0U;
        return runtimec_scalar_wrap_signed(
            (int64_t)runtimec_scalar_wrap_unsigned(logical | extension, width),
            64U, width);
    }
}

uint64_t runtimec_scalar_shift_right_unsigned(uint64_t source, uint64_t count,
                                              uint32_t width) {
    const uint64_t mask = (uint64_t)width - 1U;
    const uint64_t narrowed = runtimec_scalar_wrap_unsigned(source, width);

    return narrowed >> (count & mask);
}

/*
 * ── 「按精确数学值比较」在浮点域的边界 ─────────────────────────────────
 *
 * spec 写的是「跨类混合由 runtime **按精确数学值**比较」（`11-指令派发与特化.kv`
 * 的 `eq neq lt le gt ge` 一行），matrix:28 也是同一句。这句话在**整数之间**
 * 完全成立（`signed ↔ unsigned` 由上面的 helper 精确保住），但**一到浮点就不成立**：
 *
 *     int64 → double 在 |v| > 2^53 时丢精度
 *
 *     (double)9007199254740993 == 9007199254740992.0
 *     于是 `9007199254740993 == 9007199254740992.0` 被判为**相等**
 *
 * 这不是本实现的疏漏，是 C 的通常算术转换（usual arithmetic conversions）既定语义：
 * int 与 double 相遇时 int 先转 double。五语言里 C、Go、Rust 做同一件事；
 * Python 与 JS 的整数本来就是浮点或任意精度，不构成对照。
 *
 * 所以这里的实现**照 C 走**（阵营已定，见 `five-language-behavior-matrix.md` 的
 * `mixed-precision-promotion` 与 p7 的「分裂时选定阵营」），代价是 spec 那句
 * 「精确数学值」在浮点参与时不精确——那是文档需要收窄的措辞，不是代码需要偏离
 * C 的理由。反过来「用十进制精确比较 int 与 double」会造出一个**没有任何主流语言
 * 有**的语义，正是 p7 要避免的那种偏离。
 *
 * 判据：**整数对整数精确，任一侧浮点则按 double 域**。这与结果宽度规则同源。
 */

/*
 * 目标 `bool` 的 cast（matrix:35 bool-cast-strict）：**只接受 bool 操作数**。
 *
 * 其余 13 种一律 InvalidKind——不是「非零即真」。这条与逻辑算子（matrix:78
 * 数值非零即真）方向相反，正是它们必须分开成两族的原因：`and`/`or` 接受数值，
 * `bool(x)` 不接受。转数值要写成显式比较 `x != 0`。
 *
 * 本函数的 `source_is_bool` 由**元组**决定，不在运行期判类型——编码期已经知道
 * 源是什么种类，走不到这里的组合根本不会生成条目。
 */
void runtimec_scalar_cast_to_bool(bool source, bool *output_value,
                                  binrwir_execution_exit_t *output_exit) {
    if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) {
        /* 前置判据已经报过错（非 bool 源），不覆盖它的码。 */
        return;
    }
    *output_value = source;
}

/*
 * 非 bool 源 → bool：一律失败。单独成函数而不是在模板里判，是因为它的
 * 判据是「种类」，而种类在编码期就定了——这条函数的存在意义是记录「此路不通」，
 * 不是运行期分派。
 */
void runtimec_scalar_cast_to_bool_invalid(binrwir_execution_exit_t *output_exit) {
    runtimec_scalar_fail(output_exit,
                         RUNTIMEC_SCALAR_DIAG_OPERAND_NOT_NUMERIC);
}
