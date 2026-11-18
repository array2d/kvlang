/*
 * 组件：kvlangxvalue/value_text — 实现。判据全在头注释里，这里只写「怎么映射」。
 */

#include "kvlangxvalue.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── 往返域：决定用哪种类型判「读回来还是同一个值」────────────── */
typedef enum kvlangXvalueFloatDomain {
    KVLANG_XVALUE_FLOAT_DOMAIN_FLOAT32 = 0, /* strtof */
    KVLANG_XVALUE_FLOAT_DOMAIN_FLOAT64 = 1  /* strtod */
} kvlangXvalueFloatDomain;

#include "char_codec.h"
#include "langtype.h"

/* 浮点 → 文本（原 float_format 组件，只有一个调用方，合并在本文件）。 */
static bool float_shortest(double value, kvlangXvalueFloatDomain domain,
                           char *output, size_t capacity,
                           size_t *output_length);

/* ── 小工具 ─────────────────────────────────────────────────────────── */

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

static bool fail(kvlangXvalueTextFailure *failure,
                 kvlangXvalueTextFailure reason, size_t *output_length) {
    if (failure) {
        *failure = reason;
    }
    if (output_length) {
        *output_length = 0;
    }
    return false;
}

/* ── 整数文本 ↔ 字节 ───────────────────────────────────────────────── */

/* 有符号整数：宽度由 langtype 给，范围检查是判据的一部分（300 当 int8 是错的）。 */
static bool signed_to_bytes(uint32_t width, kvlangXvalueStringView text,
                            uint8_t *output, size_t capacity,
                            size_t *output_length,
                            kvlangXvalueTextFailure *failure) {
    char buffer[32];
    if (text.size + 1 > sizeof buffer ||
        snprintf(buffer, sizeof buffer, "%.*s", (int)text.size, text.data) < 0) {
        return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT, output_length);
    }
    char *end = NULL;
    errno = 0;
    const long long value = strtoll(buffer, &end, 10);
    if (end == buffer || *end != '\0' || errno == ERANGE) {
        return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT, output_length);
    }
    /* 范围：width 字节有符号。int64 走 else 分支避开未定义移位。 */
    if (width < 8) {
        const long long limit = 1LL << (width * 8 - 1);
        if (value < -limit || value >= limit) {
            return fail(failure, KVLANG_XVALUE_TEXT_OUT_OF_RANGE, output_length);
        }
    }
    if (output == NULL) {
        if (capacity != 0) {
            return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL, output_length);
        }
        *output_length = width;
        return true;
    }
    if (capacity < width) {
        return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL, output_length);
    }
    write_little_endian(output, (uint64_t)value, width);
    *output_length = width;
    if (failure) {
        *failure = KVLANG_XVALUE_TEXT_OK;
    }
    return true;
}

static bool unsigned_to_bytes(uint32_t width, kvlangXvalueStringView text,
                              uint8_t *output, size_t capacity,
                              size_t *output_length,
                              kvlangXvalueTextFailure *failure) {
    char buffer[32];
    if (text.size + 1 > sizeof buffer ||
        snprintf(buffer, sizeof buffer, "%.*s", (int)text.size, text.data) < 0) {
        return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT, output_length);
    }
    /* 负号一律拒绝：strtoull 会把 "-1" 静默转成最大值。 */
    if (buffer[0] == '-') {
        return fail(failure, KVLANG_XVALUE_TEXT_OUT_OF_RANGE, output_length);
    }
    char *end = NULL;
    errno = 0;
    const unsigned long long value = strtoull(buffer, &end, 10);
    if (end == buffer || *end != '\0' || errno == ERANGE) {
        return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT, output_length);
    }
    if (width < 8 && value >= (1ULL << (width * 8))) {
        return fail(failure, KVLANG_XVALUE_TEXT_OUT_OF_RANGE, output_length);
    }
    if (output == NULL) {
        if (capacity != 0) {
            return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL, output_length);
        }
        *output_length = width;
        return true;
    }
    if (capacity < width) {
        return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL, output_length);
    }
    write_little_endian(output, value, width);
    *output_length = width;
    if (failure) {
        *failure = KVLANG_XVALUE_TEXT_OK;
    }
    return true;
}

