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

/*
 * 一个字符串算子最多处理这么多个码点。**全仓只有这一个字符串容量常量**——
 * `string.c` 曾另有一个 `STRING_CODEPOINT_CAPACITY`，两处都写着「上限」而值不同，
 * 改一处不生效（实测：把 `string.c` 那个从 128 改到 1024，拼接照样在 128 字处
 * 失败，因为真正的截断点在 `string_text_t` 的 `codepoints[]` 数组与本文件的
 * 编码缓冲里）。同义常量不许有两个。
 *
 * **1024 码点 = 4KB 的 `codepoints[]` 数组**。这是「本实现受理的上限」，
 * 不是「内存够不够」：超出按具名失败返回，**不截断、不降级**——截断会产出一个
 * head 自称合法、字符却少了的字符串，那比失败难查得多。
 *
 * 为什么不是 128：benchmark 的 `k_nucleotide` 用 65 字符的 base 累加 3 轮
 * = 195 码点，128 会让它在第二轮就失败。
 *
 * 与之配套的**字节**上限是 `handler_call_text.h` 的
 * `VMBIN_HANDLER_CREATE_BUFFER_CAPACITY`（4096 = 1024 × 4）——两个数必须对得上。
 *
 * **再往上就该换实现，不是继续加这个数**：让存储层分配，栈帧不随码点数增长。
 */
#define STRING_TEXT_CODEPOINT_CAPACITY 1024U
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

/*
 * 码点序列 → **按 langtype 指定编码**的字节。这是「值的字节形态服从写槽声明的
 * langtype」这条规则的唯一落点（char·char / slice / canonicalize / formatint /
 * concat 全部走它）。
 *
 * 三种编码的差别是形状不是内容：utf32 每码点 4 字节定宽、utf8 变长、ascii 每码点
 * 1 字节且 > 0x7F 失败封闭（不截断）。langtype 不是字符种类之一 ⇒ INVALID。
 */
runtimec_status_t string_text_encode(runtimec_string_view_t langtype,
                                     const string_text_t *text,
                                     uint8_t *output, size_t capacity,
                                     size_t *output_size);

/* 码点序列 → UTF-8 字节。只在「结果恒 UTF-8」的场合直接用。 */
runtimec_status_t string_text_encode_utf8(const string_text_t *text,
                                          uint8_t *output, size_t capacity,
                                          size_t *output_size);

/*
 * 两个操作数的**共同编码**：`string·concat` 的结果该用什么编码写出。
 *
 * 规则（矩阵 `character-concat-kind`，2026-11-06 用户裁决）：
 *
 *   | 左      | 右      | 结果      |
 *   |---------|---------|-----------|
 *   | utf32   | utf32   | utf32     |
 *   | utf8    | utf8    | utf8      |
 *   | ascii   | utf8/32 | 另一侧    |
 *   | utf8    | utf32   | INVALID   |
 *
 * **`char/ascii` 提升而不拒绝**：它是 utf8 与 utf32 的共同子集，拼接没有歧义
 * （C `strcat`、Go `+`、Python `+` 没有一家拒）。拒它只会让 `"a" + ascii_var`
 * 无端报错。
 *
 * **utf8 + utf32 拒绝**：两种变长/定宽表示之间没有共同编码，必须由源码显式
 * 转换——静默选一个会让同一份程序在不同输入上产出不同字节布局。
 *
 * 剥前导 `[dims]` 再比：`[]char/X` 与 `char/X` 是同一种编码，维度是形态不是编码。
 *
 * `*output` 写成**裸种类名**（`char/utf32` 这类，不带维度段）——它是
 * `string_text_encode` 的直接输入。
 *
 * 判据放在本模块而不是 `string.c`：ascii 提升是**编码层知识**，与三种编码的
 * 解码器/编码器同处才只有一份真相。
 */
runtimec_status_t string_text_common_encoding(runtimec_string_view_t left,
                                              runtimec_string_view_t right,
                                              runtimec_string_view_t *output);

#ifdef __cplusplus
}
#endif

#endif
