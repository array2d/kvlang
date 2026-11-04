/*
 * 组件：vmbin/handlers/string 的实现
 * 作用：字符串算子——长度、取字符、比较、拼接。
 * 联系：条目由 session 装进 `binrwir_registry_t`；值经 `vmbin/value_access`
 *       按地址读写，与 core 规则共用同一条数据面。
 * 约束：
 *   - **不按 langtype 分叉**：三种编码（utf8/utf32/ascii）先统一解码成**码点序列**
 *     （`uint32_t` 数组），之后的所有位置算术、比较、拼接走的都是同一段代码。
 *     编码差异只出现在「解码」与「编码」两个边界函数里。
 *   - **不写第二份编解码**：转换一律调 `contract/char_codec`。
 *   - 变长结果走 `kvspaceSetValue`；定长缓冲超限用既有细节码。
 *
 * 本批实现（opid / 名字 / catalog 行）：
 *   0x0125 string·len       rwir_catalog.def:541  string-codepoint-indexing
 *   0x0121 string·char      rwir_catalog.def:513  string-codepoint-indexing
 *   0x0123 string·cmp       rwir_catalog.def:527  string-compare-ordering
 *   0x0127 string·concat    rwir_catalog.def:555  string-concat-result-kind
 */

#include "vmbin/handlers/string.h"

#include <stdlib.h>
#include <string.h>

#include "binrwir/execution_exit.h"
#include "binrwir/execution_frame.h"
#include "binrwir/instruction.h"
#include "binrwir/operation_identifiers.h"
#include "contract/char_codec.h"
#include "contract/error.h"
#include "contract/status.h"
#include "contract/view.h"
#include "kvspace/kvspace.h"
#include "kvspace/xvalue_head.h"
#include "vmbin/handlers/container.h"
#include "vmbin/handlers/handler_target_key.h"
#include "vmbin/value_access.h"

/* 一个字符串算子最多处理这么多个码点；超限失败封闭，不静默截断。 */
#define STRING_CODEPOINT_CAPACITY 128U
#define STRING_BYTES_CAPACITY \
    (STRING_CODEPOINT_CAPACITY * RUNTIMEC_CHAR_CODEC_UTF8_MAX_BYTES)
#define STRING_KEY_CAPACITY 512U

typedef struct string_text {
    /* 码点序列。所有位置算术都在这个视图上做，与源编码无关。 */
    uint32_t codepoints[STRING_CODEPOINT_CAPACITY];
    size_t count;
} string_text_t;

static runtimec_status_t string_fail(binrwir_execution_exit_t *output_exit,
                                     runtimec_status_t status,
                                     uint32_t source_program_counter) {
    *output_exit = (binrwir_execution_exit_t){0};
    output_exit->kind = BINRWIR_EXECUTION_EXIT_ERROR;
    output_exit->status = status;
    output_exit->logical_program_counter = source_program_counter;
    output_exit->resume_logical_program_counter = source_program_counter;
    return status;
}

/*
 * 基种类 → 编码。三种编码是**参数类型的一部分**，不是内容分支：本函数只看
 * 声明 langtype 的基种类，返回一个枚举值，之后的分派是「三种编码各一条解码器」，
 * 不是「按内容猜」。
 */
typedef enum string_encoding {
    STRING_ENCODING_INVALID = 0,
    STRING_ENCODING_UTF8 = 1,
    STRING_ENCODING_UTF32 = 2,
    STRING_ENCODING_ASCII = 3
} string_encoding_t;

static string_encoding_t string_encoding_of(runtimec_string_view_t langtype) {
    static const struct {
        const char *text;
        string_encoding_t encoding;
    } table[] = {
        {"char/utf8", STRING_ENCODING_UTF8},
        {"char/utf32", STRING_ENCODING_UTF32},
        {"char/ascii", STRING_ENCODING_ASCII},
    };
    size_t offset = 0U;

    /* 剥前导 `[dims]`：值的 langtype 带维度段，符号/签名里的是裸种类。 */
    while (offset < langtype.size && langtype.data[offset] == '[') {
        const char *closing =
            memchr(langtype.data + offset, ']', langtype.size - offset);

        if (closing == NULL) {
            return STRING_ENCODING_INVALID;
        }
        offset = (size_t)(closing - langtype.data) + 1U;
    }
    for (size_t ordinal = 0U; ordinal < sizeof(table) / sizeof(table[0]);
         ++ordinal) {
        const size_t size = strlen(table[ordinal].text);

        if (langtype.size - offset == size &&
            memcmp(langtype.data + offset, table[ordinal].text, size) == 0) {
            return table[ordinal].encoding;
        }
    }
    return STRING_ENCODING_INVALID;
}

