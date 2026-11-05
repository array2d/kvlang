#ifndef VMBIN_HANDLERS_SCALAR_CHARACTER_H
#define VMBIN_HANDLERS_SCALAR_CHARACTER_H

/*
 * 组件：vmbin/handlers/scalar_character
 * 作用：字符族特化 handler 的实现体——concat（9 条）、streq / streq·not（各 9 条）。
 * 联系：`scalar_generated.c` 产出定宽算术；本族是**变长**那一侧，所以单独成文件。
 *       解码一律经 `string_text` 的三个解码器（coding4 的面，只读调用）。
 * 约束：失败置 `exit`，不返回状态；编码在编译期由特化 opid 钉死，运行期无分派。
 *
 * 为什么需要这三条函数而不是模板：concat 的结果是**变长**的（两段相接），
 * 写它要换长度 → 落在存储层语义上；模板只做「一次运算 + 一次定宽赋值」。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "binrwir/execution_exit.h"
#include "binrwir/execution_frame.h"
#include "binrwir/instruction.h"
#include "vmbin/handlers/scalar.h"
#include "vmbin/handlers/string_text.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 解码函数的形状：`string_text_decode_{utf8,utf32,ascii}` 三者同签名。
 *
 * 在本文件定义而不是去 `string_text.h` 里加：那个头归 coding4，本族只读调用它的
 * 函数，不在别人的头上加类型。等它自己要加时，这一行删掉、换 include 即可。
 */
typedef bool (*string_text_decode_function_t)(const uint8_t *bytes, size_t size,
                                              string_text_t *output);

/* 目标字符编码：由特化元组决定，运行期不分派。 */
typedef enum runtimec_character_encoding {
    RUNTIMEC_CHARACTER_ENCODING_UTF8 = 0,
    RUNTIMEC_CHARACTER_ENCODING_UTF32 = 1,
    RUNTIMEC_CHARACTER_ENCODING_ASCII = 2
} runtimec_character_encoding_t;

/*
 * 数值 → 单字符（matrix:62 character-conversion-result）：产出字符串值，
 * 非法 Unicode 标量（> U+10FFFF 或代理区）报错。
 */
void runtimec_scalar_value_to_character(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction, uint32_t code_point,
    runtimec_character_encoding_t target_encoding,
    binrwir_execution_exit_t *output_exit);

/*
 * 字符 → 码点：单字符解码后必须**恰好一个码点**。空 body 或多码点都是畸形载荷，
 * 返回 false（不取第一个——那会把 `"ab"` 静默当成 `'a'`）。
 */
bool runtimec_scalar_character_code_point(const uint8_t *bytes, size_t size,
                                          string_text_decode_function_t decode,
                                          uint32_t *output_code_point);

/* concat：两段文本 → 一条新的 char/utf8（矩阵：结果恒 utf8）。 */
void runtimec_scalar_concat(binrwir_execution_frame_t *frame,
                            const binrwir_instruction_view_t *instruction,
                            string_text_decode_function_t decode_left,
                            string_text_decode_function_t decode_right,
                            binrwir_execution_exit_t *output_exit);

/* streq / streq·not：解码后按码点序列比较，结果 bool；`invert` 是 streq·not。 */
void runtimec_scalar_character_equal(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    string_text_decode_function_t decode_left,
    string_text_decode_function_t decode_right, bool invert,
    binrwir_execution_exit_t *output_exit);

#ifdef __cplusplus
}
#endif

#endif
