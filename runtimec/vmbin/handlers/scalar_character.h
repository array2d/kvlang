#ifndef VMBIN_HANDLERS_SCALAR_CHARACTER_H
#define VMBIN_HANDLERS_SCALAR_CHARACTER_H

/*
 * 组件：vmbin/handlers/scalar_character
 * 作用：字符族特化 handler 的实现体——concat、streq、streq·not、cast↔char。
 * 联系：`scalar_generated.c` 产出定宽算术；本族是**变长**那一侧，所以单独成文件。
 *       解码一律经 `string_text` 的三个解码器（coding4 的面，只读调用）。
 * 约束：失败置 `exit`，不返回状态；编码在编译期由特化 opid 钉死，运行期无分派。
 *
 * 为什么需要这几条函数而不是模板：concat 的结果是**变长**的（两段相接），
 * 写它要换长度 → 落在存储层语义上；模板只做「一次运算 + 一次定宽赋值」。
 *
 * ── 本模块的 thunk 宏 ──────────────────────────────────────────────────
 *
 * 生成的 thunk 必须极短，参数准备归宏。`handler_call.h` 的 `_TYPED` / `_CREATE`
 * 族覆盖不了这一族的两件事：
 *   - 写参是**变长** body（concat 结果、单字符结果），不能按 `sizeof(T)` 核宽度；
 *   - 读参是**单字符**时，要「解码成码点」这一步，它既不是纯算术也不是纯拷贝。
 *
 * 本族宏定义在这里，与 `handler_call.h` 的容器/字符串族是**平行关系**，不合并：
 * 两族各自负责一类「读参怎么来、写参怎么去」——那边读容器元素与变长文本，
 * 这边读单字符编码并解码成码点。它们共享不了同一个 thunk 骨架，硬合并只会
 * 造出一个带分支的万能宏。
 *
 * 命名沿用 `VMBIN_HANDLER_*` 前缀，是因为**用途相同**（产出注册表条目），不是因为
 * 将来会搬进同一个文件。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "binrwir/execution_exit.h"
#include "binrwir/binvthread_frame.h"
#include "binrwir/instruction.h"
#include "vmbin/handlers/scalar.h"
#include "vmbin/handlers/string_text.h"
#include "vmbin/value_access.h"


/*
 * ── 本族的 thunk 宏 ────────────────────────────────────────────────────
 *
 * 三个宏，按「读参怎么来、写参怎么去」分。共同点：**参数准备全在这里，
 * 逻辑函数只做一次转换**。之前这 72 条把定位、宽度核对、取值摊在每个 thunk 里，
 * 每条 20 行——`check_handler_size` 报的 72 条红就是这个。
 *
 * 失败出口统一走 `runtimec_scalar_fail` + 返回 `RUNTIMEC_STATUS_ERROR`：
 * 结构化 exit 已由本宏置好，调用方看 exit 而不是看返回值。
 */

/* 读参核宽度并取值：这是三个宏共用的第一步。 */
#define RUNTIMEC_SCALAR_READ_SIZED(frame, instruction, read_type, output_name,  \
                                   output_exit)                                \
    vmbin_value_location_t output_name##_position_;                            \
    const read_type *output_name = NULL;                                       \
    do {                                                                       \
        runtimec_error_t output_name##_error_;                                 \
        runtimec_error_reset(&output_name##_error_);                           \
        if (vmbin_value_access_locate_body(                                    \
                (frame), &(instruction)->read_arguments.data[0],               \
                &output_name##_position_, &output_name##_error_) !=            \
            RUNTIMEC_STATUS_SUCCESS) {                                         \
            runtimec_scalar_fail((output_exit),                                \
                                 RUNTIMEC_SCALAR_DIAG_LOCATE_FAILED);          \
            return RUNTIMEC_STATUS_ERROR;                                      \
        }                                                                      \
        if (output_name##_position_.size != sizeof(read_type)) {               \
            runtimec_scalar_fail((output_exit),                                \
                                 RUNTIMEC_SCALAR_DIAG_LENGTH_MISMATCH);        \
            return RUNTIMEC_STATUS_ERROR;                                      \
        }                                                                      \
        output_name = (const read_type *)(const void *)                        \
            output_name##_position_.read_bytes;                                \
    } while (0)

/* 收口：逻辑函数置了 exit 就返回它的状态，否则成功。**只写一次**。 */
#define RUNTIMEC_SCALAR_THUNK_END(output_exit)                                 \
    return (output_exit)->kind == BINRWIR_EXECUTION_EXIT_ERROR                 \
               ? (output_exit)->status                                         \
               : RUNTIMEC_STATUS_SUCCESS

/*
 * 定宽读 → 单字符写。`source_kind` 是读取时用的 C 类型名（bool 或整数），
 * `to_code_point` 是把该值转成 uint32 码点的表达式。
 */
#define VMBIN_HANDLER_UNARY_TO_CHARACTER(handler_name, function, source_type,   \
                                         to_code_point, encoding)              \
    static runtimec_status_t handler_name(                                     \
        binrwir_binvthread_frame_t *frame,                                      \
        const binrwir_instruction_view_t *instruction,                         \
        binrwir_execution_exit_t *output_exit) {                               \
        RUNTIMEC_SCALAR_READ_SIZED(frame, instruction, source_type, source_,   \
                                   output_exit);                               \
        {                                                                      \
            const uint32_t source_value_ = (uint32_t)(to_code_point);          \
            function(frame, instruction, source_value_, encoding,              \
                     output_exit);                                             \
        }                                                                      \
        RUNTIMEC_SCALAR_THUNK_END(output_exit);                                \
    }

/* 单字符读 → 数值写（定宽）。目标宽度/符号性/浮点由元组给定，是编译期常量。 */
#define VMBIN_HANDLER_CHARACTER_TO_NUMBER(handler_name, function, decoder,      \
                                          target_width, target_signed,         \
                                          target_is_float)                     \
    static runtimec_status_t handler_name(                                     \
        binrwir_binvthread_frame_t *frame,                                      \
        const binrwir_instruction_view_t *instruction,                         \
        binrwir_execution_exit_t *output_exit) {                               \
        function(frame, instruction, decoder, target_width, target_signed,     \
                 target_is_float, output_exit);                                \
        RUNTIMEC_SCALAR_THUNK_END(output_exit);                                \
    }