/*
 * 解码成码点序列。三种编码各一条解码器，**这是本模块唯一按编码分派的地方**；
 * 之后所有算子都在码点序列上工作。
 */
static runtimec_status_t string_decode(runtimec_string_view_t langtype,
                                       const uint8_t *bytes, size_t size,
                                       string_text_t *output) {
    const string_encoding_t encoding = string_encoding_of(langtype);
    size_t written = 0U;

    output->count = 0U;
    switch (encoding) {
    case STRING_ENCODING_UTF32:
        /* 码点序列已经是目标形态：拷贝即得，无需转换。 */
        if (size % 4U != 0U || size / 4U > STRING_CODEPOINT_CAPACITY) {
            return RUNTIMEC_STATUS_INVALID;
        }
        for (size_t offset = 0U; offset < size; offset += 4U) {
            uint32_t codepoint = (uint32_t)bytes[offset] |
                                 ((uint32_t)bytes[offset + 1U] << 8U) |
                                 ((uint32_t)bytes[offset + 2U] << 16U) |
                                 ((uint32_t)bytes[offset + 3U] << 24U);

            if (codepoint > 0x10FFFFU) {
                return RUNTIMEC_STATUS_INVALID;
            }
            output->codepoints[output->count++] = codepoint;
        }
        return RUNTIMEC_STATUS_SUCCESS;
    case STRING_ENCODING_UTF8: {
        uint8_t converted[STRING_CODEPOINT_CAPACITY * 4U];

        if (!runtimec_char_utf8_to_utf32(bytes, size, converted,
                                         sizeof(converted), &written)) {
            return RUNTIMEC_STATUS_INVALID;
        }
        for (size_t offset = 0U; offset < written; offset += 4U) {
            output->codepoints[output->count++] =
                (uint32_t)converted[offset] |
                ((uint32_t)converted[offset + 1U] << 8U) |
                ((uint32_t)converted[offset + 2U] << 16U) |
                ((uint32_t)converted[offset + 3U] << 24U);
        }
        return RUNTIMEC_STATUS_SUCCESS;
    }
    case STRING_ENCODING_ASCII:
        /* ASCII 一个字节一个码点；>= 0x80 不是合法 ASCII，失败封闭。 */
        if (size > STRING_CODEPOINT_CAPACITY) {
            return RUNTIMEC_STATUS_INVALID;
        }
        for (size_t offset = 0U; offset < size; ++offset) {
            if (bytes[offset] >= 0x80U) {
                return RUNTIMEC_STATUS_INVALID;
            }
            output->codepoints[output->count++] = bytes[offset];
        }
        return RUNTIMEC_STATUS_SUCCESS;
    default:
        return RUNTIMEC_STATUS_UNSUPPORTED;
    }
}

