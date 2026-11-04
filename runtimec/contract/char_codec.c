/*
 * 组件：contract/char_codec 的实现
 * 作用：char/utf32（4 字节小端码点序列）→ char/utf8。
 * 联系：由 vmbin 的打印与字符串算子共用；本文件不依赖任何上层组件。
 * 约束：纯函数、不分配。缓冲装不下、码点越界、输入长度非 4 的倍数一律失败，
 *       不做任何截断或替换字符——半个 UTF-8 序列比失败更难发现。
 */

#include "contract/char_codec.h"

#include <string.h>

/* 把一个码点编码进 out；返回字节数，非法码点返回 0。 */
static size_t runtimec_char_encode_codepoint(uint32_t codepoint,
                                             uint8_t *out) {
    if (codepoint <= 0x7FU) {
        out[0] = (uint8_t)codepoint;
        return 1U;
    }
    if (codepoint <= 0x7FFU) {
        out[0] = (uint8_t)(0xC0U | (codepoint >> 6U));
        out[1] = (uint8_t)(0x80U | (codepoint & 0x3FU));
        return 2U;
    }
    if (codepoint <= 0xFFFFU) {
        out[0] = (uint8_t)(0xE0U | (codepoint >> 12U));
        out[1] = (uint8_t)(0x80U | ((codepoint >> 6U) & 0x3FU));
        out[2] = (uint8_t)(0x80U | (codepoint & 0x3FU));
        return 3U;
    }
    if (codepoint <= 0x10FFFFU) {
        out[0] = (uint8_t)(0xF0U | (codepoint >> 18U));
        out[1] = (uint8_t)(0x80U | ((codepoint >> 12U) & 0x3FU));
        out[2] = (uint8_t)(0x80U | ((codepoint >> 6U) & 0x3FU));
        out[3] = (uint8_t)(0x80U | (codepoint & 0x3FU));
        return 4U;
    }
    return 0U;
}

bool runtimec_char_utf32_to_utf8(const uint8_t *input, size_t input_size,
                                 char *output, size_t output_capacity,
                                 size_t *output_length) {
    size_t used = 0U;
    size_t offset;

    if (output_length != NULL) {
        *output_length = 0U;
    }
    if (output_length == NULL || output == NULL ||
        (input == NULL && input_size != 0U) || input_size % 4U != 0U) {
        return false;
    }
    for (offset = 0U; offset < input_size; offset += 4U) {
        const uint32_t codepoint = (uint32_t)input[offset] |
                                   ((uint32_t)input[offset + 1U] << 8U) |
                                   ((uint32_t)input[offset + 2U] << 16U) |
                                   ((uint32_t)input[offset + 3U] << 24U);
        uint8_t encoded[RUNTIMEC_CHAR_CODEC_UTF8_MAX_BYTES];
        const size_t encoded_size =
            runtimec_char_encode_codepoint(codepoint, encoded);

        if (encoded_size == 0U) {
            return false;
        }
        /* 保留末字节：调用方按「总能留一个位置」约定分配文本缓冲。 */
        if (used + encoded_size >= output_capacity) {
            return false;
        }
        memcpy(output + used, encoded, encoded_size);
        used += encoded_size;
    }
    *output_length = used;
    return true;
}

/* 从一个 UTF-8 序列解出一个码点；返回消耗的字节数，非法返回 0。 */
static size_t runtimec_char_decode_codepoint(const uint8_t *input,
                                             size_t available,
                                             uint32_t *output_codepoint) {
    uint32_t codepoint;
    size_t needed;
    size_t index;

    *output_codepoint = 0U;
    if (available == 0U) {
        return 0U;
    }
    if ((input[0] & 0x80U) == 0U) {
        *output_codepoint = input[0];
        return 1U;
    }
    if ((input[0] & 0xE0U) == 0xC0U) {
        needed = 2U;
        codepoint = (uint32_t)(input[0] & 0x1FU);
    } else if ((input[0] & 0xF0U) == 0xE0U) {
        needed = 3U;
        codepoint = (uint32_t)(input[0] & 0x0FU);
    } else if ((input[0] & 0xF8U) == 0xF0U) {
        needed = 4U;
        codepoint = (uint32_t)(input[0] & 0x07U);
    } else {
        return 0U;
    }
    if (available < needed) {
        return 0U;
    }
    for (index = 1U; index < needed; ++index) {
        if ((input[index] & 0xC0U) != 0x80U) {
            return 0U;
        }
        codepoint = (codepoint << 6U) | (uint32_t)(input[index] & 0x3FU);
    }
    /* 超长编码、代理区、越界都拒绝：它们能编码出「看起来对」的错值。 */
    if ((needed == 2U && codepoint < 0x80U) ||
        (needed == 3U && codepoint < 0x800U) ||
        (needed == 4U && codepoint < 0x10000U) ||
        (codepoint >= 0xD800U && codepoint <= 0xDFFFU) ||
        codepoint > 0x10FFFFU) {
        return 0U;
    }
    *output_codepoint = codepoint;
    return needed;
}

bool runtimec_char_utf8_to_utf32(const uint8_t *input, size_t input_size,
                                 uint8_t *output, size_t output_capacity,
                                 size_t *output_length) {
    size_t used = 0U;
    size_t offset = 0U;

    if (output_length != NULL) {
        *output_length = 0U;
    }
    if (output_length == NULL || output_capacity % 4U != 0U ||
        (input == NULL && input_size != 0U) || output == NULL) {
        return false;
    }
    while (offset < input_size) {
        uint32_t codepoint = 0U;
        const size_t consumed = runtimec_char_decode_codepoint(
            input + offset, input_size - offset, &codepoint);

        if (consumed == 0U || used + 4U > output_capacity) {
            return false;
        }
        output[used] = (uint8_t)(codepoint & 0xFFU);
        output[used + 1U] = (uint8_t)((codepoint >> 8U) & 0xFFU);
        output[used + 2U] = (uint8_t)((codepoint >> 16U) & 0xFFU);
        output[used + 3U] = (uint8_t)((codepoint >> 24U) & 0xFFU);
        used += 4U;
        offset += consumed;
    }
    *output_length = used;
    return true;
}

bool runtimec_char_ascii_to_utf8(const uint8_t *input, size_t input_size,
                                 char *output, size_t output_capacity,
                                 size_t *output_length) {
    size_t offset;

    if (output_length != NULL) {
        *output_length = 0U;
    }
    if (output_length == NULL || output == NULL ||
        (input == NULL && input_size != 0U)) {
        return false;
    }
    for (offset = 0U; offset < input_size; ++offset) {
        if (input[offset] >= 0x80U || offset >= output_capacity) {
            return false;
        }
        output[offset] = (char)input[offset];
    }
    *output_length = input_size;
    return true;
}
