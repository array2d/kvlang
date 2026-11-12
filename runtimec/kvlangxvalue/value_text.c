/*
 * 组件：kvlangxvalue/value_text — 实现。判据全在头注释里，这里只写「怎么映射」。
 */

#include "kvlangxvalue/value_text.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kvlangxvalue/char_codec.h"
#include "kvlangxvalue/float_format.h"
#include "kvlangxvalue/langtype.h"

/* ── 小工具 ─────────────────────────────────────────────────────────── */

static bool view_equals(runtimec_string_view_t view, const char *literal) {
    const size_t length = strlen(literal);
    return view.size == length && memcmp(view.data, literal, length) == 0;
}

static void write_little_endian(uint8_t *output, uint64_t value, uint32_t width) {
    for (uint32_t index = 0; index < width; index++) {
        output[index] = (uint8_t)(value >> (8 * index));
    }
}

static uint64_t read_little_endian(const uint8_t *input, uint32_t width) {
    uint64_t value = 0;
    for (uint32_t index = 0; index < width; index++) {
        value |= (uint64_t)input[index] << (8 * index);
    }
    return value;
}

static bool fail(runtimec_value_text_failure_t *failure,
                 runtimec_value_text_failure_t reason, size_t *output_length) {
    if (failure) {
        *failure = reason;
    }
    if (output_length) {
        *output_length = 0;
    }
    return false;
}

/* 把 size 字节的视图拷成 NUL 结尾串。太长返回 false。 */
static bool to_c_string(runtimec_string_view_t view, char *buffer,
                        size_t capacity) {
    if (view.size + 1 > capacity) {
        return false;
    }
    memcpy(buffer, view.data, view.size);
    buffer[view.size] = '\0';
    return true;
}

/* ── 整数文本 ↔ 字节 ───────────────────────────────────────────────── */

/* 有符号整数：宽度由 langtype 给，范围检查是判据的一部分（300 当 int8 是错的）。 */
static bool signed_to_bytes(uint32_t width, runtimec_string_view_t text,
                            uint8_t *output, size_t capacity,
                            size_t *output_length,
                            runtimec_value_text_failure_t *failure) {
    char buffer[32];
    if (!to_c_string(text, buffer, sizeof buffer)) {
        return fail(failure, RUNTIMEC_VALUE_TEXT_BAD_ESCAPE_FORMAT, output_length);
    }
    char *end = NULL;
    errno = 0;
    const long long value = strtoll(buffer, &end, 10);
    if (end == buffer || *end != '\0' || errno == ERANGE) {
        return fail(failure, RUNTIMEC_VALUE_TEXT_BAD_ESCAPE_FORMAT, output_length);
    }
    /* 范围：width 字节有符号。int64 走 else 分支避开未定义移位。 */
    if (width < 8) {
        const long long limit = 1LL << (width * 8 - 1);
        if (value < -limit || value >= limit) {
            return fail(failure, RUNTIMEC_VALUE_TEXT_OUT_OF_RANGE, output_length);
        }
    }
    if (output == NULL) {
        if (capacity != 0) {
            return fail(failure, RUNTIMEC_VALUE_TEXT_buffer_TOO_SMALL, output_length);
        }
        *output_length = width;
        return true;
    }
    if (capacity < width) {
        return fail(failure, RUNTIMEC_VALUE_TEXT_buffer_TOO_SMALL, output_length);
    }
    write_little_endian(output, (uint64_t)value, width);
    *output_length = width;
    if (failure) {
        *failure = RUNTIMEC_VALUE_TEXT_OK;
    }
    return true;
}