static bool bool_to_bytes(kvlangXvalueStringView text, uint8_t *output,
                          size_t capacity, size_t *output_length,
                          kvlangXvalueTextFailure *failure) {
    /* bool 的文本形态只有 true/false —— 不接受 1/0：那是 int8 的写法，
       两套写法会让「同一个值」有两个来源。 */
    uint8_t value = 0;
    if (text.size == 4 && memcmp(text.data, "true", 4) == 0) {
        value = 1;
    } else if (text.size == 5 && memcmp(text.data, "false", 5) == 0) {
        value = 0;
    } else {
        return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT, output_length);
    }
    if (output == NULL) {
        if (capacity != 0) {
            return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL, output_length);
        }
        *output_length = 1;
        return true;
    }
    if (capacity < 1) {
        return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL, output_length);
    }
    output[0] = value;
    *output_length = 1;
    if (failure) {
        *failure = KVLANG_XVALUE_TEXT_OK;
    }
    return true;
}

/* ── 浮点文本 ↔ 字节 ───────────────────────────────────────────────── */

static bool float_to_bytes(kvlangXvalueStringView text, uint32_t width,
                           uint8_t *output, size_t capacity,
                           size_t *output_length,
                           kvlangXvalueTextFailure *failure) {
    char buffer[64];
    if (text.size + 1 > sizeof buffer ||
        snprintf(buffer, sizeof buffer, "%.*s", (int)text.size, text.data) < 0) {
        return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT, output_length);
    }
    char *end = NULL;
    errno = 0;
    const double value = strtod(buffer, &end);
    if (end == buffer || *end != '\0' || errno == ERANGE) {
        return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT, output_length);
    }
    if (output == NULL) {
        if (capacity != 0) {
            return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL, output_length);
        }
        *output_length = width;
        return true;
    }
    if (capacity < width) {
        return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL, output_length);
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
        *failure = KVLANG_XVALUE_TEXT_OK;
    }
    return true;
}

/* ── 字符 / 字符串 ─────────────────────────────────────────────────── */

/* 按编码把一段文本写成 body。utf8 原样；utf32 转码点序列；ascii 校验后原样。 */
static bool characters_to_bytes(kvlangXvalueCharEncoding encoding,
                                kvlangXvalueStringView text, uint8_t *output,
                                size_t capacity, size_t *output_length,
                                kvlangXvalueTextFailure *failure) {
    switch (encoding) {
    case KVLANG_XVALUE_CHAR_ENCODING_ASCII:
        for (size_t index = 0; index < text.size; index++) {
            if ((unsigned char)text.data[index] >= 0x80U) {
                return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT,
                            output_length);
            }
        }
        if (output == NULL) {
            if (capacity != 0) {
                return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL,
                            output_length);
            }
            *output_length = text.size;
            return true;
        }
        if (capacity < text.size) {
            return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL, output_length);
        }
        memcpy(output, text.data, text.size);
        *output_length = text.size;
        if (failure) {
            *failure = KVLANG_XVALUE_TEXT_OK;
        }
        return true;
    case KVLANG_XVALUE_CHAR_ENCODING_UTF8:
        if (output == NULL) {
            if (capacity != 0) {
                return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL,
                            output_length);
            }
            *output_length = text.size;
            return true;
        }
        if (capacity < text.size) {
            return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL, output_length);
        }
        /* UTF-8 输入必须自洽：不校验就会把畸形字节当成合法值存进去。 */
        {
            size_t checked = 0;
            /* 用反向转换做校验：目标缓冲给足 text.size 个码点够用。 */
            uint8_t *scratch = (uint8_t *)malloc(text.size * 4U + 4U);
            if (scratch == NULL) {
                return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL,
                            output_length);
            }
            const bool valid = kvlangXvalueCharUtf8ToUtf32(
                (const uint8_t *)text.data, text.size, scratch,
                text.size * 4U + 4U, &checked);
            free(scratch);
            if (!valid) {
                return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT,
                            output_length);
            }
        }
        memcpy(output, text.data, text.size);
        *output_length = text.size;
        if (failure) {
            *failure = KVLANG_XVALUE_TEXT_OK;
        }
        return true;
    case KVLANG_XVALUE_CHAR_ENCODING_UTF32: {
        /* 先把 UTF-8 文本转成码点，再按 4 字节小端摊开。 */
        uint8_t *codepoints = (uint8_t *)malloc(text.size * 4U + 4U);
        if (codepoints == NULL) {
            return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL, output_length);
        }
        size_t codepoint_bytes = 0;
        const bool valid = kvlangXvalueCharUtf8ToUtf32(
            (const uint8_t *)text.data, text.size, codepoints, text.size * 4U + 4U,
            &codepoint_bytes);
        if (!valid) {
            free(codepoints);
            return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT, output_length);
        }
        if (output == NULL) {
            free(codepoints);
            if (capacity != 0) {
                return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL,
                            output_length);
            }
            *output_length = codepoint_bytes;
            return true;
        }
        if (capacity < codepoint_bytes) {
            free(codepoints);
            return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL, output_length);
        }
        memcpy(output, codepoints, codepoint_bytes);
        free(codepoints);
        *output_length = codepoint_bytes;
        if (failure) {
            *failure = KVLANG_XVALUE_TEXT_OK;
        }
        return true;
    }
    case KVLANG_XVALUE_CHAR_ENCODING_NONE:
        break;
    }
    return fail(failure, KVLANG_XVALUE_TEXT_UNSUPPORTED_TYPE, output_length);
}

