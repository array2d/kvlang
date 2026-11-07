/*
 * 组件：vmbin/handlers/scalar_character 的实现
 * 作用：字符族特化 handler——concat（9 条）、streq / streq·not（各 9 条）。
 * 联系：`scalar_generated.c` 产出定宽算术；本文件是**变长**那一族，形态不同：
 *       concat 要造一条新的 char/utf8 wire 并整条写（分配归存储层），streq 只需
 *       解码后比较、结果定长 bool。
 * 约束：
 *   - **编码三选一不在本文件发生**：`string_text_decode_*` 是唯一解码入口
 *     （coding4 的面，只读调用，不改它的文件）。特化 opid 已经把编码钉死在
 *     编译期，所以每条 handler 调哪个解码器是常量，不是运行期分派；
 *   - concat 的结果**恒 char/utf8**（matrix:64），与两个操作数的编码无关；
 *   - streq 按**解码后的码点序列**比较（matrix:65），种类可以不同；
 *   - 失败一律置 `exit`，不返回状态（与 `scalar_special` 同一约定）。
 *
 * 为什么这 27 条必须单独存在：特化一开，`concat(utf8,utf8)` 会被改写成 0x8xxx，
 * 而注册表里若只有基础 opid `0x001F`，查不到就失败。它们不能等到「基础 opid
 * 的旧实现」兜底——那条路在特化开启后根本走不到。
 */

#include "vmbin/handlers/scalar_character.h"

#include <string.h>

#include "binrwir/execution_exit.h"
#include "binrwir/execution_frame.h"
#include "binrwir/instruction.h"
#include "contract/char_codec.h"
#include "contract/error.h"
#include "contract/status.h"
#include "contract/view.h"
#include "vmbin/handlers/scalar.h"
#include "vmbin/handlers/scalar_special.h"
#include "vmbin/handlers/string_text.h"
#include "vmbin/value_access.h"

/* 造一条 char/utf8 wire 并写进写参的槽——concat 的收尾。
 *
 * 写参的 body 长度是**变**的（拼接结果比任一操作数长），所以不能像定宽算子那样
 * 直接 memcpy 到 `write_bytes`：那要求等长。这里走 `vmbin_value_access_locate_body`
 * 拿到的位置只用来**判定是否可写**，真正的写在下面按目标容量整段拷。
 *
 * 目标槽已在帧绑定时按声明类型分配好容量（`[]char/utf8` 的长度由调用方决定），
 * 所以这里只填不扩。长度不符由 value_access 的等长契约拒绝——扩长度是存储层的事。
 */
static void scalar_character_store_utf8(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction, const uint8_t *bytes,
    size_t size, binrwir_execution_exit_t *output_exit) {
    vmbin_value_location_t location;
    runtimec_error_t error;
    runtimec_status_t status;

    runtimec_error_reset(&error);
    status = vmbin_value_access_locate_body(
        frame, &instruction->write_arguments.data[0], &location, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        runtimec_scalar_fail(output_exit,
                             RUNTIMEC_SCALAR_DIAG_LOCATE_FAILED);
        return;
    }
    if (location.write_bytes == NULL) {
        runtimec_scalar_fail(output_exit,
                             RUNTIMEC_SCALAR_DIAG_READ_ONLY_TARGET);
        return;
    }
    if (location.size != size) {
        /* 变长结果需要目标槽恰好是这么长。槽的长度由编码期决定，派生自操作数
         * 类型与长度；不匹配说明 Program 与执行期的长度事实不一致。 */
        runtimec_scalar_fail(output_exit,
                             RUNTIMEC_SCALAR_DIAG_LENGTH_MISMATCH);
        return;
    }
    if (size != 0U) {
        memcpy(location.write_bytes, bytes, size);
    }
}

/*
 * concat：两段文本解码 → 码点相接 → 编码成 UTF-8 一次写出（matrix:64）。
 *
 * 每个编码组合一条 handler 是因为**解码器要在编译期定**：`string_text_decode_utf8`
 * 与 `_ascii` 的调用点由特化 opid 决定，运行期没有 `if (是哪种编码)`。
 */
