#ifndef RUNTIMEC_CONTRACT_CHAR_CODEC_H
#define RUNTIMEC_CONTRACT_CHAR_CODEC_H

/*
 * 组件：contract/char_codec
 * 作用：字符编码之间的唯一转换点——目前是 char/utf32（码点序列）→ char/utf8。
 * 联系：vmbin 的打印与字符串算子（concat、streq）都调用这里，**只有一份实现**。
 * 约束：纯函数、不分配、不访问存储；缓冲装不下就失败，绝不截断（截断会产出
 *       半条 UTF-8 序列，看起来正常、实际是错的）。
 *
 * 放在 contract 层：`binrwir` 与 `vmbin` 都依赖 contract，两边都能用，不会形成
 * 反向依赖。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 一个 UTF-8 编码后的码点最多 4 字节。 */
#define RUNTIMEC_CHAR_CODEC_UTF8_MAX_BYTES 4U

/*
 * 把码点序列（每 4 字节一个 uint32 小端）编码成 UTF-8。
 *
 * - 非法码点（> U+10FFFF）或输入长度不是 4 的倍数 → 返回 false，输出长度置 0；
 * - 缓冲不足 → 返回 false，输出长度置 0；
 * - 成功 → 返回 true 并写入 `output_length`（不含结尾，不写 NUL）。
 *
 * 关于 `output_capacity` 的 `+1`：**这是调用方（`temp_println`）原有的缓冲约定，
 * 不是编码器的语义。** 本函数要求 `output_capacity > 输出长度`，即保留末字节不写，
 * 因为它的第一个调用方按「文本缓冲总要留一个位置」分配。**调用方若不需要末尾预留，
 * 传 `capacity + 1` 即可**——编码器本身不占用那个字节，也不会写入它。
 */
bool runtimec_char_utf32_to_utf8(const uint8_t *input, size_t input_size,
                                 char *output, size_t output_capacity,
                                 size_t *output_length);

/*
 * char/utf8 → char/utf32（码点序列，每 4 字节一个 uint32 小端）。
 *
 * `output_capacity` 是**字节容量**，必须是 4 的倍数；输出字节数写入
 * `output_length`（= 码点数 × 4）。
 *
 * 非法 UTF-8（截断、超长编码、代理区码点、> U+10FFFF）一律返回 false 且输出
 * 长度置 0——**不做替换字符**：替换会把畸形输入变成「看起来正常」的文本，
 * 调用方再也分不出它原本是坏的。
 */
bool runtimec_char_utf8_to_utf32(const uint8_t *input, size_t input_size,
                                 uint8_t *output, size_t output_capacity,
                                 size_t *output_length);

/*
 * char/ascii → char/utf8：ASCII 的每个字节就是一个码点，直接原样拷贝。
 * 非 ASCII 字节（>= 0x80）返回 false——它不是合法 ASCII。
 */
bool runtimec_char_ascii_to_utf8(const uint8_t *input, size_t input_size,
                                 char *output, size_t output_capacity,
                                 size_t *output_length);

#ifdef __cplusplus
}
#endif

#endif
