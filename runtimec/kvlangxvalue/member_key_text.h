#ifndef RUNTIMEC_CONTRACT_MEMBER_KEY_TEXT_H
#define RUNTIMEC_CONTRACT_MEMBER_KEY_TEXT_H

/*
 * 组件：kvlangxvalue/member_key_text
 * 作用：值视图 → **成员键文本**。逐字铁律的唯一实现。
 * 联系：`kvlangxvalue/langtype` 判形态，`kvlangxvalue/float_format` 供核心算法，
 *       `kvlangxvalue/char_codec` 供编码转换；本组件不依赖任何上层组件、不访问存储。
 * 约束：纯函数、不分配；缓冲装不下就报所需长度，绝不截断。
 *
 * ── 为什么必须有唯一实现 ──────────────────────────────────────────────
 *
 * spec `03-类型系统/07-文法与合法性.kv`：成员访问的键**就是 memitemkey 值按其
 * langtype 的字面量本身**（逐字铁律）。所以「值 → 键文本」只许有一处实现——
 * 两处就会漂开，而漂开的表现是**同一个值有两个键**，容器里凭空多出成员。
 *
 * ── 浮点：键层规则，不是显示层规则 ────────────────────────────────────
 *
 * spec `05-runtime语义/03-成员访问.kv`「浮点文本」把浮点文本分成**一个核心算法
 * 与两种呈现层**。本组件实现的是**键层**：
 *
 *   | 方面 | 键层（本文件） | 显示层（`float_format`） |
 *   |---|---|---|
 *   | 数字串 | 最短往返 | 同左（共享核心算法） |
 *   | 指数写法 | `< -4` 或 `>= 16`，补两位 | 同左 |
 *   | 整数值 | **不带 `.0`**：`1.0 → "1"` | 带 `.0`：`1.0 → "1.0"` |
 *   | 负零 | **归一** `"0"` | 保留符号 `"-0.0"` |
 *   | 非有限 | NaN → 错误；±Inf → `"inf"`/`"-inf"` | 一律输出文本 |
 *
 * 键层必须归一负零、去掉 `.0`，因为**键相等性不变量**要求 `a == b` 为真时
 * `a`、`b` 落到同一个键：`-0.0 == 0.0`、`1 == 1.0`。
 *
 * 三层分叉（`.0`、负零、非有限）**各自实现**，因为它们的目标不同（等值性 vs
 * 可读性）；但数字串与指数**共享** `runtimec_float_format_core`——否则会出现
 * 「键写 `1e-05`、显示写 `0.00001`」这类无谓分歧。
 *
 * 指数阈值**不复用显示层的常量**：今天数值相同（`-4`、`16`），但判据不同源。
 * 共享常量会让「哪天显示层改阈值」把键层的可观察行为一起带走，而键文本是
 * 用户能直接看到的存储结构。
 *
 * ── 容量：C `snprintf` 模型 ───────────────────────────────────────────
 *
 * 三条单位约定，逐条可检验：
 *
 *   1. `capacity` 的单位是**可写字节数，含结尾 NUL**；
 *   2. `*output_length` 两条路径**都不含 NUL**：
 *      成功 = 文本长度；`INSUFFICIENT_CAPACITY` = 所需文本长度；
 *   3. 调用方分配时**恒为 `required + 1`**；成功返回的 `output_length`
 *      可直接用于切片/比较，不必减一。
 *
 * 这是 C 的形态（「调用方给缓冲」只有 C 有），五语言矩阵在这一格分裂，跟 C 走。
 *
 * ── 失败语义 ─────────────────────────────────────────────────────────
 *
 * 除 `INSUFFICIENT_CAPACITY` 外，失败一律 `*output_length = 0` 且 `output`
 * **一个字节都不写**（调用方缓冲里可能还有旧内容，抹一半比不抹更难查）。
 * `INSUFFICIENT_CAPACITY` 是唯一例外：它必须回报所需长度，否则调用方无从重试。
 *
 * ── 不接受元组 ───────────────────────────────────────────────────────
 *
 * `[scalar,…]` 元组**不是值类型**，所以它不是本函数的入参形态：元组只存在于
 * **key 位置**，其文本由 parser 从源码原样产出（`layout/src/parser.rs:1743-1760`），
 * 从不经「值 → 文本」转换。给它写一个分支就是为不存在的调用方写代码。
 *
 * 若将来「元组是一等值」成立，那是**语言变更**（要改 `valid_shape`、要生产者、
 * 要存储形态），不是在这里补一个分支。届时再实现。
 */