static bool unsigned_to_bytes(uint32_t width, runtimec_string_view_t text,
                              uint8_t *output, size_t capacity,
                              size_t *output_length,
                              runtimec_value_text_failure_t *failure) {
    char buffer[32];
    if (!to_c_string(text, buffer, sizeof buffer)) {
        return fail(failure, RUNTIMEC_VALUE_TEXT_BAD_ESCAPE_FORMAT, output_length);
    }
    /* 负号一律拒绝：strtoull 会把 "-1" 静默转成最大值。 */
    if (buffer[0] == '-') {
        return fail(failure, RUNTIMEC_VALUE_TEXT_OUT_OF_RANGE, output_length);
    }
    char *end = NULL;
    errno = 0;
    const unsigned long long value = strtoull(buffer, &end, 10);
    if (end == buffer || *end != '\0' || errno == ERANGE) {
        return fail(failure, RUNTIMEC_VALUE_TEXT_BAD_ESCAPE_FORMAT, output_length);
    }
    if (width < 8 && value >= (1ULL << (width * 8))) {
        return fail(failure, RUNTIMEC_VALUE_TEXT_OUT_OF_RANGE, output_length);
    }
    if (output == NULL) {
        if (capacity != 0) {
            return fail(failure, RUNTIMEC_VALUE_TEXT_buffer_TOO_SMALL, output_length);
        }
        *output_length = width;
        return true;
    }
    if (capacity < width) {
        return fail(failure, RUNTIMEC_VALUE_TEXT_buffer_TOO_SMALL, output_length);
    }
    write_little_endian(output, value, width);
    *output_length = width;
    if (failure) {
        *failure = RUNTIMEC_VALUE_TEXT_OK;
    }
    return true;
}

static bool bool_to_bytes(runtimec_string_view_t text, uint8_t *output,
                          size_t capacity, size_t *output_length,
                          runtimec_value_text_failure_t *failure) {
    /* bool 的文本形态只有 true/false —— 不接受 1/0：那是 int8 的写法，
       两套写法会让「同一个值」有两个来源。 */
    uint8_t value = 0;
    if (view_equals(text, "true")) {
        value = 1;
    } else if (view_equals(text, "false")) {
        value = 0;
    } else {
        return fail(failure, RUNTIMEC_VALUE_TEXT_BAD_ESCAPE_FORMAT, output_length);
    }
    if (output == NULL) {
        if (capacity != 0) {
            return fail(failure, RUNTIMEC_VALUE_TEXT_buffer_TOO_SMALL, output_length);
        }
        *output_length = 1;
        return true;
    }
    if (capacity < 1) {
        return fail(failure, RUNTIMEC_VALUE_TEXT_buffer_TOO_SMALL, output_length);
    }
    output[0] = value;
    *output_length = 1;
    if (failure) {
        *failure = RUNTIMEC_VALUE_TEXT_OK;
    }
    return true;
}

/* ── 浮点文本 ↔ 字节 ───────────────────────────────────────────────── */

static bool float_to_bytes(runtimec_string_view_t text, uint32_t width,
                           uint8_t *output, size_t capacity,
                           size_t *output_length,
                           runtimec_value_text_failure_t *failure) {
    char buffer[64];
    if (!to_c_string(text, buffer, sizeof buffer)) {
        return fail(failure, RUNTIMEC_VALUE_TEXT_BAD_ESCAPE_FORMAT, output_length);
    }
    char *end = NULL;
    errno = 0;
    const double value = strtod(buffer, &end);
    if (end == buffer || *end != '\0' || errno == ERANGE) {
        return fail(failure, RUNTIMEC_VALUE_TEXT_BAD_ESCAPE_FORMAT, output_length);
    }
    if (output == NULL) {
        if (capacity != 0) {
            return fail(failure, RUNTIMEC_VALUE_TEXT_buffer_TOO_SMALL, output_length);
        }
        *output_length = width;
        return true;
    }
    if (capacity < width) {
        return fail(failure, RUNTIMEC_VALUE_TEXT_buffer_TOO_SMALL, output_length);
    }
    if (width == 4) {
        const float narrowed = (float)value;
        uint32_t bits = 0;
        memcpy(&bits, &narrowed, sizeof bits);
        write_little_endian(output, bits, 4);
    } else {
        uint64_t bits = 0;
        memcpy(&bits, &value, sizeof bits);
        write_little_endian(output, bits, 8);
    }
    *output_length = width;
    if (failure) {
        *failure = RUNTIMEC_VALUE_TEXT_OK;
    }
    return true;
}