void runtimec_scalar_concat(binrwir_execution_frame_t *frame,
                            const binrwir_instruction_view_t *instruction,
                            string_text_decode_function_t decode_left,
                            string_text_decode_function_t decode_right,
                            binrwir_execution_exit_t *output_exit) {
    vmbin_value_location_t left;
    vmbin_value_location_t right;
    runtimec_error_t error;
    runtimec_status_t status;
    string_text_t left_text;
    string_text_t right_text;
    string_text_t joined;
    uint8_t utf8[STRING_TEXT_BYTES_CAPACITY];
    size_t utf8_size = 0U;

    runtimec_error_reset(&error);
    status = vmbin_value_access_locate_body(
        frame, &instruction->read_arguments.data[0], &left, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        runtimec_scalar_fail(output_exit, RUNTIMEC_SCALAR_DIAG_LOCATE_FAILED);
        return;
    }
    status = vmbin_value_access_locate_body(
        frame, &instruction->read_arguments.data[1], &right, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        runtimec_scalar_fail(output_exit, RUNTIMEC_SCALAR_DIAG_LOCATE_FAILED);
        return;
    }
    if (!decode_left(left.read_bytes, left.size, &left_text) ||
        !decode_right(right.read_bytes, right.size, &right_text)) {
        /* 畸形字符载荷（matrix:64 的负向判据）。 */
        runtimec_scalar_fail(output_exit,
                             RUNTIMEC_SCALAR_DIAG_MALFORMED_CHARACTER);
        return;
    }
    if (left_text.count + right_text.count > STRING_TEXT_CODEPOINT_CAPACITY) {
        runtimec_scalar_fail(output_exit,
                             RUNTIMEC_SCALAR_DIAG_LENGTH_MISMATCH);
        return;
    }
    joined.count = left_text.count + right_text.count;
    memcpy(joined.codepoints, left_text.codepoints,
           left_text.count * sizeof(uint32_t));
    memcpy(joined.codepoints + left_text.count, right_text.codepoints,
           right_text.count * sizeof(uint32_t));
    if (string_text_encode_utf8(&joined, utf8, sizeof(utf8), &utf8_size) !=
        RUNTIMEC_STATUS_SUCCESS) {
        runtimec_scalar_fail(output_exit,
                             RUNTIMEC_SCALAR_DIAG_MALFORMED_CHARACTER);
        return;
    }
    scalar_character_store_utf8(frame, instruction, utf8, utf8_size,
                                output_exit);
}

/*
 * streq / streq·not：解码后逐码点比较（matrix:65）。
 *
 * 与 concat 的差别只在结尾：结果定长 bool，直接写一个字节，不碰存储层。
 */
void runtimec_scalar_character_equal(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    string_text_decode_function_t decode_left,
    string_text_decode_function_t decode_right, bool invert,
    binrwir_execution_exit_t *output_exit) {
    vmbin_value_location_t left;
    vmbin_value_location_t right;
    vmbin_value_location_t target;
    runtimec_error_t error;
    runtimec_status_t status;
    string_text_t left_text;
    string_text_t right_text;
    bool equal;

    runtimec_error_reset(&error);
    status = vmbin_value_access_locate_body(
        frame, &instruction->read_arguments.data[0], &left, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        runtimec_scalar_fail(output_exit, RUNTIMEC_SCALAR_DIAG_LOCATE_FAILED);
        return;
    }
    status = vmbin_value_access_locate_body(
        frame, &instruction->read_arguments.data[1], &right, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        runtimec_scalar_fail(output_exit, RUNTIMEC_SCALAR_DIAG_LOCATE_FAILED);
        return;
    }
    if (!decode_left(left.read_bytes, left.size, &left_text) ||
        !decode_right(right.read_bytes, right.size, &right_text)) {
        runtimec_scalar_fail(output_exit,
                             RUNTIMEC_SCALAR_DIAG_MALFORMED_CHARACTER);
        return;
    }
    equal = left_text.count == right_text.count &&
            memcmp(left_text.codepoints, right_text.codepoints,
                   left_text.count * sizeof(uint32_t)) == 0;
    status = vmbin_value_access_locate_body(
        frame, &instruction->write_arguments.data[0], &target, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        runtimec_scalar_fail(output_exit, RUNTIMEC_SCALAR_DIAG_LOCATE_FAILED);
        return;
    }
    if (target.write_bytes == NULL || target.size != sizeof(bool)) {
        runtimec_scalar_fail(output_exit,
                             RUNTIMEC_SCALAR_DIAG_LENGTH_MISMATCH);
        return;
    }
    *target.write_bytes = (uint8_t)((equal != invert) ? 1U : 0U);
}