/* ── 文本 → body（主入口）─────────────────────────────────────────── */

static bool text_to_bytes_impl(kvlangXvalueStringView langtype,
                                  kvlangXvalueStringView value_text,
                                  uint8_t *output, size_t output_capacity,
                                  size_t *output_length,
                                  kvlangXvalueTextFailure *failure) {
    if (failure) {
        *failure = KVLANG_XVALUE_TEXT_OK;
    }
    if (output_length == NULL) {
        return fail(failure, KVLANG_XVALUE_TEXT_BAD_LANGTYPE, NULL);
    }
    *output_length = 0;

    kvlangXvalueLangtype parsed;
    kvlangXvalueLangtypeParse(langtype, &parsed);

    /* 标量：宽度与符号性来自解析结果，文本解释按它走。 */
    if (parsed.form == KVLANG_XVALUE_FORM_SCALAR) {
        switch (parsed.scalar) {
        case KVLANG_XVALUE_SCALAR_SIGNED_INTEGER:
            return signed_to_bytes(parsed.scalar_width, value_text, output,
                                   output_capacity, output_length, failure);
        case KVLANG_XVALUE_SCALAR_UNSIGNED_INTEGER:
            return unsigned_to_bytes(parsed.scalar_width, value_text, output,
                                     output_capacity, output_length, failure);
        case KVLANG_XVALUE_SCALAR_FLOAT:
            return float_to_bytes(value_text, parsed.scalar_width, output,
                                  output_capacity, output_length, failure);
        case KVLANG_XVALUE_SCALAR_BOOLEAN:
            return bool_to_bytes(value_text, output, output_capacity,
                                 output_length, failure);
        case KVLANG_XVALUE_SCALAR_NONE:
            break;
        }
        return fail(failure, KVLANG_XVALUE_TEXT_BAD_LANGTYPE, output_length);
    }

    /* 字符：单字符与字符串共用编码，区别只在语义（这里都按字节写）。 */
    if (parsed.form == KVLANG_XVALUE_FORM_CHARACTER ||
        parsed.form == KVLANG_XVALUE_FORM_STRING) {
        return characters_to_bytes(parsed.char_encoding, value_text, output,
                                   output_capacity, output_length, failure);
    }

    /* 其余形态（ARRAY 定长 / TUPLE / MAPPING / UNRECOGNIZED）没有唯一文本形态。
       **不猜格式**——猜出来的格式会被当成标准，然后所有人跟着用。 */
    return fail(failure, KVLANG_XVALUE_TEXT_UNSUPPORTED_TYPE, output_length);
}

/* ── body → 文本（主入口）─────────────────────────────────────────── */