/* ── 字符 / 字符串 ─────────────────────────────────────────────────── */

/* 按编码把一段文本写成 body。utf8 原样；utf32 转码点序列；ascii 校验后原样。 */
static bool characters_to_bytes(runtimec_langtype_char_encoding_t encoding,
                                runtimec_string_view_t text, uint8_t *output,
                                size_t capacity, size_t *output_length,
                                runtimec_value_text_failure_t *failure) {
    switch (encoding) {
    case RUNTIMEC_LANGTYPE_CHAR_ENCODING_ASCII:
        for (size_t index = 0; index < text.size; index++) {
            if ((unsigned char)text.data[index] >= 0x80U) {
                return fail(failure, RUNTIMEC_VALUE_TEXT_BAD_ESCAPE_FORMAT,
                            output_length);
            }
        }
        if (output == NULL) {
            if (capacity != 0) {
                return fail(failure, RUNTIMEC_VALUE_TEXT_buffer_TOO_SMALL,
                            output_length);
            }
            *output_length = text.size;
            return true;
        }
        if (capacity < text.size) {
            return fail(failure, RUNTIMEC_VALUE_TEXT_buffer_TOO_SMALL, output_length);
        }
        memcpy(output, text.data, text.size);
        *output_length = text.size;
        if (failure) {
            *failure = RUNTIMEC_VALUE_TEXT_OK;
        }
        return true;
    case RUNTIMEC_LANGTYPE_CHAR_ENCODING_UTF8:
        if (output == NULL) {
            if (capacity != 0) {
                return fail(failure, RUNTIMEC_VALUE_TEXT_buffer_TOO_SMALL,
                            output_length);
            }
            *output_length = text.size;
            return true;
        }
        if (capacity < text.size) {
            return fail(failure, RUNTIMEC_VALUE_TEXT_buffer_TOO_SMALL, output_length);
        }
        /* UTF-8 输入必须自洽：不校验就会把畸形字节当成合法值存进去。 */
        {
            size_t checked = 0;
            /* 用反向转换做校验：目标缓冲给足 text.size 个码点够用。 */
            uint8_t *scratch = (uint8_t *)malloc(text.size * 4U + 4U);
            if (scratch == NULL) {
                return fail(failure, RUNTIMEC_VALUE_TEXT_buffer_TOO_SMALL,
                            output_length);
            }
            const bool valid = runtimec_char_utf8_to_utf32(
                (const uint8_t *)text.data, text.size, scratch,
                text.size * 4U + 4U, &checked);
            free(scratch);
            if (!valid) {
                return fail(failure, RUNTIMEC_VALUE_TEXT_BAD_ESCAPE_FORMAT,
                            output_length);
            }
        }
        memcpy(output, text.data, text.size);
        *output_length = text.size;
        if (failure) {
            *failure = RUNTIMEC_VALUE_TEXT_OK;
        }
        return true;
    case RUNTIMEC_LANGTYPE_CHAR_ENCODING_UTF32: {
        /* 先把 UTF-8 文本转成码点，再按 4 字节小端摊开。 */
        uint8_t *codepoints = (uint8_t *)malloc(text.size * 4U + 4U);
        if (codepoints == NULL) {
            return fail(failure, RUNTIMEC_VALUE_TEXT_buffer_TOO_SMALL, output_length);
        }
        size_t codepoint_bytes = 0;
        const bool valid = runtimec_char_utf8_to_utf32(
            (const uint8_t *)text.data, text.size, codepoints, text.size * 4U + 4U,
            &codepoint_bytes);
        if (!valid) {
            free(codepoints);
            return fail(failure, RUNTIMEC_VALUE_TEXT_BAD_ESCAPE_FORMAT, output_length);
        }
        if (output == NULL) {
            free(codepoints);
            if (capacity != 0) {
                return fail(failure, RUNTIMEC_VALUE_TEXT_buffer_TOO_SMALL,
                            output_length);
            }
            *output_length = codepoint_bytes;
            return true;
        }
        if (capacity < codepoint_bytes) {
            free(codepoints);
            return fail(failure, RUNTIMEC_VALUE_TEXT_buffer_TOO_SMALL, output_length);
        }
        memcpy(output, codepoints, codepoint_bytes);
        free(codepoints);
        *output_length = codepoint_bytes;
        if (failure) {
            *failure = RUNTIMEC_VALUE_TEXT_OK;
        }
        return true;
    }
    case RUNTIMEC_LANGTYPE_CHAR_ENCODING_NONE:
        break;
    }
    return fail(failure, RUNTIMEC_VALUE_TEXT_UNSUPPORTED_TYPE, output_length);
}