/*
 * 数值 → 单字符（matrix:62 character-conversion-result）：
 * 「字符转换产出字符串值」，非法 Unicode 标量（> U+10FFFF 或代理区）报错。
 *
 * 三种目标编码的产出**长度不同**，而目标槽的长度是编码期定的：
 *   char/utf32  每个码点 4 字节，恒 4；
 *   char/ascii  仅 U+0000..U+007F，恒 1 字节，超出即错误（不是截断）；
 *   char/utf8   1–4 字节，长度随码点变——**调用方给的槽必须是算好的那个长度**，
 *               所以这条路径要现算再比对，不能假设定宽。
 *
 * 源是 char 的情形（跨编码字符转换）走另一条：先解码成码点、再按目标编码重编。
 */
void runtimec_scalar_value_to_character(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction, uint32_t code_point,
    runtimec_character_encoding_t target_encoding,
    binrwir_execution_exit_t *output_exit) {
    uint8_t encoded[RUNTIMEC_CHAR_CODEC_UTF8_MAX_BYTES];
    size_t encoded_size = 0U;
    uint32_t as_utf32 = code_point;

    if (code_point > UINT32_C(0x10FFFF) ||
        (code_point >= UINT32_C(0xD800) && code_point <= UINT32_C(0xDFFF))) {
        /* 代理区与超范围都不是合法 Unicode 标量值（matrix:62 的负向判据）。 */
        runtimec_scalar_fail(output_exit,
                             RUNTIMEC_SCALAR_DIAG_NOT_A_SCALAR_VALUE);
        return;
    }
    switch (target_encoding) {
    case RUNTIMEC_CHARACTER_ENCODING_UTF32:
        memcpy(encoded, &as_utf32, sizeof(as_utf32));
        encoded_size = sizeof(as_utf32);
        break;
    case RUNTIMEC_CHARACTER_ENCODING_ASCII:
        if (code_point > UINT32_C(0x7F)) {
            /* ASCII 装不下就**报错**，不截断：截断会把 `é` 变成某个控制字符，
             * 看起来像正常数据。 */
            runtimec_scalar_fail(output_exit,
                                 RUNTIMEC_SCALAR_DIAG_MALFORMED_CHARACTER);
            return;
        }
        encoded[0] = (uint8_t)code_point;
        encoded_size = 1U;
        break;
    default:
        if (!runtimec_char_utf32_to_utf8(
                (const uint8_t *)&as_utf32, sizeof(as_utf32),
                (char *)encoded, sizeof(encoded), &encoded_size)) {
            runtimec_scalar_fail(output_exit,
                                 RUNTIMEC_SCALAR_DIAG_NOT_A_SCALAR_VALUE);
            return;
        }
        break;
    }
    scalar_character_store_utf8(frame, instruction, encoded, encoded_size,
                                output_exit);
}

/*
 * 字符 → 数值（matrix:62 的反向）：**先解码成码点**，再按目标类型转换。
 *
 * 单字符的值 decode 出来恰好一个码点。零个或多个都是畸形载荷——前者是空 body，
 * 后者是「说是单字符却装了两个字」。两种情况都失败，不取第一个。
 *
 * 数值类目标（整数/浮点）直接按码点值转换；char 类目标走 `value_to_character`
 * 重编（跨编码字符转换）。
 */
bool runtimec_scalar_character_code_point(const uint8_t *bytes, size_t size,
                                          string_text_decode_function_t decode,
                                          uint32_t *output_code_point) {
    string_text_t text;

    if (!decode(bytes, size, &text)) {
        return false;
    }
    if (text.count != 1U) {
        return false;
    }
    *output_code_point = text.codepoints[0];
    return true;
}

