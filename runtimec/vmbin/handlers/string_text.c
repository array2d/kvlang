
#include "vmbin/handlers/string_text.h"

#include <string.h>

#include "vmbin/value_access.h"

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

static runtimec_status_t string_decode(runtimec_string_view_t langtype,
                                       const uint8_t *bytes, size_t size,
                                       string_text_t *output) {
    const enum string_encoding encoding = string_encoding_of(langtype);
    size_t written = 0U;

    output->count = 0U;
    switch (encoding) {
    case STRING_ENCODING_UTF32:
        
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
    
    if (!runtimec_char_utf32_to_utf8(codepoint_bytes, text->count * 4U,
                                     (char *)output, capacity, output_size)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t string_text_decode_argument(
    const binrwir_binvthread_frame_t *frame, const binrwir_argument_t *argument,
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

runtimec_status_t string_text_decode(runtimec_bytes_view_t bytes,
                                     runtimec_string_view_t langtype,
                                     string_text_t *output) {
    output->count = 0U;
    return string_decode(langtype, bytes.data, bytes.size, output);
}

bool string_text_encoding_is_fixed_width(runtimec_string_view_t langtype) {
    return string_encoding_of(langtype) == STRING_ENCODING_UTF32;
}

static runtimec_string_view_t string_encoding_name(enum string_encoding encoding) {
    switch (encoding) {
    case STRING_ENCODING_UTF8:
        return runtimec_string_view_from_c_string("char/utf8");
    case STRING_ENCODING_UTF32:
        return runtimec_string_view_from_c_string("char/utf32");
    case STRING_ENCODING_ASCII:
        return runtimec_string_view_from_c_string("char/ascii");
    case STRING_ENCODING_INVALID:
        break;
    }
    return runtimec_string_view_make(NULL, 0U);
}

runtimec_status_t string_text_common_encoding(runtimec_string_view_t left,
                                              runtimec_string_view_t right,
                                              runtimec_string_view_t *output) {
    const enum string_encoding left_encoding = string_encoding_of(left);
    const enum string_encoding right_encoding = string_encoding_of(right);

    *output = runtimec_string_view_make(NULL, 0U);
    if (left_encoding == STRING_ENCODING_INVALID ||
        right_encoding == STRING_ENCODING_INVALID) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (left_encoding == right_encoding) {
        *output = string_encoding_name(left_encoding);
        return RUNTIMEC_STATUS_SUCCESS;
    }
    
    if (left_encoding == STRING_ENCODING_ASCII) {
        *output = string_encoding_name(right_encoding);
        return RUNTIMEC_STATUS_SUCCESS;
    }
    if (right_encoding == STRING_ENCODING_ASCII) {
        *output = string_encoding_name(left_encoding);
        return RUNTIMEC_STATUS_SUCCESS;
    }
    return RUNTIMEC_STATUS_INVALID;
}