/* 单字符读 → 单字符写（跨编码）。 */
#define VMBIN_HANDLER_CHARACTER_TO_CHARACTER(handler_name, function, decoder,   \
                                             encoding)                         \
    static runtimec_status_t handler_name(                                     \
        binrwir_binvthread_frame_t *frame,                                      \
        const binrwir_instruction_view_t *instruction,                         \
        binrwir_execution_exit_t *output_exit) {                               \
        function(frame, instruction, decoder, encoding, output_exit);          \
        RUNTIMEC_SCALAR_THUNK_END(output_exit);                                \
    }

/* 两段字符读 → 比较（结果定长 bool）。 */
#define VMBIN_HANDLER_CHARACTER_EQUAL(handler_name, function, left_decoder,     \
                                      right_decoder, invert)                   \
    static runtimec_status_t handler_name(                                     \
        binrwir_binvthread_frame_t *frame,                                      \
        const binrwir_instruction_view_t *instruction,                         \
        binrwir_execution_exit_t *output_exit) {                               \
        function(frame, instruction, left_decoder, right_decoder, invert,      \
                 output_exit);                                                 \
        RUNTIMEC_SCALAR_THUNK_END(output_exit);                                \
    }

/* 两段字符读 → 变长 utf8 写。 */
#define VMBIN_HANDLER_CHARACTER_CONCAT(handler_name, function, left_decoder,    \
                                       right_decoder)                          \
    static runtimec_status_t handler_name(                                     \
        binrwir_binvthread_frame_t *frame,                                      \
        const binrwir_instruction_view_t *instruction,                         \
        binrwir_execution_exit_t *output_exit) {                               \
        function(frame, instruction, left_decoder, right_decoder,              \
                 output_exit);                                                 \
        RUNTIMEC_SCALAR_THUNK_END(output_exit);                                \
    }

#ifdef __cplusplus
extern "C" {
#endif

/* 目标字符编码：由特化元组决定，运行期不分派。 */
typedef enum runtimec_character_encoding {
    RUNTIMEC_CHARACTER_ENCODING_UTF8 = 0,
    RUNTIMEC_CHARACTER_ENCODING_UTF32 = 1,
    RUNTIMEC_CHARACTER_ENCODING_ASCII = 2
} runtimec_character_encoding_t;

/*
 * 解码函数的形状：`string_text_decode_{utf8,utf32,ascii}` 三者同签名。
 *
 * 在本文件定义而不是去 `string_text.h` 里加：那个头归 coding4，本族只读调用它的
 * 函数，不在别人的头上加类型。等它自己要加时，这一行删掉、换 include 即可。
 */
typedef bool (*string_text_decode_function_t)(const uint8_t *bytes, size_t size,
                                              string_text_t *output);

/*
 * 字符 → 码点：单字符解码后必须**恰好一个码点**。空 body 或多码点都是畸形载荷，
 * 返回 false（不取第一个——那会把 `"ab"` 静默当成 `'a'`）。
 */
bool runtimec_scalar_character_code_point(const uint8_t *bytes, size_t size,
                                          string_text_decode_function_t decode,
                                          uint32_t *output_code_point);

/*
 * 数值 → 单字符（matrix:62 character-conversion-result）：产出字符串值，
 * 非法 Unicode 标量（> U+10FFFF 或代理区）报错。
 */
void runtimec_scalar_value_to_character(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, uint32_t code_point,
    runtimec_character_encoding_t target_encoding,
    binrwir_execution_exit_t *output_exit);

/* 字符 → 字符（跨编码）：解码成码点后按目标编码重编。 */
void runtimec_scalar_character_to_value(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    string_text_decode_function_t decode_source,
    runtimec_character_encoding_t target_encoding,
    binrwir_execution_exit_t *output_exit);

/* 字符 → 数值：码点值写到定宽目标槽；窄化按整数 cast 的同一套判据回绕。 */
void runtimec_scalar_character_to_number(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    string_text_decode_function_t decode_source, uint32_t target_width,
    bool target_signed, bool target_is_float,
    binrwir_execution_exit_t *output_exit);

/* concat：两段文本 → 一条新的 char/utf8（矩阵：结果恒 utf8）。 */
void runtimec_scalar_concat(binrwir_binvthread_frame_t *frame,
                            const binrwir_instruction_view_t *instruction,
                            string_text_decode_function_t decode_left,
                            string_text_decode_function_t decode_right,
                            binrwir_execution_exit_t *output_exit);

/* streq / streq·not：解码后按码点序列比较，结果 bool；`invert` 是 streq·not。 */
void runtimec_scalar_character_equal(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    string_text_decode_function_t decode_left,
    string_text_decode_function_t decode_right, bool invert,
    binrwir_execution_exit_t *output_exit);

#ifdef __cplusplus
}
#endif

#endif