#include <stddef.h>
#include <stdint.h>

#include "kvlangxvalue/view.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 失败成因，经 `error.code` 报出。
 *
 * 放头文件而不是 .c：测试必须能引用具体码值做断言，并证明不同成因给出
 * **不同**码——只断言「失败」时，把元组错报成「不支持形态」照样绿。
 *
 * 编号只增不改：已发出的码会出现在日志里。
 */
typedef enum runtimec_member_key_text_diagnostic {
    /* 请求形状不合法（空 langtype、output/output_length 为 NULL、langtype 畸形） */
    RUNTIMEC_MEMBER_KEY_TEXT_DIAG_INVALID_REQUEST = 1,
    /* 缓冲放不下；`*output_length` 已给出所需文本长度 */
    RUNTIMEC_MEMBER_KEY_TEXT_DIAG_INSUFFICIENT_CAPACITY = 2,
    /* 该 langtype 不能作成员键（元组、形状、未识别形态） */
    RUNTIMEC_MEMBER_KEY_TEXT_DIAG_UNSUPPORTED_LANGTYPE = 3,
    /* body 长度与声明的类型对不上 */
    RUNTIMEC_MEMBER_KEY_TEXT_DIAG_MALFORMED_BODY = 4,
    /* 浮点键是 NaN（±Inf 合法，不报此码） */
    RUNTIMEC_MEMBER_KEY_TEXT_DIAG_NON_FINITE = 5
} runtimec_member_key_text_diagnostic_t;

/*
 * 值 → 成员键文本。
 *
 * `langtype` 是**值槽的完整 langtype 文本**（含 `[]`、`[N]` 维度前缀）。
 * 调用方不剥前缀：剥法是 `kvlangxvalue/langtype` 的一部分，各写一遍就是第二条规则。
 *
 * `body` 是值体的只读字节视图（小端定宽标量、字符串原样）。
 *
 * 成功返回 `SUCCESS`、写 `*output`（NUL 结尾）与 `*output_length`（不含 NUL）。
 * 失败见头注释「失败语义」；`error` 可为 NULL，非 NULL 时被置为带 `code` 的错误。
 *
 * 空 body 是**合法的空串键**（`output_length = 0`），不是畸形：字符串没有
 * 「应有的宽度」，`MALFORMED_BODY` 只对声明定宽的类型有意义。空串键落到存储
 * 即 `base·`，是逐字铁律的直接后果——本函数**不归一化、不拒绝**，也不替
 * 存储层做合法性判断。
 *
 * ── 本函数不判长度上界，调用方必须判 ──────────────────────────────────
 *
 * 存储能表达的**完整 key**（父路径 + `·` + 本函数的输出）上界是 **1023 字节**。
 * 这个数字有两个来源，缺一不可：
 *
 *   1. `runtimec/contract/kvspace_access.c` 的 `RUNTIMEC_KVSPACE_KEY_CAPACITY`
 *      = 1024，那是**栈缓冲尺寸**——键文本加结尾 NUL 要装进它；
 *   2. `kvspace-c/src/kvspace.c` 的 Get / Set 两条路径都对 `strlen(key) >= 1024`
 *      做**语义拒收**。
 *
 * 所以 1023 是「存储层能表达的上界」，不是「我们碰巧用了这么大的缓冲」。
 *
 * **为什么不在本函数里判**：键文本本身（比如一段 2000 字节的 UTF-8 字符串）
 * 是合法的值→文本转换，它只是**装不进这个存储的键**。上界是「拼接后的完整 key」
 * 的属性，只有调用方知道父路径多长。本函数把长度如实回报，调用方按
 * `VMBIN_HANDLER_CALL_DIAG_KEY_TOO_LONG` 失败封闭——**绝不截断**：截断会写到
 * 一个看起来像、实际不是的键上。
 */
void runtimec_member_key_text(runtimec_string_view_t langtype,
                              runtimec_bytes_view_t body, char *output,
                              size_t capacity, size_t *output_length);

#ifdef __cplusplus
}
#endif

#endif
