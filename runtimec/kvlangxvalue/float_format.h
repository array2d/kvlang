#ifndef RUNTIMEC_CONTRACT_FLOAT_FORMAT_H
#define RUNTIMEC_CONTRACT_FLOAT_FORMAT_H

/*
 * 组件：kvlangxvalue/float_format
 * 作用：浮点值的**唯一**文本化实现——最短往返表示。
 * 联系：`vmbin` 的打印算子与一切需要把浮点写成文本的地方都调用这里，只有一份实现。
 * 约束：纯函数、不分配、不访问存储；缓冲装不下就失败，绝不截断。
 *
 * ── 完整契约（名字只说了一半，另一半在这里）────────────────────────────
 *
 * 函数名叫 `shortest`，但「最短往返」四个字**不包含**下面四件事。它们同样是
 * 契约的一部分，读的人必须在这一段里拿到全部规则，不必去读实现：
 *
 * 1. **按值本身的宽度定往返域。**
 *    `float32` 用 `strtof` 判往返，`float64` 用 `strtod`。
 *
 *    为什么不能统一用 double 域：`float32(3.4028235e38)` 作为 double 是
 *    `3.4028234663852886e+38`；若按 double 判往返，会一路加到 17 位，打出
 *    **那些在被截断成 float32 后根本不存在的尾数**——对读数的人毫无意义。
 *    按宽度定域时它停在 8 位：`3.4028235e+38`。
 *
 * 2. **整数形态补 `.0`。**
 *    `12.0` 打成 `12.0` 而不是 `12`。判据是**值的 kind 是浮点**，不是事后看
 *    字符串像不像整数。理由：`12` 会被读成 int64，读者分不出值的种类。
 *
 * 3. **`-0.0` 保留符号位。**
 *    `-0.0` 与 `0.0` 的位不同，打印要能区分。补 `.0` 之后是 `-0.0`，
 *    不会与整数 `0` 混淆。
 *
 * 4. **非有限值是固定串，不走 `%g`。**
 *    `nan` → `"nan"`，`inf` → `"inf"`，`-inf` → `"-inf"`。
 *
 *    **理由是确定性，不是「跟某个语言一致」。** IEEE 754 里 NaN 的符号位是
 *    **未指定**的：`0.0/0.0` 在不同平台、不同优化级别下 `printf("%g")` 可能
 *    给出 `nan` 也可能给出 `-nan`。那违反「同输入同输出」——而这类不确定性
 *    **测不出来**，因为两次跑恰好相同是常态。所以这里不走格式化库，直接写死。
 *
 *    对称地：`%g` 对 `inf` 恰好能读回，但同样不用它——判据是「非有限值不做
 *    十进制往返」，比「inf 恰好能往返」更稳。
 *
 * ── 阵营 ──────────────────────────────────────────────────────────────
 *
 * 「最短往返 + 整数形态保留 `.0`」= **Python `repr`**。实测对照：
 *
 *   | 值     | Python repr | JS String()  | 本实现 |
 *   |--------|-------------|--------------|--------|
 *   | 12.0   | `12.0`      | `12`         | `12.0` |
 *   | 1e20   | `1e+20`     | `1000…000`   | `1e+20`|
 *   | -0.0   | `-0.0`      | `0`          | `-0.0` |
 *   | nan    | `nan`       | `NaN`        | `nan`  |
 *
 * JS 虽然也是最短往返，但上面四处与 `.0` 规则、符号位规则都不同，**不是同阵营**。
 * 所以阵营是 **Python 一个**，不是「Python/Go/JS/Rust 四家」。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 往返域：决定用哪种类型判「读回来还是同一个值」。 */
typedef enum runtimec_float_domain {
    RUNTIMEC_FLOAT_DOMAIN_FLOAT32 = 0, /* strtof */
    RUNTIMEC_FLOAT_DOMAIN_FLOAT64 = 1  /* strtod */
} runtimec_float_domain_t;

/*
 * 把一个浮点值写成文本。
 *
 * `value` 恒以 `double` 传入：float32 的每个值都能精确表示为 double，所以窄化
 * 不引入舍入；**往返域由 `domain` 决定**，不由参数类型决定。
 *
 * 成功返回 true 并写 `*output_length`（不含结尾 NUL）；缓冲不足返回 false 且
 * `*output_length` 置 0——**不截断**（截断会产出一个看起来正常、读回去却是
 * 另一个值的串）。
 */
bool runtimec_float_format_shortest(double value, runtimec_float_domain_t domain,
                                    char *output, size_t capacity,
                                    size_t *output_length);

/*
 * ── 核心算法出口（呈现层之间的唯一共享点）────────────────────────────
 *
 * 浮点文本有**一个核心算法**与**两种呈现层**（spec `05-runtime语义/03-成员访问.kv`
 * 的「浮点文本」节）：
 *
 *   | 呈现层 | 整数值 | 负零 | 非有限 |
 *   |---|---|---|---|
 *   | 成员键 | 不带 `.0` | 归一 `"0"` | NaN 报错、±Inf 成键 |
 *   | 显示   | 带 `.0`   | 保留符号 | 一律输出文本 |
 *
 * 两层**必须共用核心算法**（最短往返数字串 + 十进制指数），否则会出现
 * 「键写 `1e-05`、显示写 `0.00001`」这类无谓分歧；**但呈现规则各自实现**——
 * 上面三处分叉由用途决定，不是两套算法。
 *
 * 这两个函数就是那个共享核心。它们不做任何呈现决定（不补 `.0`、不归一负零、
 * 不判非有限值该不该报错），只回答「数字串与指数是什么」和「非有限值的固定串
 * 是什么」。
 */
bool runtimec_float_format_core(double value, runtimec_float_domain_t domain,
                                char *digits, size_t digits_capacity,
                                int *exponent, bool *negative);

/*
 * 非有限值的固定串：`nan` / `inf` / `-inf`。有限值返回 false。
 *
 * 键层写 `"inf"`/`"-inf"` **必须**调这个，不许自己拼字面量——否则同一个串
 * 有两处真相。NaN 该不该接受由调用方（呈现层）决定：显示层输出 `nan`，
 * 键层判定为不合法。本函数不替它们做这个决定。
 */
bool runtimec_float_format_non_finite_text(double value, char *output,
                                           size_t capacity,
                                           size_t *output_length);

#ifdef __cplusplus
}
#endif

#endif
