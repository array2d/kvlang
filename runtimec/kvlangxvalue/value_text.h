#ifndef RUNTIMEC_CONTRACT_VALUE_TEXT_H
#define RUNTIMEC_CONTRACT_VALUE_TEXT_H

/*
 * 组件：kvlangxvalue/value_text
 * 作用：**值与文本之间唯一的转换点**——给定 langtype 与一段文本，产出 body 字节；
 *       反过来把 body 字节按 langtype 解释成文本。
 * 联系：只依赖 `kvlangxvalue/langtype`（判形态）、`char_codec`（编码转换）、
 *       `float_format`（浮点文本）。不访问存储、不分配。
 * 约束：**没有「成功但内容未知」这条路径**。不支持的类型一律失败，绝不打印十六进
 *       进制冒充成功——那会让调用方以为值被正确解释了。
 *
 * ── 为什么这一层必须只有一个实现 ──────────────────────────────────────
 *
 * 「一段文本 = 哪些字节」和 langtype 的形态分类是同一条规则的两种用法。写在 CLI 里
 * 就是第二份；写在两个 CLI 里就是第三份。分歧的表现是「同一个值，两个工具读出来不
 * 一样」，而两边都自认为对。
 *
 * ── 支持范围是**刻意窄**的 ────────────────────────────────────────────
 *
 * 只覆盖有**天然文本形态**的类型：整数、浮点、bool、`char/<enc>`、`[]char/<enc>`。
 * Ptr / map / struct / 定长数组**没有**唯一文本形态（`[3]int64` 写成 `1,2,3` 还是
 * `[1,2,3]`？没有共识就没有唯一答案），所以它们在这里**失败**，由调用方决定怎么
 * 呈现——而不是本组件猜一个格式再让所有人跟着用。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kvlangxvalue/view.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 转换失败的原因。**只有成功才有值**——`runtimec_value_text_of` 失败时也写它。 */
typedef enum runtimec_value_text_failure {
    RUNTIMEC_VALUE_TEXT_OK = 0,
    RUNTIMEC_VALUE_TEXT_BAD_LANGTYPE,      /* langtype 语法畸形 */
    RUNTIMEC_VALUE_TEXT_UNSUPPORTED_TYPE,  /* 形态合法但没有唯一文本形态 */
    RUNTIMEC_VALUE_TEXT_BAD_ESCAPE_FORMAT, /* 文本不是该类型的合法表示（如 "abc" 当 int64） */
    RUNTIMEC_VALUE_TEXT_OUT_OF_RANGE,      /* 数值超出该类型表示范围（如 300 当 int8） */
    RUNTIMEC_VALUE_TEXT_buffer_TOO_SMALL   /* 输出缓冲装不下；不截断 */
} runtimec_value_text_failure_t;

/*
 * 文本 → body。
 *
 * 成功返回 true，`*output_length` 是写进 `output` 的字节数；失败返回 false，
 * `*output_length` 置 0，`*failure` 说明原因。
 *
 * `output` 允许为 NULL —— 那是「只问要多少字节」的用法，此时 `output_capacity`
 * 必须为 0，成功时写 `*output_length` 为所需长度。
 *
 * 整数按小端定宽写；`bool` 写 1 字节 0/1；`char/utf32` 与 `[]char/utf32` 写码点
 * 序列（每码点 4 字节小端）；`char/utf8` 与 `[]char/utf8` 写 UTF-8 字节；
 * `char/ascii` 只接受 ASCII 输入，否则失败。
 */
bool runtimec_value_text_to_bytes(runtimec_string_view_t langtype,
                                  runtimec_string_view_t value_text,
                                  uint8_t *output, size_t output_capacity,
                                  size_t *output_length,
                                  runtimec_value_text_failure_t *failure);

/*
 * body → 文本。
 *
 * 成功返回 true，写 `*output_length`（不含结尾 NUL）；失败返回 false，
 * `*output_length` 置 0。
 *
 * **与 `to_bytes` 是严格互逆的**：任何 `to_bytes` 成功产出的 body，`to_text` 都能
 * 读回同一段文本（浮点除外——浮点走最短往返表示，读回的是同一个**值**，文本可能
 * 与输入不同，如输入 `3.140` 读回 `3.14`）。
 *
 * `output` 为 NULL 时与 `to_bytes` 同义：只问长度。
 */
bool runtimec_value_text_to_text(runtimec_string_view_t langtype,
                                 runtimec_bytes_view_t body,
                                 char *output, size_t output_capacity,
                                 size_t *output_length,
                                 runtimec_value_text_failure_t *failure);

/* 失败原因的可读文本。用于命令行报错；不带句号，调用方自己加。 */
const char *runtimec_value_text_failure_text(runtimec_value_text_failure_t failure);

#ifdef __cplusplus
}
#endif

#endif