/* ── 文本 → body（主入口）─────────────────────────────────────────── */

bool runtimec_value_text_to_bytes(runtimec_string_view_t langtype,
                                  runtimec_string_view_t value_text,
                                  uint8_t *output, size_t output_capacity,
                                  size_t *output_length,
                                  runtimec_value_text_failure_t *failure) {
    if (failure) {
        *failure = RUNTIMEC_VALUE_TEXT_OK;
    }
    if (output_length == NULL) {
        return fail(failure, RUNTIMEC_VALUE_TEXT_BAD_LANGTYPE, NULL);
    }
    *output_length = 0;

    runtimec_langtype_t parsed;
    runtimec_langtype_parse(langtype, &parsed);

    /* 标量：宽度与符号性来自解析结果，文本解释按它走。 */
    if (parsed.form == RUNTIMEC_LANGTYPE_FORM_SCALAR) {
        switch (parsed.scalar) {
        case RUNTIMEC_LANGTYPE_SCALAR_SIGNED_INTEGER:
            return signed_to_bytes(parsed.scalar_width, value_text, output,
                                   output_capacity, output_length, failure);
        case RUNTIMEC_LANGTYPE_SCALAR_UNSIGNED_INTEGER:
            return unsigned_to_bytes(parsed.scalar_width, value_text, output,
                                     output_capacity, output_length, failure);
        case RUNTIMEC_LANGTYPE_SCALAR_FLOAT:
            return float_to_bytes(value_text, parsed.scalar_width, output,
                                  output_capacity, output_length, failure);
        case RUNTIMEC_LANGTYPE_SCALAR_BOOLEAN:
            return bool_to_bytes(value_text, output, output_capacity,
                                 output_length, failure);
        case RUNTIMEC_LANGTYPE_SCALAR_NONE:
            break;
        }
        return fail(failure, RUNTIMEC_VALUE_TEXT_BAD_LANGTYPE, output_length);
    }

    /* 字符：单字符与字符串共用编码，区别只在语义（这里都按字节写）。 */
    if (parsed.form == RUNTIMEC_LANGTYPE_FORM_CHARACTER ||
        parsed.form == RUNTIMEC_LANGTYPE_FORM_STRING) {
        return characters_to_bytes(parsed.char_encoding, value_text, output,
                                   output_capacity, output_length, failure);
    }

    /* 其余形态（ARRAY 定长 / TUPLE / MAPPING / UNRECOGNIZED）没有唯一文本形态。
       **不猜格式**——猜出来的格式会被当成标准，然后所有人跟着用。 */
    return fail(failure, RUNTIMEC_VALUE_TEXT_UNSUPPORTED_TYPE, output_length);
}

/* ── body → 文本（主入口）─────────────────────────────────────────── */