static bool finish_text(const char *source, size_t length, char *output,
                        size_t capacity, size_t *output_length,
                        kvlangXvalueTextFailure *failure) {
    if (output == NULL) {
        if (capacity != 0) {
            return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL, output_length);
        }
        *output_length = length;
        return true;
    }
    if (capacity <= length) {
        return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL, output_length);
    }
    memcpy(output, source, length);
    output[length] = '\0';
    *output_length = length;
    if (failure) {
        *failure = KVLANG_XVALUE_TEXT_OK;
    }
    return true;
}

static bool text_from_bytes_impl(kvlangXvalueStringView langtype,
                                 const uint8_t *body_data, size_t body_size,
                                 char *output,
                                 size_t output_capacity, size_t *output_length,
                                 kvlangXvalueTextFailure *failure) {
    if (failure) {
        *failure = KVLANG_XVALUE_TEXT_OK;
    }
    if (output_length == NULL) {
        return fail(failure, KVLANG_XVALUE_TEXT_BAD_LANGTYPE, NULL);
    }
    *output_length = 0;

    kvlangXvalueLangtype parsed;
    kvlangXvalueLangtypeParse(langtype, &parsed);

    if (parsed.form == KVLANG_XVALUE_FORM_SCALAR) {
        char buffer[64];
        int written = 0;
        switch (parsed.scalar) {
        case KVLANG_XVALUE_SCALAR_SIGNED_INTEGER: {
            if (body_size != parsed.scalar_width) {
                return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT,
                            output_length);
            }
            const uint64_t raw = read_little_endian(body_data, parsed.scalar_width);
            int64_t value = (int64_t)raw;
            /* 符号扩展：宽度不足 8 时把高位补成符号位。 */
            if (parsed.scalar_width < 8) {
                const uint32_t shift = 64U - parsed.scalar_width * 8U;
                value = ((int64_t)(raw << shift)) >> shift;
            }
            written = snprintf(buffer, sizeof buffer, "%lld", (long long)value);
            break;
        }
        case KVLANG_XVALUE_SCALAR_UNSIGNED_INTEGER: {
            if (body_size != parsed.scalar_width) {
                return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT,
                            output_length);
            }
            const uint64_t value = read_little_endian(body_data, parsed.scalar_width);
            written = snprintf(buffer, sizeof buffer, "%llu",
                               (unsigned long long)value);
            break;
        }
        case KVLANG_XVALUE_SCALAR_FLOAT: {
            if (body_size != parsed.scalar_width) {
                return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT,
                            output_length);
            }
            const uint64_t raw = read_little_endian(body_data, parsed.scalar_width);
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
            const kvlangXvalueFloatDomain domain =
                parsed.scalar_width == 4 ? KVLANG_XVALUE_FLOAT_DOMAIN_FLOAT32
                                         : KVLANG_XVALUE_FLOAT_DOMAIN_FLOAT64;
            if (!float_shortest(value, domain, buffer,
                                                sizeof buffer, &float_length)) {
                return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT,
                            output_length);
            }
            return finish_text(buffer, float_length, output, output_capacity,
                               output_length, failure);
        }
        case KVLANG_XVALUE_SCALAR_BOOLEAN: {
            if (body_size != 1) {
                return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT,
                            output_length);
            }
            const char *text = body_data[0] ? "true" : "false";
            return finish_text(text, strlen(text), output, output_capacity,
                               output_length, failure);
        }
        case KVLANG_XVALUE_SCALAR_NONE:
            return fail(failure, KVLANG_XVALUE_TEXT_BAD_LANGTYPE, output_length);
        }
        if (written <= 0 || (size_t)written >= sizeof buffer) {
            return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT, output_length);
        }
        return finish_text(buffer, (size_t)written, output, output_capacity,
                           output_length, failure);
    }

    if (parsed.form == KVLANG_XVALUE_FORM_CHARACTER ||
        parsed.form == KVLANG_XVALUE_FORM_STRING) {
        switch (parsed.char_encoding) {
        case KVLANG_XVALUE_CHAR_ENCODING_UTF8:
        case KVLANG_XVALUE_CHAR_ENCODING_ASCII:
            /* 两种编码的字节就是文本本身；ASCII 已在写入时校验过。 */
            return finish_text((const char *)body_data, body_size, output,
                               output_capacity, output_length, failure);
        case KVLANG_XVALUE_CHAR_ENCODING_UTF32: {
            if (body_size % 4U != 0) {
                return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT,
                            output_length);
            }
            if (output == NULL) {
                if (output_capacity != 0) {
                    return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL,
                                output_length);
                }
                /* UTF-8 最长 4 字节/码点，给上界即可。 */
                *output_length = body_size;
                return true;
            }
            size_t encoded = 0;
            /* 编码器**不写** NUL 结尾（见 char_codec.h：那个字节留给调用方），
               所以这里必须自己补——不补的话读回的是缓冲区里的陈旧尾巴，
               看起来像「值多出几个字符」。 */
            if (!kvlangXvalueCharUtf32ToUtf8(body_data, body_size, output,
                                             output_capacity, &encoded)) {
                return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL,
                            output_length);
            }
            if (encoded >= output_capacity) {
                return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL,
                            output_length);
            }
            output[encoded] = '\0';
            *output_length = encoded;
            if (failure) {
                *failure = KVLANG_XVALUE_TEXT_OK;
            }
            return true;
        }
        case KVLANG_XVALUE_CHAR_ENCODING_NONE:
            break;
        }
    }

    return fail(failure, KVLANG_XVALUE_TEXT_UNSUPPORTED_TYPE, output_length);
}