/* 码点序列 → UTF-8 字节（拼接的结果恒为 char/utf8，见行为矩阵）。 */
static runtimec_status_t string_encode_utf8(const string_text_t *text,
                                            uint8_t *output,
                                            size_t capacity,
                                            size_t *output_size) {
    size_t written = 0U;

    *output_size = 0U;
    /* 先折回 4 字节码点的字节序列：char_codec 的入口形态就是它。 */
    {
        uint8_t codepoint_bytes[STRING_CODEPOINT_CAPACITY * 4U];

        for (size_t ordinal = 0U; ordinal < text->count; ++ordinal) {
            const uint32_t codepoint = text->codepoints[ordinal];

            codepoint_bytes[ordinal * 4U] = (uint8_t)(codepoint & 0xFFU);
            codepoint_bytes[ordinal * 4U + 1U] =
                (uint8_t)((codepoint >> 8U) & 0xFFU);
            codepoint_bytes[ordinal * 4U + 2U] =
                (uint8_t)((codepoint >> 16U) & 0xFFU);
            codepoint_bytes[ordinal * 4U + 3U] =
                (uint8_t)((codepoint >> 24U) & 0xFFU);
        }
        /* codec 要求容量比输出多留 1 字节（调用方原有约定），这里照传。 */
        if (!runtimec_char_utf32_to_utf8(codepoint_bytes, text->count * 4U,
                                         (char *)output, capacity, &written)) {
            return RUNTIMEC_STATUS_INVALID;
        }
    }
    *output_size = written;
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 造一条 char/utf8 的 wire 并整条写进写参的目标 key——拼接、取字符两条
 * 创建型算子的公共尾段。**结果恒为 char/utf8**（行为矩阵
 * string-concat-result-kind）：不沿用操作数的编码，也不按操作数组合分叉。
 */
static runtimec_status_t string_publish_utf8(
    const binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction, const uint8_t *bytes,
    size_t size, binrwir_execution_exit_t *output_exit) {
    char key[STRING_KEY_CAPACITY];
    char message[256];
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    char langtype_text[64];
    const uint32_t counter = instruction->source_logical_program_counter;
    runtimec_status_t status;

    /* 形状段 = 码点数？不：这里的 langtype 用 `[字节数]char/utf8` 还是裸种类，
     * 由存储形态决定——compact 字符序列的 langtype 带 `[N]`，N 是**码点数**。
     * 本函数收到的是已编码字节，码点数不等于字节数，所以不能在这里推 N。
     * 写法：从写槽声明取（调用方已经知道结果类型），本函数不猜。 */
    {
        runtimec_string_view_t declared;

        status = vmbin_value_access_langtype(
            frame, &instruction->write_arguments.data[0], &declared, NULL);
        if (status != RUNTIMEC_STATUS_SUCCESS ||
            declared.size + 1U > sizeof(langtype_text)) {
            return string_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
        }
        memcpy(langtype_text, declared.data, declared.size);
        langtype_text[declared.size] = '\0';
    }
    if (kvspaceXvalueHeadNewSlack(bytes, (uint64_t)size, (uint64_t)size,
                                  langtype_text, &wire, &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        return string_fail(output_exit, RUNTIMEC_STATUS_ERROR, counter);
    }
    status = runtimec_handler_target_key(frame, &instruction->write_arguments.data[0], key,
                               sizeof(key));
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        message[0] = '\0';
        if (kvspaceSetValue(frame->kvspace_handle, key, wire,
                            (uint32_t)wire_size, message, sizeof(message)) != 0) {
            status = RUNTIMEC_STATUS_ERROR;
        }
    }
    free(wire);
    return status == RUNTIMEC_STATUS_SUCCESS
               ? RUNTIMEC_STATUS_SUCCESS
               : string_fail(output_exit, status, counter);
}

/* 一个参数项 → 解码后的码点序列。 */
static runtimec_status_t string_argument(
    const binrwir_execution_frame_t *frame, const binrwir_argument_t *argument,
    string_text_t *output, runtimec_error_t *error) {
    runtimec_string_view_t langtype;
    vmbin_value_location_t location;
    runtimec_status_t status =
        vmbin_value_access_langtype(frame, argument, &langtype, error);

    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = vmbin_value_access_locate_body(frame, argument, &location, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    return string_decode(langtype, location.read_bytes, location.size, output);
}

/* 写一个 int64 结果到写参（宽度由写参自己决定）。 */
static runtimec_status_t string_write_int64(
    binrwir_execution_frame_t *frame, const binrwir_argument_t *argument,
    int64_t value, runtimec_error_t *error) {
    vmbin_value_location_t destination;
    uint8_t payload[8];
    runtimec_status_t status =
        vmbin_value_access_locate_body(frame, argument, &destination, error);

    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (destination.write_bytes == NULL || destination.size > sizeof(payload)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    memset(payload, 0, sizeof(payload));
    memcpy(payload, &value, destination.size < sizeof(value) ? destination.size
                                                             : sizeof(value));
    return vmbin_value_access_write_body(
        frame, argument, runtimec_bytes_view_make(payload, destination.size),
        error);
}

/*
 * `string·len(s) -> n`：**码点数**，不是字节数（行为矩阵
 * string-codepoint-indexing 的判据：位置按码点算）。
 */
static inline runtimec_status_t string_length(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    string_text_t text;
    runtimec_error_t error;
    runtimec_status_t status;
    const uint32_t counter = instruction->source_logical_program_counter;

    runtimec_error_reset(&error);
    if (instruction->read_arguments.count != 1U ||
        instruction->write_arguments.count != 1U) {
        return string_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    status = string_argument(frame, &instruction->read_arguments.data[0], &text,
                             &error);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = string_write_int64(frame, &instruction->write_arguments.data[0],
                                    (int64_t)text.count, &error);
    }
    return status == RUNTIMEC_STATUS_SUCCESS
               ? RUNTIMEC_STATUS_SUCCESS
               : string_fail(output_exit, status, counter);
}

/*
 * `string·char(s, i) -> c`：第 i 个码点，结果是一个**单字符字符串**（不是整数）——
 * 行为矩阵 string-codepoint-indexing 的正向例子就是「返回首个码点作为一字符字符串」。
 * 越界按码点数判，与编码无关。
 */
static inline runtimec_status_t string_character(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    string_text_t text;
    string_text_t single;
    vmbin_value_location_t index;
    uint8_t encoded[STRING_BYTES_CAPACITY];
    size_t encoded_size = 0U;
    int64_t position = 0;
    runtimec_error_t error;
    runtimec_status_t status;
    const uint32_t counter = instruction->source_logical_program_counter;

    runtimec_error_reset(&error);
    if (instruction->read_arguments.count != 2U ||
        instruction->write_arguments.count != 1U) {
        return string_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    status = string_argument(frame, &instruction->read_arguments.data[0], &text,
                             &error);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[1], &index, &error);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return string_fail(output_exit, status, counter);
    }
    if (index.size != sizeof(int64_t)) {
        return string_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    memcpy(&position, index.read_bytes, sizeof(position));
    if (position < 0 || (uint64_t)position >= text.count) {
        return string_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    single.count = 1U;
    single.codepoints[0] = text.codepoints[position];
    status = string_encode_utf8(&single, encoded, sizeof(encoded),
                                &encoded_size);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return string_fail(output_exit, status, counter);
    }
    return string_publish_utf8(frame, instruction, encoded, encoded_size,
                               output_exit);
}

/*
 * `string·cmp(a, b) -> n`：按**码点**逐位比较，返回 -1 / 0 / 1
 * （行为矩阵 string-compare-ordering 是 consensus：五语言都按码点）。
 *
 * 先用公共前缀比，前缀相同则短的更小——这是各语言字典序的同一套规则。
 */
static inline runtimec_status_t string_compare(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    string_text_t left;
    string_text_t right;
    runtimec_error_t error;
    runtimec_status_t status;
    const uint32_t counter = instruction->source_logical_program_counter;

    runtimec_error_reset(&error);
    if (instruction->read_arguments.count != 2U ||
        instruction->write_arguments.count != 1U) {
        return string_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    status = string_argument(frame, &instruction->read_arguments.data[0], &left,
                             &error);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = string_argument(frame, &instruction->read_arguments.data[1],
                                 &right, &error);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return string_fail(output_exit, status, counter);
    }
    {
        const size_t shared = left.count < right.count ? left.count : right.count;
        int64_t result = 0;
        size_t ordinal = 0U;

        for (; ordinal < shared; ++ordinal) {
            if (left.codepoints[ordinal] != right.codepoints[ordinal]) {
                result = left.codepoints[ordinal] < right.codepoints[ordinal]
                             ? -1
                             : 1;
                break;
            }
        }
        if (ordinal == shared && left.count != right.count) {
            result = left.count < right.count ? -1 : 1;
        }
        status = string_write_int64(frame, &instruction->write_arguments.data[0],
                                    result, &error);
    }
    return status == RUNTIMEC_STATUS_SUCCESS
               ? RUNTIMEC_STATUS_SUCCESS
               : string_fail(output_exit, status, counter);
}

/*
 * `string·concat(a, b) -> c`：结果恒为 **char/utf8**（行为矩阵
 * string-concat-result-kind 是 consensus：拼接产出新串，两个操作数不变）。
 *
 * 操作数可以任意编码：先各自解码成码点，拼起来再统一编码一次——这正是
 * 「按码点拼接、编码只做一次」的实现形态，不需要为编码组合写 N² 条路径。
 */
static inline runtimec_status_t string_concat(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    string_text_t left;
    string_text_t right;
    string_text_t joined;
    uint8_t encoded[STRING_BYTES_CAPACITY];
    size_t encoded_size = 0U;
    runtimec_error_t error;
    runtimec_status_t status;
    const uint32_t counter = instruction->source_logical_program_counter;

    runtimec_error_reset(&error);
    if (instruction->read_arguments.count != 2U ||
        instruction->write_arguments.count != 1U) {
        return string_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    status = string_argument(frame, &instruction->read_arguments.data[0], &left,
                             &error);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = string_argument(frame, &instruction->read_arguments.data[1],
                                 &right, &error);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return string_fail(output_exit, status, counter);
    }
    if (left.count + right.count > STRING_CODEPOINT_CAPACITY) {
        return string_fail(output_exit, RUNTIMEC_STATUS_OUT_OF_MEMORY, counter);
    }
    joined.count = left.count + right.count;
    memcpy(joined.codepoints, left.codepoints, left.count * sizeof(uint32_t));
    memcpy(joined.codepoints + left.count, right.codepoints,
           right.count * sizeof(uint32_t));
    status = string_encode_utf8(&joined, encoded, sizeof(encoded),
                                &encoded_size);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return string_fail(output_exit, status, counter);
    }
    return string_publish_utf8(frame, instruction, encoded, encoded_size,
                               output_exit);
}

/*
 * 注册用的薄封装：主体是 `static inline`，注册表要的是函数地址，两件事各自
 * 独立；薄封装只在**本文件**定义一次，注册表里因此只有一份地址。
 */
static runtimec_status_t string_length_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return string_length(frame, instruction, output_exit);
}

static runtimec_status_t string_character_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return string_character(frame, instruction, output_exit);
}