static bool finish_text(const char *source, size_t length, char *output,
                        size_t capacity, size_t *output_length,
                        runtimec_value_text_failure_t *failure) {
    if (output == NULL) {
        if (capacity != 0) {
            return fail(failure, RUNTIMEC_VALUE_TEXT_buffer_TOO_SMALL, output_length);
        }
        *output_length = length;
        return true;
    }
    if (capacity <= length) {
        return fail(failure, RUNTIMEC_VALUE_TEXT_buffer_TOO_SMALL, output_length);
    }
    memcpy(output, source, length);
    output[length] = '\0';
    *output_length = length;
    if (failure) {
        *failure = RUNTIMEC_VALUE_TEXT_OK;
    }
    return true;
}

bool runtimec_value_text_to_text(runtimec_string_view_t langtype,
                                 runtimec_bytes_view_t body, char *output,
                                 size_t output_capacity, size_t *output_length,
                                 runtimec_value_text_failure_t *failure) {
    if (failure) {
        *failure = RUNTIMEC_VALUE_TEXT_OK;
    }
    if (output_length == NULL) {
        return fail(failure, RUNTIMEC_VALUE_TEXT_BAD_LANGTYPE, NULL);
    }
    *output_length = 0;

    runtimec_langtype_t parsed;
    runtimec_langtype_parse(langtype, &parsed);

    if (parsed.form == RUNTIMEC_LANGTYPE_FORM_SCALAR) {
        char buffer[64];
        int written = 0;
        switch (parsed.scalar) {
        case RUNTIMEC_LANGTYPE_SCALAR_SIGNED_INTEGER: {
            if (body.size != parsed.scalar_width) {
                return fail(failure, RUNTIMEC_VALUE_TEXT_BAD_ESCAPE_FORMAT,
                            output_length);
            }
            const uint64_t raw = read_little_endian(body.data, parsed.scalar_width);
            int64_t value = (int64_t)raw;
            /* 符号扩展：宽度不足 8 时把高位补成符号位。 */
            if (parsed.scalar_width < 8) {
                const uint32_t shift = 64U - parsed.scalar_width * 8U;
                value = ((int64_t)(raw << shift)) >> shift;
            }
            written = snprintf(buffer, sizeof buffer, "%lld", (long long)value);
            break;
        }
        case RUNTIMEC_LANGTYPE_SCALAR_UNSIGNED_INTEGER: {
            if (body.size != parsed.scalar_width) {
                return fail(failure, RUNTIMEC_VALUE_TEXT_BAD_ESCAPE_FORMAT,
                            output_length);
            }
            const uint64_t value = read_little_endian(body.data, parsed.scalar_width);
            written = snprintf(buffer, sizeof buffer, "%llu",
                               (unsigned long long)value);
            break;
        }
        case RUNTIMEC_LANGTYPE_SCALAR_FLOAT: {
            if (body.size != parsed.scalar_width) {
                return fail(failure, RUNTIMEC_VALUE_TEXT_BAD_ESCAPE_FORMAT,
                            output_length);
            }
            const uint64_t raw = read_little_endian(body.data, parsed.scalar_width);
            double value = 0.0;
            if (parsed.scalar_width == 4) {
                const uint32_t bits = (uint32_t)raw;
                float narrowed = 0.0f;
                memcpy(&narrowed, &bits, sizeof narrowed);
                value = (double)narrowed;
            } else {
                memcpy(&value, &raw, sizeof value);
            }
            size_t float_length = 0;
            const runtimec_float_domain_t domain =
                parsed.scalar_width == 4 ? RUNTIMEC_FLOAT_DOMAIN_FLOAT32
                                         : RUNTIMEC_FLOAT_DOMAIN_FLOAT64;
            if (!runtimec_float_format_shortest(value, domain, buffer,
                                                sizeof buffer, &float_length)) {
                return fail(failure, RUNTIMEC_VALUE_TEXT_BAD_ESCAPE_FORMAT,
                            output_length);
            }
            return finish_text(buffer, float_length, output, output_capacity,
                               output_length, failure);
        }
        case RUNTIMEC_LANGTYPE_SCALAR_BOOLEAN: {
            if (body.size != 1) {
                return fail(failure, RUNTIMEC_VALUE_TEXT_BAD_ESCAPE_FORMAT,
                            output_length);
            }
            const char *text = body.data[0] ? "true" : "false";
            return finish_text(text, strlen(text), output, output_capacity,
                               output_length, failure);
        }
        case RUNTIMEC_LANGTYPE_SCALAR_NONE:
            return fail(failure, RUNTIMEC_VALUE_TEXT_BAD_LANGTYPE, output_length);
        }
        if (written <= 0 || (size_t)written >= sizeof buffer) {
            return fail(failure, RUNTIMEC_VALUE_TEXT_BAD_ESCAPE_FORMAT, output_length);
        }
        return finish_text(buffer, (size_t)written, output, output_capacity,
                           output_length, failure);
    }

    if (parsed.form == RUNTIMEC_LANGTYPE_FORM_CHARACTER ||
        parsed.form == RUNTIMEC_LANGTYPE_FORM_STRING) {
        switch (parsed.char_encoding) {
        case RUNTIMEC_LANGTYPE_CHAR_ENCODING_UTF8:
        case RUNTIMEC_LANGTYPE_CHAR_ENCODING_ASCII:
            /* 两种编码的字节就是文本本身；ASCII 已在写入时校验过。 */
            return finish_text((const char *)body.data, body.size, output,
                               output_capacity, output_length, failure);
        case RUNTIMEC_LANGTYPE_CHAR_ENCODING_UTF32: {
            if (body.size % 4U != 0) {
                return fail(failure, RUNTIMEC_VALUE_TEXT_BAD_ESCAPE_FORMAT,
                            output_length);
            }
            if (output == NULL) {
                if (output_capacity != 0) {
                    return fail(failure, RUNTIMEC_VALUE_TEXT_buffer_TOO_SMALL,
                                output_length);
                }
                /* UTF-8 最长 4 字节/码点，给上界即可。 */
                *output_length = body.size;
                return true;
            }
            size_t encoded = 0;
            /* 编码器**不写** NUL 结尾（见 char_codec.h：那个字节留给调用方），
               所以这里必须自己补——不补的话读回的是缓冲区里的陈旧尾巴，
               看起来像「值多出几个字符」。 */
            if (!runtimec_char_utf32_to_utf8(body.data, body.size, output,
                                             output_capacity, &encoded)) {
                return fail(failure, RUNTIMEC_VALUE_TEXT_buffer_TOO_SMALL,
                            output_length);
            }
            if (encoded >= output_capacity) {
                return fail(failure, RUNTIMEC_VALUE_TEXT_buffer_TOO_SMALL,
                            output_length);
            }
            output[encoded] = '\0';
            *output_length = encoded;
            if (failure) {
                *failure = RUNTIMEC_VALUE_TEXT_OK;
            }
            return true;
        }
        case RUNTIMEC_LANGTYPE_CHAR_ENCODING_NONE:
            break;
        }
    }

    return fail(failure, RUNTIMEC_VALUE_TEXT_UNSUPPORTED_TYPE, output_length);
}

const char *runtimec_value_text_failure_text(runtimec_value_text_failure_t failure) {
    switch (failure) {
    case RUNTIMEC_VALUE_TEXT_OK:
        return "ok";
    case RUNTIMEC_VALUE_TEXT_BAD_LANGTYPE:
        return "langtype 语法畸形";
    case RUNTIMEC_VALUE_TEXT_UNSUPPORTED_TYPE:
        return "该类型不支持文本解释";
    case RUNTIMEC_VALUE_TEXT_BAD_ESCAPE_FORMAT:
        return "文本不是该类型的合法表示";
    case RUNTIMEC_VALUE_TEXT_OUT_OF_RANGE:
        return "数值超出该类型的表示范围";
    case RUNTIMEC_VALUE_TEXT_buffer_TOO_SMALL:
        return "输出缓冲不足（不截断）";
    }
    return "未知失败";
}
