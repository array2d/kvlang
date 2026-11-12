
#include "contract/member_key_text.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "contract/char_codec.h"
#include "contract/float_format.h"
#include "contract/langtype.h"

#define RUNTIMEC_MEMBER_KEY_TEXT_EXPONENT_FIXED_MIN (-4)
#define RUNTIMEC_MEMBER_KEY_TEXT_EXPONENT_FIXED_MAX 16

#define RUNTIMEC_MEMBER_KEY_TEXT_INTEGER_CAPACITY 24U

#define RUNTIMEC_MEMBER_KEY_TEXT_FLOAT_CAPACITY 40U

static runtimec_status_t runtimec_member_key_text_fail(
    runtimec_error_t *error, runtimec_status_t status,
    runtimec_member_key_text_diagnostic_t diagnostic, const char *message,
    char *output, size_t *output_length) {
    
    if (output_length != NULL) {
        *output_length = 0U;
    }
    if (error != NULL) {
        runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                           (int64_t)diagnostic,
                           runtimec_string_view_from_c_string(message),
                           RUNTIMEC_PROGRAM_COUNTER_NONE);
    }
    (void)output;
    return status;
}

static runtimec_status_t runtimec_member_key_text_emit(
    const char *text, size_t text_length, char *output, size_t capacity,
    size_t *output_length, runtimec_error_t *error) {
    if (text_length + 1U > capacity) {
        if (output_length != NULL) {
            *output_length = text_length;
        }
        if (error != NULL) {
            runtimec_error_set(error, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                               RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                               (int64_t)RUNTIMEC_MEMBER_KEY_TEXT_DIAG_INSUFFICIENT_CAPACITY,
                               runtimec_string_view_from_c_string(
                                   "member key buffer is too small"),
                               RUNTIMEC_PROGRAM_COUNTER_NONE);
        }
        return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
    }
    memcpy(output, text, text_length);
    output[text_length] = '\0';
    *output_length = text_length;
    return RUNTIMEC_STATUS_SUCCESS;
}

static uint64_t runtimec_member_key_text_load_unsigned(const uint8_t *body,
                                                        size_t width) {
    uint64_t value = 0U;

    for (size_t index = 0U; index < width; ++index) {
        value |= (uint64_t)body[index] << (8U * index);
    }
    return value;
}

static runtimec_status_t runtimec_member_key_text_integer(
    const runtimec_langtype_t *type, runtimec_bytes_view_t body,
    char *output, size_t capacity, size_t *output_length,
    runtimec_error_t *error) {
    char text[RUNTIMEC_MEMBER_KEY_TEXT_INTEGER_CAPACITY];
    const uint64_t raw = runtimec_member_key_text_load_unsigned(body.data,
                                                               body.size);
    int written;

    if (type->scalar == RUNTIMEC_LANGTYPE_SCALAR_SIGNED_INTEGER) {
        const unsigned shift = (unsigned)(64U - 8U * type->scalar_width);
        const int64_t value = (int64_t)(raw << shift) >> shift;

        written = snprintf(text, sizeof(text), "%lld", (long long)value);
    } else {
        written = snprintf(text, sizeof(text), "%llu",
                           (unsigned long long)raw);
    }
    if (written <= 0 || (size_t)written >= sizeof(text)) {
        return runtimec_member_key_text_fail(
            error, RUNTIMEC_STATUS_ERROR,
            RUNTIMEC_MEMBER_KEY_TEXT_DIAG_MALFORMED_BODY,
            "integer member key could not be formatted", output, output_length);
    }
    return runtimec_member_key_text_emit(text, (size_t)written, output, capacity,
                                         output_length, error);
}

static runtimec_status_t runtimec_member_key_text_boolean(
    runtimec_bytes_view_t body, char *output, size_t capacity,
    size_t *output_length, runtimec_error_t *error) {
    const char *text = body.data[0] != 0U ? "true" : "false";

    return runtimec_member_key_text_emit(text, strlen(text), output, capacity,
                                         output_length, error);
}

