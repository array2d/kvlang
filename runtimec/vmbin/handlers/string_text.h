#ifndef VMBIN_HANDLERS_STRING_TEXT_H
#define VMBIN_HANDLERS_STRING_TEXT_H

/*
 * 组件：vmbin/handlers/string_text
 * 作用：字符串算子的**公共文本层**——把任意编码的值解码成码点序列，以及把码点
 *       序列编码回 UTF-8。所有位置算术、比较、拼接都在码点序列上做。
 * 联系：`handlers/string.c` 的 13 条 handler 共用它；`contract/char_codec` 提供
 *       底层编解码原语，本组件只是「参数项 → 码点序列」这一层的编排。
 * 约束：
 *   - **编解码只有一份实现**（`contract/char_codec`），本组件不重写；
 *   - **唯一按编码分派的地方在 `string_text_decode`**：三种编码各一条解码器，
 *     之后全部走码点序列——编码差异到此为止；
 *   - 定长缓冲，超限失败封闭，不截断（截断会产出半条 UTF-8，看起来正常、实际是错的）。
 */

#include <stddef.h>
#include <stdint.h>

#include "binrwir/execution_frame.h"
#include "contract/char_codec.h"
#include "contract/error.h"
#include "contract/status.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 一个字符串算子最多处理这么多个码点。 */
#define STRING_TEXT_CODEPOINT_CAPACITY 128U
#define STRING_TEXT_BYTES_CAPACITY \
    (STRING_TEXT_CODEPOINT_CAPACITY * RUNTIMEC_CHAR_CODEC_UTF8_MAX_BYTES)

/* 解码后的文本：码点序列。位置算术只在这个视图上做，与源编码无关。 */
typedef struct string_text {
    uint32_t codepoints[STRING_TEXT_CODEPOINT_CAPACITY];
    size_t count;
} string_text_t;

/* 一个参数项 → 解码后的码点序列（声明类型决定编码）。 */
runtimec_status_t string_text_decode_argument(
    const binrwir_execution_frame_t *frame, const binrwir_argument_t *argument,
    string_text_t *output, runtimec_error_t *error);

/*
 * **全系统唯一的「按编码三选一」**：给定字节与声明 langtype，解成码点序列。
 *
 * 为什么分岔放在这里而不是每个 handler：操作数的 langtype 是**值的属性**，运行时
 * 读它是「读数据」，不是「按类型分派」。把三选一关进这一间屋子，十二个字符串算子
 * 都调它，任何一个 handler 里都不出现 `if (utf32) / else`。
 *
 * 判据：**中心化的分岔不算分叉**——区别在于改一处还是改九处。
 *
 * 成功返回 SUCCESS 并写 `*output`；编码不认识或字节不是该编码的合法序列返回
 * INVALID，`*output` 置空。**不做替换字符**：替换会把畸形输入变成「看起来正常」
 * 的文本，调用方再也分不出它原本是坏的。
 */
runtimec_status_t string_text_decode(runtimec_bytes_view_t bytes,
                                     runtimec_string_view_t langtype,
                                     string_text_t *output);

/*
 * 按**指定编码**解码——三个函数，各自单一编码。
 *
 * 为什么分成三个而不是一个「看 langtype 再分派」的入口：char 种类已由特化 opid
 * 定死，「这个 handler 解哪种编码」是**编译期常量**。三个函数让宏在展开时就把
 * 编码钉住，运行期没有分支。
 */
bool string_text_decode_utf8(const uint8_t *bytes, size_t size,
                             string_text_t *output);
bool string_text_decode_utf32(const uint8_t *bytes, size_t size,
                              string_text_t *output);
bool string_text_decode_ascii(const uint8_t *bytes, size_t size,
                              string_text_t *output);

/*
 * 该编码是否**定宽**：定宽意味着「换一个码点不会改变 body 长度」，可以同槽 patch。
 * 只有 `char/utf32` 是定宽（4 字节/码点）；utf8 与 ascii 都不是。
 */
bool string_text_encoding_is_fixed_width(runtimec_string_view_t langtype);

/* 码点序列 → UTF-8 字节。 */
runtimec_status_t string_text_encode_utf8(const string_text_t *text,
                                          uint8_t *output, size_t capacity,
                                          size_t *output_size);

#ifdef __cplusplus
}
#endif

#endif