const char *kvlangXvalueTextFailureText(kvlangXvalueTextFailure failure) {
    switch (failure) {
    case KVLANG_XVALUE_TEXT_OK:
        return "ok";
    case KVLANG_XVALUE_TEXT_BAD_LANGTYPE:
        return "langtype 语法畸形";
    case KVLANG_XVALUE_TEXT_UNSUPPORTED_TYPE:
        return "该类型不支持文本解释";
    case KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT:
        return "文本不是该类型的合法表示";
    case KVLANG_XVALUE_TEXT_OUT_OF_RANGE:
        return "数值超出该类型的表示范围";
    case KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL:
        return "输出缓冲不足（不截断）";
    }
    return "未知失败";
}

/* ── 公共 ABI：裸指针入口 ──────────────────────────────────────────────
 *
 * 对外只有这一种形态：调用方给指针与长度，不引入 view 类型；内部实现仍是 view。
 * 裸指针让 Go/Python/Rust 绑定不必理解 C 结构体布局。 */

bool kvlangXvalueTextToBytes(const char *langtype, size_t langtype_length,
                             const char *value_text, size_t value_text_length,
                             uint8_t *output, size_t output_capacity,
                             size_t *output_length,
                             kvlangXvalueTextFailure *failure) {
    return text_to_bytes_impl(
        kvlangXvalueStringViewMake(langtype, langtype_length),
        kvlangXvalueStringViewMake(value_text, value_text_length), output,
        output_capacity, output_length, failure);
}

bool kvlangXvalueTextFromBytes(const char *langtype, size_t langtype_length,
                               const uint8_t *body, size_t body_length,
                               char *output, size_t output_capacity,
                               size_t *output_length,
                               kvlangXvalueTextFailure *failure) {
    return text_from_bytes_impl(
        kvlangXvalueStringViewMake(langtype, langtype_length),
        body, body_length, output, output_capacity,
        output_length, failure);
}

/*
 * ── 浮点值 → 文本（原 float_format 组件，唯一调用方就在这里，已并入本文件）
 * 作用：最短往返表示。按**三件正交的事**分段实现。
 * 约束：纯函数、不分配；缓冲不足返回 false 且不写部分内容。
 *
 * ── 为什么分三段，而不是「%.*g 之后再判断要不要改」────────────────────
 *
 * 浮点文本化是三个**互相独立**的维度，各自有各自的判据来源：
 *
 *   | 维度       | 决定什么            | 谁定                        |
 *   |------------|---------------------|-----------------------------|
 *   | 最短往返   | 有效数字几位        | 值本身（按宽度定往返域）    |
 *   | 定点/指数  | `100.0` vs `1e+02`  | 阈值 `[1e-4, 1e16)`         |
 *   | `.0` 规则  | `12` vs `12.0`      | 整数形态必须有小数点        |
 *
 * 把它们压进一个 `%.*g` 再打补丁，就是**把三维问题压回一维**——`%g` 一位参数
 * 同时决定有效数字位数与定点/指数切换，两者没法分开调。所以这里让 `%g` 完全
 * 不参与判断：先用 `%.*e` 拿到「有效数字串 + 十进制指数」这一份中性数据，
 * 再由三段各自决定输出形态。
 */