static runtimec_status_t runtimec_member_key_text_float(
    const runtimec_langtype_t *type, runtimec_bytes_view_t body,
    char *output, size_t capacity, size_t *output_length,
    runtimec_error_t *error) {
    char digits[32];
    char text[RUNTIMEC_MEMBER_KEY_TEXT_FLOAT_CAPACITY];
    int exponent = 0;
    bool negative = false;
    double value = 0.0;
    size_t cursor = 0U;
    const runtimec_float_domain_t domain =
        type->scalar_width == 4U ? RUNTIMEC_FLOAT_DOMAIN_FLOAT32
                                 : RUNTIMEC_FLOAT_DOMAIN_FLOAT64;

    if (type->scalar_width == 4U) {
        float narrow = 0.0f;

        memcpy(&narrow, body.data, sizeof(narrow));
        
        value = (double)narrow;
    } else {
        memcpy(&value, body.data, sizeof(value));
    }
    if (isnan(value)) {
        return runtimec_member_key_text_fail(
            error, RUNTIMEC_STATUS_INVALID,
            RUNTIMEC_MEMBER_KEY_TEXT_DIAG_NON_FINITE,
            "floating member key must not be NaN: NaN has no key text",
            output, output_length);
    }
    if (isinf(value)) {
        char infinite[RUNTIMEC_MEMBER_KEY_TEXT_FLOAT_CAPACITY];
        size_t length = 0U;

        if (!runtimec_float_format_non_finite_text(value, infinite,
                                                   sizeof(infinite), &length)) {
            return runtimec_member_key_text_fail(
                error, RUNTIMEC_STATUS_ERROR,
                RUNTIMEC_MEMBER_KEY_TEXT_DIAG_MALFORMED_BODY,
                "infinite member key could not be formatted", output,
                output_length);
        }
        return runtimec_member_key_text_emit(infinite, length, output, capacity,
                                             output_length, error);
    }
    if (!runtimec_float_format_core(value, domain, digits, sizeof(digits),
                                    &exponent, &negative)) {
        return runtimec_member_key_text_fail(
            error, RUNTIMEC_STATUS_ERROR,
            RUNTIMEC_MEMBER_KEY_TEXT_DIAG_MALFORMED_BODY,
            "floating member key shortest round-trip failed", output,
            output_length);
    }
    
    if (negative && strcmp(digits, "0") == 0) {
        negative = false;
    }
    if (negative) {
        text[cursor++] = '-';
    }
    if (exponent >= RUNTIMEC_MEMBER_KEY_TEXT_EXPONENT_FIXED_MIN &&
        exponent < RUNTIMEC_MEMBER_KEY_TEXT_EXPONENT_FIXED_MAX) {
        const size_t digit_count = strlen(digits);

        if (exponent >= 0) {
            const size_t integral = (size_t)exponent + 1U;

            for (size_t ordinal = 0U; ordinal < integral; ++ordinal) {
                text[cursor++] = ordinal < digit_count ? digits[ordinal] : '0';
            }
            if (digit_count > integral) {
                text[cursor++] = '.';
                memcpy(text + cursor, digits + integral, digit_count - integral);
                cursor += digit_count - integral;
            }
            
        } else {
            const size_t leading_zeros = (size_t)(-exponent) - 1U;

            text[cursor++] = '0';
            text[cursor++] = '.';
            for (size_t pad = 0U; pad < leading_zeros; ++pad) {
                text[cursor++] = '0';
            }
            memcpy(text + cursor, digits, digit_count);
            cursor += digit_count;
        }
    } else {
        const size_t digit_count = strlen(digits);
        int written;

        if (digit_count > 1U) {
            text[cursor++] = digits[0];
            text[cursor++] = '.';
            memcpy(text + cursor, digits + 1U, digit_count - 1U);
            cursor += digit_count - 1U;
        } else {
            text[cursor++] = digits[0];
        }
        written = snprintf(text + cursor, sizeof(text) - cursor, "e%c%02d",
                           exponent < 0 ? '-' : '+',
                           exponent < 0 ? -exponent : exponent);
        if (written <= 0 || (size_t)written >= sizeof(text) - cursor) {
            return runtimec_member_key_text_fail(
                error, RUNTIMEC_STATUS_ERROR,
                RUNTIMEC_MEMBER_KEY_TEXT_DIAG_MALFORMED_BODY,
                "floating member key exponent could not be formatted", output,
                output_length);
        }
        cursor += (size_t)written;
    }
    return runtimec_member_key_text_emit(text, cursor, output, capacity,
                                         output_length, error);
}

