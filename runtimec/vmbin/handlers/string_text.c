/*
 * 组件：vmbin/handlers/string_text 的实现
 * 约束：编解码一律经 `contract/char_codec`；本文件不写第二份。
 *       唯一按编码分派的地方是 `string_decode`，之后全部走码点序列。
 */

#include "vmbin/handlers/string_text.h"

#include <string.h>

#include "vmbin/value_access.h"

/* 基种类 → 编码：这是「参数类型的一部分」，不是内容分支。 */
enum string_encoding {
    STRING_ENCODING_INVALID = 0,
    STRING_ENCODING_UTF8 = 1,
    STRING_ENCODING_UTF32 = 2,
    STRING_ENCODING_ASCII = 3
};

static enum string_encoding string_encoding_of(runtimec_string_view_t langtype) {
    static const struct {
        const char *text;
        enum string_encoding encoding;
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
    const enum string_encoding encoding = string_encoding_of(langtype);
    size_t written = 0U;

    output->count = 0U;
    switch (encoding) {
    case STRING_ENCODING_UTF32:
        /* 码点序列已经是目标形态：拷贝即得，无需转换。 */
        if (size % 4U != 0U || size / 4U > STRING_TEXT_CODEPOINT_CAPACITY) {
            return RUNTIMEC_STATUS_INVALID;
        }
        for (size_t offset = 0U; offset < size; offset += 4U) {
            const uint32_t codepoint = (uint32_t)bytes[offset] |
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
        uint8_t converted[STRING_TEXT_CODEPOINT_CAPACITY * 4U];

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
        if (size > STRING_TEXT_CODEPOINT_CAPACITY) {
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

/* 三个单一编码入口：供宏在展开时钉住编码，运行期无分支。 */
bool string_text_decode_utf8(const uint8_t *bytes, size_t size,
                             string_text_t *output) {
    return string_decode(runtimec_string_view_from_c_string("char/utf8"), bytes,
                         size, output) == RUNTIMEC_STATUS_SUCCESS;
}

bool string_text_decode_utf32(const uint8_t *bytes, size_t size,
                              string_text_t *output) {
    return string_decode(runtimec_string_view_from_c_string("char/utf32"), bytes,
                         size, output) == RUNTIMEC_STATUS_SUCCESS;
}

bool string_text_decode_ascii(const uint8_t *bytes, size_t size,
                              string_text_t *output) {
    return string_decode(runtimec_string_view_from_c_string("char/ascii"), bytes,
                         size, output) == RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 码点序列 → **按 langtype 指定的编码**的字节。
 *
 * 这是 A 裁决（`decision`：值的字节形态服从写槽声明的 langtype）的唯一落点。
 * 三种编码的差别是**形状**，不是内容：
 *   char/utf32 —— 每个码点 4 字节小端，定宽；
 *   char/utf8  —— 变长 1~4 字节；
 *   char/ascii —— 每码点 1 字节，> 0x7F 装不下即失败（**不截断**：截断会把 `é`
 *                 变成某个控制字符，那是静默的数据损坏）。
 *
 * `langtype` 无效（不是三种字符种类之一）⇒ INVALID，不猜。
 */
runtimec_status_t string_text_encode(runtimec_string_view_t langtype,
                                     const string_text_t *text,
                                     uint8_t *output, size_t capacity,
                                     size_t *output_size) {
    *output_size = 0U;
    switch (string_encoding_of(langtype)) {
    case STRING_ENCODING_UTF32:
        if (text->count > capacity / 4U) {
            return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
        }
        for (size_t ordinal = 0U; ordinal < text->count; ++ordinal) {
            const uint32_t codepoint = text->codepoints[ordinal];
            output[ordinal * 4U] = (uint8_t)(codepoint & 0xFFU);
            output[ordinal * 4U + 1U] = (uint8_t)((codepoint >> 8U) & 0xFFU);
            output[ordinal * 4U + 2U] = (uint8_t)((codepoint >> 16U) & 0xFFU);
            output[ordinal * 4U + 3U] = (uint8_t)((codepoint >> 24U) & 0xFFU);
        }
        *output_size = text->count * 4U;
        return RUNTIMEC_STATUS_SUCCESS;
    case STRING_ENCODING_ASCII:
        if (text->count > capacity) {
            return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
        }
        for (size_t ordinal = 0U; ordinal < text->count; ++ordinal) {
            if (text->codepoints[ordinal] > UINT32_C(0x7F)) {
                return RUNTIMEC_STATUS_INVALID;
            }
            output[ordinal] = (uint8_t)text->codepoints[ordinal];
        }
        *output_size = text->count;
        return RUNTIMEC_STATUS_SUCCESS;
    case STRING_ENCODING_UTF8:
        return string_text_encode_utf8(text, output, capacity, output_size);
    case STRING_ENCODING_INVALID:
    default:
        return RUNTIMEC_STATUS_INVALID;
    }
}

runtimec_status_t string_text_encode_utf8(const string_text_t *text,
                                          uint8_t *output, size_t capacity,
                                          size_t *output_size) {
    uint8_t codepoint_bytes[STRING_TEXT_CODEPOINT_CAPACITY * 4U];

    *output_size = 0U;
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
    /* codec 要求容量比输出多留 1 字节（调用方原有约定），照传。 */
    if (!runtimec_char_utf32_to_utf8(codepoint_bytes, text->count * 4U,
                                     (char *)output, capacity, output_size)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t string_text_decode_argument(
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

/* 全系统唯一的「按编码三选一」——其余十二个字符串算子都调它。 */
runtimec_status_t string_text_decode(runtimec_bytes_view_t bytes,
                                     runtimec_string_view_t langtype,
                                     string_text_t *output) {
    output->count = 0U;
    return string_decode(langtype, bytes.data, bytes.size, output);
}

/* 只有 utf32 定宽：每码点恒 4 字节。「换码点不改长度」的唯一依据是编码本身。 */
bool string_text_encoding_is_fixed_width(runtimec_string_view_t langtype) {
    return string_encoding_of(langtype) == STRING_ENCODING_UTF32;
}