static runtimec_status_t string_compare_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return string_compare(frame, instruction, output_exit);
}

static runtimec_status_t string_concat_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return string_concat(frame, instruction, output_exit);
}


/*
 * `string·set(s, i, c) -> s`：把第 i 个码点换成 `c` 的首码点，**同槽 patch**——
 * 只改 body 里那一段，head 与 attr 一个字节不动。
 *
 * `s` 必须先物化成可变长度的字符序列；换成不同字节宽的码点时 body 长度会变，
 * 那不是 patch 能做的事，此时失败封闭（改长度属于存储层）。
 */
static inline runtimec_status_t string_set(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    string_text_t text;
    string_text_t replacement;
    vmbin_value_location_t location;
    vmbin_value_location_t position_value;
    int64_t position = 0;
    runtimec_error_t error;
    runtimec_status_t status;
    const uint32_t counter = instruction->source_logical_program_counter;

    runtimec_error_reset(&error);
    if (instruction->read_arguments.count != 3U ||
        instruction->write_arguments.count != 1U) {
        return string_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    status = string_argument(frame, &instruction->read_arguments.data[0], &text,
                             &error);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[1], &position_value, &error);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = string_argument(frame, &instruction->read_arguments.data[2],
                                 &replacement, &error);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return string_fail(output_exit, status, counter);
    }
    if (position_value.size != sizeof(position) || replacement.count != 1U) {
        return string_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    memcpy(&position, position_value.read_bytes, sizeof(position));
    if (position < 0 || (uint64_t)position >= text.count) {
        return string_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    status = vmbin_value_access_locate_body(
        frame, &instruction->write_arguments.data[0], &location, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return string_fail(output_exit, status, counter);
    }
    if (location.write_bytes == NULL) {
        return string_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    /*
     * 只处理码点宽度不变的替换：新码点与旧码点的 UTF-8 字节宽相同时直接覆盖
     * 那一段。宽度不同要改 body 长度——那是存储层的事，这里失败封闭。
     */
    {
        string_text_t old_one;
        uint8_t old_bytes[STRING_BYTES_CAPACITY];
        uint8_t new_bytes[STRING_BYTES_CAPACITY];
        size_t old_size = 0U;
        size_t new_size = 0U;

        old_one.count = 1U;
        old_one.codepoints[0] = text.codepoints[position];
        status = string_encode_utf8(&old_one, old_bytes, sizeof(old_bytes),
                                    &old_size);
        if (status == RUNTIMEC_STATUS_SUCCESS) {
            status = string_encode_utf8(&replacement, new_bytes,
                                        sizeof(new_bytes), &new_size);
        }
        if (status != RUNTIMEC_STATUS_SUCCESS || old_size != new_size) {
            return string_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
        }
        /* 源编码到 UTF-8 的字节偏移：前面每个码点各自编码后的长度之和。 */
        {
            size_t byte_offset = 0U;

            for (int64_t ordinal = 0; ordinal < position; ++ordinal) {
                string_text_t single;
                uint8_t scratch[STRING_BYTES_CAPACITY];
                size_t scratch_size = 0U;

                single.count = 1U;
                single.codepoints[0] = text.codepoints[ordinal];
                if (string_encode_utf8(&single, scratch, sizeof(scratch),
                                       &scratch_size) != RUNTIMEC_STATUS_SUCCESS) {
                    return string_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                       counter);
                }
                byte_offset += scratch_size;
            }
            if (byte_offset + new_size > location.size) {
                return string_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
            }
            memcpy(location.write_bytes + byte_offset, new_bytes, new_size);
        }
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * `string·ord(s, i) -> n`：第 i 个码点的**码点值**（整数）。与 `char` 相对：
 * `char` 返回一字符字符串，`ord` 返回它的数值。越界按码点数判。
 */
static inline runtimec_status_t string_ordinal(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    string_text_t text;
    vmbin_value_location_t position_value;
    int64_t position = 0;
    runtimec_error_t error;
    runtimec_status_t status;
    const uint32_t counter = instruction->source_logical_program_counter;

    runtimec_error_reset(&error);
    if (instruction->read_arguments.count != 2U ||
        instruction->write_arguments.count != 1U) {
        return string_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    status = string_argument(frame, &instruction->read_arguments.data[0], &text,
                             &error);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[1], &position_value, &error);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return string_fail(output_exit, status, counter);
    }
    if (position_value.size != sizeof(position)) {
        return string_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    memcpy(&position, position_value.read_bytes, sizeof(position));
    if (position < 0 || (uint64_t)position >= text.count) {
        return string_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    status = string_write_int64(frame, &instruction->write_arguments.data[0],
                                (int64_t)text.codepoints[position], &error);
    return status == RUNTIMEC_STATUS_SUCCESS
               ? RUNTIMEC_STATUS_SUCCESS
               : string_fail(output_exit, status, counter);
}

/*
 * `string·find(s, needle) -> n`：needle 首次出现的**码点下标**，找不到返回 -1。
 * 空 needle 返回 0（各语言的同一套约定）。
 */
static inline runtimec_status_t string_find(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    string_text_t haystack;
    string_text_t needle;
    runtimec_error_t error;
    runtimec_status_t status;
    const uint32_t counter = instruction->source_logical_program_counter;

    runtimec_error_reset(&error);
    if (instruction->read_arguments.count != 2U ||
        instruction->write_arguments.count != 1U) {
        return string_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    status = string_argument(frame, &instruction->read_arguments.data[0],
                             &haystack, &error);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = string_argument(frame, &instruction->read_arguments.data[1],
                                 &needle, &error);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return string_fail(output_exit, status, counter);
    }
    {
        int64_t found = -1;
        size_t start = 0U;

        if (needle.count == 0U) {
            found = 0;
        } else {
            for (; start + needle.count <= haystack.count && found < 0; ++start) {
                size_t matched = 0U;

                while (matched < needle.count &&
                       haystack.codepoints[start + matched] ==
                           needle.codepoints[matched]) {
                    ++matched;
                }
                if (matched == needle.count) {
                    found = (int64_t)start;
                }
            }
        }
        status = string_write_int64(frame, &instruction->write_arguments.data[0],
                                    found, &error);
    }
    return status == RUNTIMEC_STATUS_SUCCESS
               ? RUNTIMEC_STATUS_SUCCESS
               : string_fail(output_exit, status, counter);
}

/*
 * `string·slice(s, lo, hi) -> t`：按**码点**取半开区间 `[lo, hi)`，结果恒为
 * char/utf8。位置越界或 lo > hi 返回 INVALID（行为矩阵 string-slice-bounds）。
 */
static inline runtimec_status_t string_slice(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    string_text_t text;
    string_text_t sliced;
    vmbin_value_location_t bounds[2];
    int64_t lo = 0;
    int64_t hi = 0;
    uint8_t encoded[STRING_BYTES_CAPACITY];
    size_t encoded_size = 0U;
    runtimec_error_t error;
    runtimec_status_t status;
    const uint32_t counter = instruction->source_logical_program_counter;

    runtimec_error_reset(&error);
    if (instruction->read_arguments.count != 3U ||
        instruction->write_arguments.count != 1U) {
        return string_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    status = string_argument(frame, &instruction->read_arguments.data[0], &text,
                             &error);
    for (size_t ordinal = 0U; ordinal < 2U && status == RUNTIMEC_STATUS_SUCCESS;
         ++ordinal) {
        status = vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[ordinal + 1U],
            &bounds[ordinal], &error);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return string_fail(output_exit, status, counter);
    }
    if (bounds[0].size != sizeof(lo) || bounds[1].size != sizeof(hi)) {
        return string_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    memcpy(&lo, bounds[0].read_bytes, sizeof(lo));
    memcpy(&hi, bounds[1].read_bytes, sizeof(hi));
    if (lo < 0 || hi < lo || (uint64_t)hi > text.count) {
        return string_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    sliced.count = (size_t)(hi - lo);
    memcpy(sliced.codepoints, text.codepoints + lo,
           sliced.count * sizeof(uint32_t));
    status = string_encode_utf8(&sliced, encoded, sizeof(encoded),
                                &encoded_size);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return string_fail(output_exit, status, counter);
    }
    return string_publish_utf8(frame, instruction, encoded, encoded_size,
                               output_exit);
}

/*
 * 整数 → 文本（`formatint` / `formatuint` 共用）：base 缺省 10，11 以上用
 * **小写**字母（行为矩阵 string-format-base-radix）。负号只对带符号版本生效。
 */
static runtimec_status_t string_format_integer(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction, int is_signed,
    binrwir_execution_exit_t *output_exit) {
    vmbin_value_location_t value_location;
    vmbin_value_location_t base_location;
    uint64_t magnitude = 0U;
    int64_t signed_value = 0;
    int64_t base = 10;
    char digits[80];
    size_t length = 0U;
    int negative = 0;
    uint8_t encoded[STRING_BYTES_CAPACITY];
    size_t encoded_size = 0U;
    runtimec_error_t error;
    runtimec_status_t status;
    const uint32_t counter = instruction->source_logical_program_counter;

    runtimec_error_reset(&error);
    if (instruction->read_arguments.count < 1U ||
        instruction->read_arguments.count > 2U ||
        instruction->write_arguments.count != 1U) {
        return string_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    status = vmbin_value_access_locate_body(
        frame, &instruction->read_arguments.data[0], &value_location, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return string_fail(output_exit, status, counter);
    }
    if (value_location.size != sizeof(int64_t)) {
        return string_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    if (is_signed != 0) {
        memcpy(&signed_value, value_location.read_bytes, sizeof(signed_value));
        if (signed_value < 0) {
            negative = 1;
            /* 取绝对值时按无符号算，避免 INT64_MIN 的溢出。 */
            magnitude = (uint64_t)(-(signed_value + 1)) + 1U;
        } else {
            magnitude = (uint64_t)signed_value;
        }
    } else {
        memcpy(&magnitude, value_location.read_bytes, sizeof(magnitude));
    }
    if (instruction->read_arguments.count == 2U) {
        status = vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[1], &base_location, &error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return string_fail(output_exit, status, counter);
        }
        if (base_location.size != sizeof(base)) {
            return string_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
        }
        memcpy(&base, base_location.read_bytes, sizeof(base));
    }
    if (base < 2 || base > 36) {
        return string_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    {
        char reversed[80];
        size_t written = 0U;

        do {
            const uint64_t remainder = magnitude % (uint64_t)base;
            reversed[written++] =
                remainder < 10U ? (char)('0' + remainder)
                                : (char)('a' + (remainder - 10U));
            magnitude /= (uint64_t)base;
        } while (magnitude != 0U && written < sizeof(reversed));
        if (negative != 0) {
            digits[length++] = '-';
        }
        while (written > 0U) {
            digits[length++] = reversed[--written];
        }
    }
    {
        string_text_t text;
        size_t ordinal = 0U;

        text.count = length;
        for (; ordinal < length; ++ordinal) {
            text.codepoints[ordinal] = (uint32_t)(uint8_t)digits[ordinal];
        }
        status = string_encode_utf8(&text, encoded, sizeof(encoded),
                                    &encoded_size);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return string_fail(output_exit, status, counter);
    }
    return string_publish_utf8(frame, instruction, encoded, encoded_size,
                               output_exit);
}

/*
 * 严格全消费的数字解析（`parseint` / `parseuint` / `parsefloat` 共用）：
 * 行为矩阵 string-parse-strict-full-consumption —— 整串必须吃完，否则 INVALID。
 * **不做部分解析、不跳过空白**：`strtol` 那种「停在第一个非法字符」正是这条
 * 决策明确否掉的语义。
 */
static runtimec_status_t string_parse_number(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction, int allow_float,
    int is_signed, binrwir_execution_exit_t *output_exit) {
    string_text_t text;
    vmbin_value_location_t destination;
    uint8_t payload[8];
    runtimec_error_t error;
    runtimec_status_t status;
    const uint32_t counter = instruction->source_logical_program_counter;

    runtimec_error_reset(&error);
    if (instruction->read_arguments.count != 1U ||
        instruction->write_arguments.count != 1U) {
        return string_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    status = string_argument(frame, &instruction->read_arguments.data[0], &text,
                             &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return string_fail(output_exit, status, counter);
    }
    if (text.count == 0U || text.count + 1U > STRING_CODEPOINT_CAPACITY) {
        return string_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    status = vmbin_value_access_locate_body(
        frame, &instruction->write_arguments.data[0], &destination, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return string_fail(output_exit, status, counter);
    }
    if (destination.write_bytes == NULL || destination.size > sizeof(payload)) {
        return string_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    {
        char ascii[STRING_CODEPOINT_CAPACITY + 1U];
        size_t ordinal = 0U;

        for (; ordinal < text.count; ++ordinal) {
            /* 只接受 ASCII 数字形态：非 ASCII 码点一律不是数字。 */
            if (text.codepoints[ordinal] > 0x7FU) {
                return string_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
            }
            ascii[ordinal] = (char)(uint8_t)text.codepoints[ordinal];
        }
        ascii[ordinal] = '\0';
        memset(payload, 0, sizeof(payload));
        if (allow_float != 0) {
            char *end = NULL;
            const double parsed = strtod(ascii, &end);

            /* 严格全消费：end 必须停在结尾，且真的消费了内容。 */
            if (end == ascii || *end != '\0') {
                return string_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
            }
            memcpy(payload, &parsed, sizeof(parsed));
        } else if (is_signed != 0) {
            char *end = NULL;
            const long long parsed = strtoll(ascii, &end, 10);

            if (end == ascii || *end != '\0') {
                return string_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
            }
            {
                const int64_t value = (int64_t)parsed;

                memcpy(payload, &value, sizeof(value));
            }
        } else {
            char *end = NULL;
            const unsigned long long parsed = strtoull(ascii, &end, 10);

            /* 负数不是合法无符号数：strtoull 会接受 '-'，这里要拦。 */
            if (ascii[0] == '-' || end == ascii || *end != '\0') {
                return string_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
            }
            {
                const uint64_t value = (uint64_t)parsed;

                memcpy(payload, &value, sizeof(value));
            }
        }
    }
    status = vmbin_value_access_write_body(
        frame, &instruction->write_arguments.data[0],
        runtimec_bytes_view_make(payload, destination.size), &error);
    return status == RUNTIMEC_STATUS_SUCCESS
               ? RUNTIMEC_STATUS_SUCCESS
               : string_fail(output_exit, status, counter);
}

static runtimec_status_t string_set_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return string_set(frame, instruction, output_exit);
}

static runtimec_status_t string_ordinal_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return string_ordinal(frame, instruction, output_exit);
}

static runtimec_status_t string_find_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return string_find(frame, instruction, output_exit);
}

static runtimec_status_t string_slice_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return string_slice(frame, instruction, output_exit);
}

static runtimec_status_t string_format_integer_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return string_format_integer(frame, instruction, 1, output_exit);
}

static runtimec_status_t string_format_unsigned_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return string_format_integer(frame, instruction, 0, output_exit);
}

static runtimec_status_t string_parse_integer_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return string_parse_number(frame, instruction, 0, 1, output_exit);
}

static runtimec_status_t string_parse_unsigned_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return string_parse_number(frame, instruction, 0, 0, output_exit);
}

static runtimec_status_t string_parse_float_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return string_parse_number(frame, instruction, 1, 0, output_exit);
}

static const binrwir_registry_entry_t runtimec_string_entry_table[] = {
    {BINRWIR_OPERATION_IDENTIFIER_STRING_LENGTH, BINRWIR_REGISTRY_FLAG_NONE,
     string_length_handler},
    {BINRWIR_OPERATION_IDENTIFIER_STRING_CHARACTER, BINRWIR_REGISTRY_FLAG_NONE,
     string_character_handler},
    {BINRWIR_OPERATION_IDENTIFIER_STRING_COMPARE, BINRWIR_REGISTRY_FLAG_NONE,
     string_compare_handler},
    {BINRWIR_OPERATION_IDENTIFIER_STRING_CONCAT, BINRWIR_REGISTRY_FLAG_NONE,
     string_concat_handler},
    {BINRWIR_OPERATION_IDENTIFIER_STRING_SET, BINRWIR_REGISTRY_FLAG_NONE,
     string_set_handler},
    {BINRWIR_OPERATION_IDENTIFIER_STRING_ORDINAL, BINRWIR_REGISTRY_FLAG_NONE,
     string_ordinal_handler},
    {BINRWIR_OPERATION_IDENTIFIER_STRING_FIND, BINRWIR_REGISTRY_FLAG_NONE,
     string_find_handler},
    {BINRWIR_OPERATION_IDENTIFIER_STRING_SLICE, BINRWIR_REGISTRY_FLAG_NONE,
     string_slice_handler},
    {BINRWIR_OPERATION_IDENTIFIER_STRING_FORMAT_INTEGER,
     BINRWIR_REGISTRY_FLAG_NONE, string_format_integer_handler},
    {BINRWIR_OPERATION_IDENTIFIER_STRING_FORMAT_UNSIGNED_INTEGER,
     BINRWIR_REGISTRY_FLAG_NONE, string_format_unsigned_handler},
    {BINRWIR_OPERATION_IDENTIFIER_STRING_PARSE_INTEGER, BINRWIR_REGISTRY_FLAG_NONE,
     string_parse_integer_handler},
    {BINRWIR_OPERATION_IDENTIFIER_STRING_PARSE_UNSIGNED_INTEGER,
     BINRWIR_REGISTRY_FLAG_NONE, string_parse_unsigned_handler},
    {BINRWIR_OPERATION_IDENTIFIER_STRING_PARSE_FLOAT, BINRWIR_REGISTRY_FLAG_NONE,
     string_parse_float_handler},
};

const binrwir_registry_entry_t *runtimec_string_handler_entries(
    size_t *output_count) {
    if (output_count == NULL) {
        return NULL;
    }
    *output_count = sizeof(runtimec_string_entry_table) /
                    sizeof(runtimec_string_entry_table[0]);
    return runtimec_string_entry_table;
}