#define RUNTIMEC_MEMBER_KEY_TEXT_CHUNK_BYTES 256U

static bool runtimec_member_key_text_utf32_walk(runtimec_bytes_view_t body,
                                                char *output,
                                                size_t output_capacity,
                                                size_t *output_length) {
    size_t offset = 0U;
    size_t total = 0U;

    if (body.size % 4U != 0U) {
        return false;
    }
    while (offset < body.size) {
        char chunk[RUNTIMEC_MEMBER_KEY_TEXT_CHUNK_BYTES + 1U];
        size_t take = body.size - offset;
        size_t produced = 0U;

        if (take > RUNTIMEC_MEMBER_KEY_TEXT_CHUNK_BYTES) {
            take = RUNTIMEC_MEMBER_KEY_TEXT_CHUNK_BYTES;
        }
        take -= take % 4U;
        
        if (!runtimec_char_utf32_to_utf8(body.data + offset, take, chunk,
                                         sizeof(chunk), &produced)) {
            return false;
        }
        if (output != NULL) {
            if (total + produced > output_capacity) {
                return false;
            }
            memcpy(output + total, chunk, produced);
        }
        total += produced;
        offset += take;
    }
    *output_length = total;
    return true;
}

static runtimec_status_t runtimec_member_key_text_text(
    const runtimec_langtype_t *type, runtimec_bytes_view_t body,
    char *output, size_t capacity, size_t *output_length,
    runtimec_error_t *error) {
    if (type->char_encoding == RUNTIMEC_LANGTYPE_CHAR_ENCODING_UTF32) {
        size_t required = 0U;

        if (!runtimec_member_key_text_utf32_walk(body, NULL, 0U, &required)) {
            return runtimec_member_key_text_fail(
                error, RUNTIMEC_STATUS_INVALID,
                RUNTIMEC_MEMBER_KEY_TEXT_DIAG_MALFORMED_BODY,
                "char/utf32 body holds an invalid code point or a partial "
                "code point",
                output, output_length);
        }
        if (required + 1U > capacity) {
            if (output_length != NULL) {
                *output_length = required;
            }
            if (error != NULL) {
                runtimec_error_set(
                    error, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                    RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                    (int64_t)RUNTIMEC_MEMBER_KEY_TEXT_DIAG_INSUFFICIENT_CAPACITY,
                    runtimec_string_view_from_c_string(
                        "member key buffer is too small"),
                    RUNTIMEC_PROGRAM_COUNTER_NONE);
            }
            return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
        }
        {
            size_t written = 0U;

            if (!runtimec_member_key_text_utf32_walk(body, output, capacity,
                                                     &written)) {
                return runtimec_member_key_text_fail(
                    error, RUNTIMEC_STATUS_ERROR,
                    RUNTIMEC_MEMBER_KEY_TEXT_DIAG_MALFORMED_BODY,
                    "char/utf32 body changed between the two conversion "
                    "passes",
                    output, output_length);
            }
            output[written] = '\0';
            *output_length = written;
        }
        return RUNTIMEC_STATUS_SUCCESS;
    }
    
    return runtimec_member_key_text_emit((const char *)body.data, body.size,
                                         output, capacity, output_length, error);
}