#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* double 的十进制往返上限是 16 位小数（= 17 位有效数字）；float 是 8 位（9 位有效）。 */
#define KVLANG_XVALUE_FLOAT_FORMAT_MAX_FRACTION_DIGITS 17

/*
 * 定点与指数的分界：十进制指数落在 **[EXPONENT_FIXED_MIN, EXPONENT_FIXED_MAX)**
 * 时用定点，之外用指数。
 *
 * 这两个数是**语言行为，不是实现细节**：`100.0` 打 `100.0` 还是 `1e+02` 会被
 * 用户直接看到。阈值取 Python `repr` 的那一对。
 *
 * **与宽度无关**：float32 与 float64 用同一对阈值。理由是这两个数回答的是
 * 「人类怎么读这个量级」，不是「这个值占几个字节」——float32 的 `1e16`
 * 与 float64 的 `1e16` 在读者眼里是同一个量级。下一个人不要以为它们是从
 * 宽度推出来的。
 */
#define KVLANG_XVALUE_FLOAT_FORMAT_FIXED_EXPONENT_MIN (-4)
#define KVLANG_XVALUE_FLOAT_FORMAT_FIXED_EXPONENT_MAX 16

/*
 * 非有限值写死。
 *
 * **不调 `%g`**：NaN 的符号位在 IEEE 754 里未指定，`printf` 的输出因此依赖平台
 * 与优化级别——`0.0/0.0` 可能得到 `nan` 也可能得到 `-nan`。那违反「同输入同输出」，
 * 而且测不出来：两次跑恰好相同是常态，坏的那次看起来像偶然。
 *
 * `inf` 虽然能被 `%g` 正确打印，也一并走这里——判据是「非有限值不做十进制往返」，
 * 比「inf 恰好能往返」更稳。
 */
static bool kvlangxvalue_float_format_non_finite(double value, char *output,
                                             size_t capacity,
                                             size_t *output_length) {
    const char *text;

    if (!isnan(value) && !isinf(value)) {
        return false;
    }
    /* NaN 的符号位无意义：`nan` 与 `-nan` 是同一个值，输出必须相同。 */
    if (isnan(value)) {
        text = "nan";
    } else {
        text = signbit(value) != 0 ? "-inf" : "inf";
    }
    {
        const size_t length = strlen(text);

        if (length >= capacity) {
            return false;
        }
        memcpy(output, text, length);
        output[length] = '\0';
        *output_length = length;
    }
    return true;
}

/*
 * ── 第一段：最短往返 ──────────────────────────────────────────────────
 *
 * 只回答「有效数字要几位」。产出两份中性数据：
 *   `digits` —— 有效数字串（不含小数点，如 `"1234567890123"`）；
 *   `exponent` —— 十进制指数（值 = 0.digits × 10^(exponent+1)，即首位数字的权重）。
 *
 * **往返域由 `domain` 决定，不由参数类型决定**：`value` 恒是 double，但 float32
 * 值的往返判据必须是 `strtof`——否则会打出那些在 float32 里根本不存在的尾数。
 */