/*
 * 字符 → 数值 / 字符（matrix:62 的反向）。源是字符，先解码成码点，再按目标转换。
 *
 * 目标分两类：
 *   数值目标（整数/浮点）—— 码点值直接转换。码点恒在 [0, 0x10FFFF]，所以
 *      「到更宽的类型」永远装得下；窄化的回绕与饱和按整数 cast 的同一套判据。
 *   char 目标 —— 走 `value_to_character` 重编，目标编码由元组定。
 *
 * 单字符要求**恰好一个码点**：空 body 或多码点都是畸形载荷，不取第一个。
 */
void runtimec_scalar_character_to_value(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    string_text_decode_function_t decode_source,
    runtimec_character_encoding_t target_encoding,
    binrwir_execution_exit_t *output_exit) {
    vmbin_value_location_t source;
    runtimec_error_t error;
    runtimec_status_t status;
    uint32_t code_point = 0U;

    runtimec_error_reset(&error);
    status = vmbin_value_access_locate_body(
        frame, &instruction->read_arguments.data[0], &source, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        runtimec_scalar_fail(output_exit, RUNTIMEC_SCALAR_DIAG_LOCATE_FAILED);
        return;
    }
    if (!runtimec_scalar_character_code_point(source.read_bytes, source.size,
                                              decode_source, &code_point)) {
        runtimec_scalar_fail(output_exit,
                             RUNTIMEC_SCALAR_DIAG_MALFORMED_CHARACTER);
        return;
    }
    runtimec_scalar_value_to_character(frame, instruction, code_point,
                                       target_encoding, output_exit);
}

/*
 * 字符 → 整数 / 浮点：目标定宽，直接写。
 *
 * 码点范围 [0, 0x10FFFF] 对任何整数/浮点目标都够用（最小目标 int8 的上限是 127，
 * 所以**窄化仍可能发生**）——按整数 cast 的同一套语义：有符号目标回绕、无符号
 * 目标回绕。这与其他整数 cast 的判据同源，不另立规则。
 */
void runtimec_scalar_character_to_number(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    string_text_decode_function_t decode_source, uint32_t target_width,
    bool target_signed, bool target_is_float,
    binrwir_execution_exit_t *output_exit) {
    vmbin_value_location_t source;
    vmbin_value_location_t target;
    runtimec_error_t error;
    runtimec_status_t status;
    uint32_t code_point = 0U;

    runtimec_error_reset(&error);
    status = vmbin_value_access_locate_body(
        frame, &instruction->read_arguments.data[0], &source, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        runtimec_scalar_fail(output_exit, RUNTIMEC_SCALAR_DIAG_LOCATE_FAILED);
        return;
    }
    if (!runtimec_scalar_character_code_point(source.read_bytes, source.size,
                                              decode_source, &code_point)) {
        runtimec_scalar_fail(output_exit,
                             RUNTIMEC_SCALAR_DIAG_MALFORMED_CHARACTER);
        return;
    }
    status = vmbin_value_access_locate_body(
        frame, &instruction->write_arguments.data[0], &target, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        runtimec_scalar_fail(output_exit, RUNTIMEC_SCALAR_DIAG_LOCATE_FAILED);
        return;
    }
    if (target.write_bytes == NULL ||
        target.size != (size_t)(target_width / 8U)) {
        runtimec_scalar_fail(output_exit,
                             RUNTIMEC_SCALAR_DIAG_LENGTH_MISMATCH);
        return;
    }
    if (target_is_float) {
        if (target_width == 32U) {
            float narrow = (float)code_point;
            memcpy(target.write_bytes, &narrow, sizeof(narrow));
        } else {
            double wide = (double)code_point;
            memcpy(target.write_bytes, &wide, sizeof(wide));
        }
        return;
    }
    if (target_signed) {
        const int64_t narrowed = runtimec_scalar_wrap_signed(
            (int64_t)code_point, 32U, target_width);
        memcpy(target.write_bytes, &narrowed, target.size);
        return;
    }
    {
        const uint64_t narrowed =
            runtimec_scalar_wrap_unsigned((uint64_t)code_point, target_width);
        memcpy(target.write_bytes, &narrowed, target.size);
    }
}