runtimec_status_t runtimec_member_key_text(runtimec_string_view_t langtype,
                                           runtimec_bytes_view_t body,
                                           char *output, size_t capacity,
                                           size_t *output_length,
                                           runtimec_error_t *error) {
    runtimec_langtype_t type;
    runtimec_status_t status;

    if (output_length != NULL) {
        *output_length = 0U;
    }
    if (error != NULL) {
        runtimec_error_reset(error);
    }
    if (output == NULL || output_length == NULL || !runtimec_string_view_is_valid(langtype) ||
        !runtimec_bytes_view_is_valid(body)) {
        return runtimec_member_key_text_fail(
            error, RUNTIMEC_STATUS_INVALID,
            RUNTIMEC_MEMBER_KEY_TEXT_DIAG_INVALID_REQUEST,
            "member key request is malformed", output, output_length);
    }
    
    {
        for (size_t scan = 0U; scan + 1U < langtype.size; ++scan) {
            if ((uint8_t)langtype.data[scan] == 0xC2U &&
                (uint8_t)langtype.data[scan + 1U] == 0xB7U) {
                return runtimec_member_key_text_fail(
                    error, RUNTIMEC_STATUS_UNSUPPORTED,
                    RUNTIMEC_MEMBER_KEY_TEXT_DIAG_UNSUPPORTED_LANGTYPE,
                    "legacy map syntax '[K]·V' is not accepted; write 'K'V "
                    "instead (issue #631)",
                    output, output_length);
            }
        }
    }
    status = runtimec_langtype_parse(langtype, &type);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_member_key_text_fail(
            error, RUNTIMEC_STATUS_INVALID,
            RUNTIMEC_MEMBER_KEY_TEXT_DIAG_INVALID_REQUEST,
            "member key langtype is malformed", output, output_length);
    }
    
    switch (type.form) {
    case RUNTIMEC_LANGTYPE_FORM_TUPLE:
        return runtimec_member_key_text_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED,
            RUNTIMEC_MEMBER_KEY_TEXT_DIAG_UNSUPPORTED_LANGTYPE,
            "tuple keys have no value form: their text is produced by the "
            "parser from source and never converted from a body",
            output, output_length);
    case RUNTIMEC_LANGTYPE_FORM_ARRAY: {
        
        runtimec_langtype_t base_type;
        if (type.dimension_count == 1 &&
            runtimec_langtype_parse(type.base, &base_type) ==
                RUNTIMEC_STATUS_SUCCESS &&
            base_type.form == RUNTIMEC_LANGTYPE_FORM_CHARACTER) {
            return runtimec_member_key_text_text(&base_type, body, output,
                                                 capacity, output_length,
                                                 error);
        }
        return runtimec_member_key_text_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED,
            RUNTIMEC_MEMBER_KEY_TEXT_DIAG_UNSUPPORTED_LANGTYPE,
            "a shaped langtype is a value form, not a member key form",
            output, output_length);
    }
    case RUNTIMEC_LANGTYPE_FORM_MAPPING:
        
        return runtimec_member_key_text_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED,
            RUNTIMEC_MEMBER_KEY_TEXT_DIAG_UNSUPPORTED_LANGTYPE,
            "a map type is not a member key form: its formatter lives inside the "
            "quotes and its value side has no body to format",
            output, output_length);
    case RUNTIMEC_LANGTYPE_FORM_UNRECOGNIZED:
        return runtimec_member_key_text_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED,
            RUNTIMEC_MEMBER_KEY_TEXT_DIAG_UNSUPPORTED_LANGTYPE,
            "langtype cannot be a member key", output, output_length);
    case RUNTIMEC_LANGTYPE_FORM_CHARACTER:
    case RUNTIMEC_LANGTYPE_FORM_STRING:
        
        return runtimec_member_key_text_text(&type, body, output, capacity,
                                             output_length, error);
    case RUNTIMEC_LANGTYPE_FORM_SCALAR:
        break;
    }
    if (body.size != type.scalar_width) {
        return runtimec_member_key_text_fail(
            error, RUNTIMEC_STATUS_INVALID,
            RUNTIMEC_MEMBER_KEY_TEXT_DIAG_MALFORMED_BODY,
            "scalar member key body width does not match its langtype", output,
            output_length);
    }
    switch (type.scalar) {
    case RUNTIMEC_LANGTYPE_SCALAR_BOOLEAN:
        return runtimec_member_key_text_boolean(body, output, capacity,
                                                output_length, error);
    case RUNTIMEC_LANGTYPE_SCALAR_SIGNED_INTEGER:
    case RUNTIMEC_LANGTYPE_SCALAR_UNSIGNED_INTEGER:
        return runtimec_member_key_text_integer(&type, body, output, capacity,
                                                output_length, error);
    case RUNTIMEC_LANGTYPE_SCALAR_FLOAT:
        return runtimec_member_key_text_float(&type, body, output, capacity,
                                              output_length, error);
    case RUNTIMEC_LANGTYPE_SCALAR_NONE:
        break;
    }
    return runtimec_member_key_text_fail(
        error, RUNTIMEC_STATUS_UNSUPPORTED,
        RUNTIMEC_MEMBER_KEY_TEXT_DIAG_UNSUPPORTED_LANGTYPE,
        "langtype cannot be a member key", output, output_length);
}