static bool kvlangXvalueFloatShortest_digits(
    double value, kvlangXvalueFloatDomain domain, char *digits,
    size_t digits_capacity, int *exponent, bool *negative) {
    int fraction;
    bool found = false;

    for (fraction = 0; fraction <= KVLANG_XVALUE_FLOAT_FORMAT_MAX_FRACTION_DIGITS;
         ++fraction) {
        char candidate[64];
        const int written = snprintf(candidate, sizeof(candidate), "%.*e",
                                     fraction, value);

        if (written <= 0 || (size_t)written >= sizeof(candidate)) {
            return false;
        }
        if (domain == KVLANG_XVALUE_FLOAT_DOMAIN_FLOAT32) {
            found = strtof(candidate, NULL) == (float)value;
        } else {
            found = strtod(candidate, NULL) == value;
        }
        if (!found) {
            continue;
        }
        /* `%.*e` 恒形如 `d[.ddd]e±xx`：整数部分一位，之后是 '.' 与小数、再是 'e'。 */
        {
            /*
             * `%.*e` 对负值形如 `-1.23e+02`：**负号在最前面**，不在有效数字里。
             * 有效数字从 `sign` 之后开始——把负号当首位数字，会拼出 `-.0` 这种串
             * （实测过一次：所有负值的输出全错）。
             */
            const char *sign = candidate[0] == '-' ? candidate + 1 : candidate;

            *negative = candidate[0] == '-';
            const char *dot = strchr(sign, '.');
            const char *mark = strchr(sign, 'e');
            size_t integral = (size_t)((dot != NULL ? dot : mark) - sign);
            size_t count = 0U;

            if (integral >= digits_capacity) {
                return false;
            }
            digits[count++] = sign[0];
            if (dot != NULL && mark != NULL) {
                const size_t fractional = (size_t)(mark - dot - 1);

                if (count + fractional >= digits_capacity) {
                    return false;
                }
                memcpy(digits + count, dot + 1, fractional);
                count += fractional;
            }
            digits[count] = '\0';
            *exponent = mark != NULL ? atoi(mark + 1) : 0;
            return true;
        }
        return true;
    }
    /* 17 位小数都读不回来，说明实现有错——不降级、不猜。 */
    return false;
}

/* 去掉有效数字串尾部的零；保留至少一位（`1000` → `1`）。 */
static void kvlangxvalue_float_format_trim_digits(char *digits) {
    size_t length = strlen(digits);

    while (length > 1U && digits[length - 1U] == '0') {
        digits[length - 1U] = '\0';
        --length;
    }
}

/*
 * ── 第二段：定点还是指数 ──────────────────────────────────────────────
 *
 * 纯阈值判断，只看十进制指数。**与值、与宽度都无关**——这是它能被单独写一段的
 * 原因：判据只依赖 `exponent` 一个整数。
 */
static bool kvlangxvalue_float_format_use_fixed(int exponent) {
    return exponent >= KVLANG_XVALUE_FLOAT_FORMAT_FIXED_EXPONENT_MIN &&
           exponent < KVLANG_XVALUE_FLOAT_FORMAT_FIXED_EXPONENT_MAX;
}

/*
 * ── 第三段：拼串 ──────────────────────────────────────────────────────
 *
 * 输入是前两段的产物（有效数字 + 指数 + 用不用定点），输出最终文本。
 * 三段里只有这一段接触输出缓冲。
 */

/* 指数形态：`d[.ddd]e±XX`，指数至少两位（`1e+02`、`5e-324`）。 */
static bool kvlangxvalue_float_format_write_exponential(const char *digits,
                                                    int exponent,
                                                    bool negative, char *output,
                                                    size_t capacity,
                                                    size_t *output_length) {
    char tail[32];
    int written;

    if (digits[1] != '\0') {
        written = snprintf(tail, sizeof(tail), "e%c%02d",
                           exponent < 0 ? '-' : '+',
                           exponent < 0 ? -exponent : exponent);
    } else {
        written = snprintf(tail, sizeof(tail), "e%c%02d",
                           exponent < 0 ? '-' : '+',
                           exponent < 0 ? -exponent : exponent);
    }
    if (written <= 0 || (size_t)written >= sizeof(tail)) {
        return false;
    }
    {
        const size_t digits_length = strlen(digits);
        const size_t tail_length = (size_t)written;
        const size_t separated = digits[1] != '\0' ? 1U : 0U; /* '.' */
        const size_t sign_length = negative ? 1U : 0U;
        const size_t total = sign_length + digits_length + separated + tail_length;

        if (total >= capacity) {
            return false;
        }
        {
            size_t cursor = 0U;

            if (negative) {
                output[cursor++] = '-';
            }
            output[cursor++] = digits[0];

            if (separated != 0U) {
                output[cursor++] = '.';
                memcpy(output + cursor, digits + 1, digits_length - 1U);
                cursor += digits_length - 1U;
            }
            memcpy(output + cursor, tail, tail_length);
            cursor += tail_length;
            output[cursor] = '\0';
            *output_length = cursor;
        }
    }
    return true;
}

/*
 * 定点形态：按指数把小数点放在正确位置，缺位补零。
 *
 * 同时承担 `.0` 规则：**整数形态必须带小数点**，否则 `12` 会被读成 int64。
 * 这条判据是「值的 kind 是浮点」，在拼串这一步落地——所以 `.0` 不是独立一段，
 * 它只在这一段里多写两个字符。
 */
static bool kvlangxvalue_float_format_write_fixed(const char *digits, int exponent,
                                              bool negative, char *output,
                                              size_t capacity,
                                              size_t *output_length) {
    const size_t digits_length = strlen(digits);
    const size_t sign_length = negative ? 1U : 0U;
    size_t cursor = 0U;
    size_t total;
    int ordinal;

    if (exponent >= 0) {
        const size_t integral = (size_t)exponent + 1U;
        const size_t fractional = digits_length > integral
                                      ? digits_length - integral
                                      : 0U;

        /* 整数形态补 `.0`；否则写小数部分。 */
        total = sign_length + integral +
                (fractional != 0U ? 1U + fractional : 2U);
    } else {
        const size_t leading_zeros = (size_t)(-exponent) - 1U;

        total = sign_length + 2U + leading_zeros + digits_length;
    }
    /*
     * **先算总长、够才写**：先写一部分再发现放不下，会在输出里留下半个结果，
     * 而调用方只看返回值的话不知道缓冲已被污染。契约要求「不足即失败且不写部分」。
     */
    if (total >= capacity) {
        return false;
    }
    if (negative) {
        output[cursor++] = '-';
    }
    if (exponent >= 0) {
        const size_t integral = (size_t)exponent + 1U;

        for (ordinal = 0; ordinal < (int)integral; ++ordinal) {
            output[cursor++] = (size_t)ordinal < digits_length
                                   ? digits[ordinal]
                                   : '0';
        }
        if (digits_length > integral) {
            const size_t fractional = digits_length - integral;

            output[cursor++] = '.';
            memcpy(output + cursor, digits + integral, fractional);
            cursor += fractional;
        } else {
            /* 整数形态：补 `.0`，否则读者分不出这是浮点。 */
            output[cursor++] = '.';
            output[cursor++] = '0';
        }
    } else {
        const size_t leading_zeros = (size_t)(-exponent) - 1U;
        size_t pad;

        output[cursor++] = '0';
        output[cursor++] = '.';
        for (pad = 0U; pad < leading_zeros; ++pad) {
            output[cursor++] = '0';
        }
        memcpy(output + cursor, digits, digits_length);
        cursor += digits_length;
    }
    output[cursor] = '\0';
    *output_length = cursor;
    return true;
}

static bool float_shortest(double value, kvlangXvalueFloatDomain domain,
                           char *output, size_t capacity,
                           size_t *output_length) {
    char digits[32];
    int exponent = 0;
    bool negative = false;

    if (output == NULL || output_length == NULL) {
        return false;
    }
    *output_length = 0U;
    if (capacity == 0U) {
        return false;
    }
    /* 第零步：非有限值不走十进制往返，直接写死。 */
    if (kvlangxvalue_float_format_non_finite(value, output, capacity,
                                         output_length)) {
        return true;
    }
    /* 第一段：最短往返 → 有效数字 + 指数。 */
    if (!kvlangXvalueFloatShortest_digits(value, domain, digits,
                                               sizeof(digits), &exponent,
                                               &negative)) {
        return false;
    }
    kvlangxvalue_float_format_trim_digits(digits);
    /* 第二段：阈值决定形态。 */
    if (kvlangxvalue_float_format_use_fixed(exponent)) {
        /* 第三段：拼串（含 `.0` 规则）。 */
        return kvlangxvalue_float_format_write_fixed(digits, exponent, negative,
                                                  output, capacity,
                                                  output_length);
    }
    return kvlangxvalue_float_format_write_exponential(digits, exponent, negative,
                                                    output, capacity,
                                                    output_length);
}
