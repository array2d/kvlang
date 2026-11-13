
#include "vmbin/handlers/string.h"

#include <stdlib.h>
#include <string.h>

#include "binrwir/execution_exit.h"
#include "binrwir/binvthread_frame.h"
#include "binrwir/instruction.h"
#include "binrwir/operation_identifiers.h"
#include "contract/char_codec.h"
#include "contract/error.h"
#include "contract/status.h"
#include "contract/view.h"
#include <pathspace/pathspace_xvalue.h>
#include "vmbin/handlers/container.h"
#include "vmbin/frame_target_key.h"
#include "vmbin/handlers/handler_call.h"
#include "vmbin/handlers/handler_call_text.h"
#include "vmbin/value_access.h"

#define STRING_KEY_CAPACITY 512U

static void string_length_of(runtimec_string_view_t langtype, size_t body_size,
                             int64_t *out,
                             binrwir_execution_exit_t *output_exit) {
    static const struct {
        const char *text;
        int is_codepoint_count;
    } table[] = {
        {"char/utf8", 0}, {"char/ascii", 0}, {"char/utf32", 1},
    };
    size_t offset = 0U;

    while (offset < langtype.size && langtype.data[offset] == '[') {
        const char *closing =
            memchr(langtype.data + offset, ']', langtype.size - offset);

        if (closing == NULL) {
            break;
        }
        offset = (size_t)(closing - langtype.data) + 1U;
    }
    for (size_t ordinal = 0U; ordinal < sizeof(table) / sizeof(table[0]);
         ++ordinal) {
        const size_t size = strlen(table[ordinal].text);

        if (langtype.size - offset == size &&
            memcmp(langtype.data + offset, table[ordinal].text, size) == 0) {
            *out = table[ordinal].is_codepoint_count != 0
                       ? (int64_t)(body_size / 4U)
                       : (int64_t)body_size;
            return;
        }
    }
    vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_UNSUPPORTED,
                       VMBIN_HANDLER_CALL_TEXT_DIAG_UNSUPPORTED_ENCODING,
                       output_exit->logical_program_counter);
}

static void string_length_build(const vmbin_value_location_t *text,
                                runtimec_string_view_t langtype, int64_t *out,
                                binrwir_execution_exit_t *output_exit) {
    string_length_of(langtype, text->size, out, output_exit);
}

static void string_character_of(const string_text_t *text, const int64_t *index,
                                runtimec_string_view_t write_langtype,
                                uint8_t *buffer, size_t capacity,
                                size_t *output_length,
                                binrwir_execution_exit_t *output_exit) {
    string_text_t single;

    *output_length = 0U;
    if (*index < 0 || (uint64_t)*index >= text->count) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    single.count = 1U;
    single.codepoints[0] = text->codepoints[*index];
    
    const runtimec_status_t encoded = string_text_encode(
        write_langtype, &single, buffer, capacity, output_length);
    if (encoded != RUNTIMEC_STATUS_SUCCESS) {
        *output_length = 0U;
        vmbin_handler_fail(output_exit,
                           encoded == RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY
                               ? RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY
                               : RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
    }
}

static inline void string_character_build(const string_text_t *text,
                                          const int64_t *index,
                                          runtimec_string_view_t write_langtype,
                                          uint8_t *buffer, size_t capacity,
                                          size_t *output_length,
                                          binrwir_execution_exit_t *output_exit) {
    string_character_of(text, index, write_langtype, buffer, capacity,
                        output_length, output_exit);
}

VMBIN_HANDLER_TEXT_INDEX_CREATE(string_character_handler, string_character_build,
                                int64_t)

static int64_t string_compare_codepoints(const string_text_t *left,
                                         const string_text_t *right) {
    const size_t shared = left->count < right->count ? left->count : right->count;
    size_t ordinal = 0U;

    for (; ordinal < shared; ++ordinal) {
        if (left->codepoints[ordinal] != right->codepoints[ordinal]) {
            return left->codepoints[ordinal] < right->codepoints[ordinal] ? -1 : 1;
        }
    }
    if (left->count == right->count) {
        return 0;
    }
    return left->count < right->count ? -1 : 1;
}

static inline void string_compare_build(const string_text_t *left,
                                        const string_text_t *right,
                                        int64_t *out) {
    *out = string_compare_codepoints(left, right);
}

VMBIN_HANDLER_BINARY_TEXT_SCALAR(string_compare_handler, string_compare_build,
                                 int64_t)

typedef struct runtimec_string_concat_result {
    uint8_t bytes[VMBIN_HANDLER_CREATE_BUFFER_CAPACITY];
    size_t length;
} runtimec_string_concat_result_t;

static void string_concat_of(const string_text_t *left,
                             const string_text_t *right,
                             runtimec_string_view_t common, uint8_t *buffer,
                             size_t capacity, size_t *output_length,
                             binrwir_execution_exit_t *output_exit) {
    string_text_t joined;

    *output_length = 0U;
    if (left->count + right->count > STRING_TEXT_CODEPOINT_CAPACITY) {
        
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_OUT_OF_MEMORY,
                           VMBIN_HANDLER_CALL_DIAG_STRING_TOO_LONG,
                           output_exit->logical_program_counter);
        return;
    }
    joined.count = left->count + right->count;
    memcpy(joined.codepoints, left->codepoints, left->count * sizeof(uint32_t));
    memcpy(joined.codepoints + left->count, right->codepoints,
           right->count * sizeof(uint32_t));
    if (string_text_encode(common, &joined, buffer, capacity, output_length) !=
        RUNTIMEC_STATUS_SUCCESS) {
        *output_length = 0U;
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
    }
}

static runtimec_status_t string_concat_pipeline(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    runtimec_string_concat_result_t *output, binrwir_execution_exit_t *output_exit) {
    runtimec_string_view_t left_langtype;
    runtimec_string_view_t right_langtype;
    runtimec_string_view_t common;
    string_text_t left_text;
    string_text_t right_text;
    runtimec_error_t error;
    runtimec_status_t status;

    runtimec_error_reset(&error);
    status = vmbin_value_access_langtype(
        frame, &instruction->read_arguments.data[0], &left_langtype, &error);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = vmbin_value_access_langtype(
            frame, &instruction->read_arguments.data[1], &right_langtype, &error);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = string_text_common_encoding(left_langtype, right_langtype,
                                             &common);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = string_text_decode_argument(
            frame, &instruction->read_arguments.data[0], &left_text, &error);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = string_text_decode_argument(
            frame, &instruction->read_arguments.data[1], &right_text, &error);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_UNSUPPORTED_LANGTYPE,
                           instruction->source_logical_program_counter);
        return RUNTIMEC_STATUS_ERROR;
    }
    string_concat_of(&left_text, &right_text, common, output->bytes,
                     sizeof(output->bytes), &output->length, output_exit);
    if (output_exit->kind != BINRWIR_EXECUTION_EXIT_NONE) {
        return RUNTIMEC_STATUS_ERROR;
    }
    return vmbin_handler_write_new_value(frame, instruction, output->bytes,
                                         output->length, output_exit);
}

static inline runtimec_status_t string_concat_execute(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    runtimec_string_concat_result_t result;

    VMBIN_HANDLER_REQUIRE_ARITY(instruction, 2U, 1U, output_exit);
    return string_concat_pipeline(frame, instruction, &result, output_exit);
}

static runtimec_status_t string_concat_handler(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return string_concat_execute(frame, instruction, output_exit);
}

VMBIN_HANDLER_UNARY_TEXT_SCALAR(string_length_handler, string_length_build,
                                int64_t)

static void string_set_in_place(const string_text_t *text,
                                const vmbin_value_location_t *write_location,
                                const int64_t *position, const string_text_t *replacement,
                                binrwir_execution_exit_t *output_exit) {
    size_t byte_offset = 0U;

    if (*position < 0 || (uint64_t)*position >= text->count ||
        replacement->count != 1U) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    
    byte_offset = (size_t)*position * 4U;
    if (byte_offset + 4U > write_location->size) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    {
        const uint32_t codepoint = replacement->codepoints[0];

        write_location->write_bytes[byte_offset] = (uint8_t)(codepoint & 0xFFU);
        write_location->write_bytes[byte_offset + 1U] =
            (uint8_t)((codepoint >> 8U) & 0xFFU);
        write_location->write_bytes[byte_offset + 2U] =
            (uint8_t)((codepoint >> 16U) & 0xFFU);
        write_location->write_bytes[byte_offset + 3U] =
            (uint8_t)((codepoint >> 24U) & 0xFFU);
    }
}

static void string_set_rebuild(const string_text_t *text,
                               const int64_t *position,
                               const string_text_t *replacement,
                               runtimec_string_view_t write_langtype,
                               uint8_t *buffer, size_t capacity,
                               size_t *output_length,
                               binrwir_execution_exit_t *output_exit) {
    string_text_t rebuilt;

    *output_length = 0U;
    if (*position < 0 || (uint64_t)*position >= text->count ||
        replacement->count != 1U) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    rebuilt = *text;
    rebuilt.codepoints[*position] = replacement->codepoints[0];
    
    if (string_text_encode(write_langtype, &rebuilt, buffer, capacity,
                           output_length) != RUNTIMEC_STATUS_SUCCESS) {
        *output_length = 0U;
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
    }
}

static inline void string_set_patch_build(
    const string_text_t *text, const vmbin_value_location_t *write_location,
    const int64_t *position, const string_text_t *replacement,
    binrwir_execution_exit_t *output_exit) {
    string_set_in_place(text, write_location, position, replacement, output_exit);
}

static inline void string_set_rebuild_build(
    const string_text_t *text, const int64_t *position,
    const string_text_t *replacement, runtimec_string_view_t write_langtype,
    uint8_t *buffer, size_t capacity, size_t *output_length,
    binrwir_execution_exit_t *output_exit) {
    string_set_rebuild(text, position, replacement, write_langtype, buffer,
                       capacity, output_length, output_exit);
}

static int64_t string_first_codepoint(const vmbin_value_location_t *text,
                                      runtimec_string_view_t langtype,
                                      binrwir_execution_exit_t *output_exit) {
    string_text_t decoded;

    if (string_text_decode(
            runtimec_bytes_view_make(text->read_bytes, text->size), langtype,
            &decoded) != RUNTIMEC_STATUS_SUCCESS ||
        decoded.count == 0U) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return 0;
    }
    return (int64_t)decoded.codepoints[0];
}

static void string_ordinal_build(const vmbin_value_location_t *text,
                                 runtimec_string_view_t langtype, int64_t *out,
                                 binrwir_execution_exit_t *output_exit) {
    *out = string_first_codepoint(text, langtype, output_exit);
}

VMBIN_HANDLER_UNARY_TEXT_SCALAR(string_ordinal_handler, string_ordinal_build,
                                int64_t)

static int64_t string_find_in(const string_text_t *haystack,
                              const string_text_t *needle) {
    size_t start = 0U;

    if (needle->count == 0U) {
        return 0;
    }
    for (; start + needle->count <= haystack->count; ++start) {
        size_t matched = 0U;

        while (matched < needle->count &&
               haystack->codepoints[start + matched] ==
                   needle->codepoints[matched]) {
            ++matched;
        }
        if (matched == needle->count) {
            return (int64_t)start;
        }
    }
    return -1;
}

static inline void string_find_build(const string_text_t *haystack,
                                     const string_text_t *needle,
                                     int64_t *out) {
    *out = string_find_in(haystack, needle);
}

VMBIN_HANDLER_BINARY_TEXT_SCALAR(string_find_handler, string_find_build, int64_t)

static void string_slice_of(const string_text_t *text,
                            const vmbin_value_location_t *bounds,
                            runtimec_string_view_t write_langtype,
                            uint8_t *buffer, size_t capacity,
                            size_t *output_length,
                            binrwir_execution_exit_t *output_exit) {
    int64_t lo = 0;
    int64_t hi = 0;
    string_text_t sliced;

    *output_length = 0U;
    if (bounds[0].size != sizeof(int64_t) || bounds[1].size != sizeof(int64_t)) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    memcpy(&lo, bounds[0].read_bytes, sizeof(lo));
    memcpy(&hi, bounds[1].read_bytes, sizeof(hi));
    if (lo < 0 || hi < lo || (uint64_t)hi > text->count) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    sliced.count = (size_t)(hi - lo);
    memcpy(sliced.codepoints, text->codepoints + lo,
           sliced.count * sizeof(uint32_t));
    if (string_text_encode(write_langtype, &sliced, buffer, capacity,
                           output_length) != RUNTIMEC_STATUS_SUCCESS) {
        *output_length = 0U;
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
    }
}

static inline void string_slice_build(const string_text_t *text,
                                      const vmbin_value_location_t *bounds,
                                      runtimec_string_view_t write_langtype,
                                      uint8_t *buffer, size_t capacity,
                                      size_t *output_length,
                                      binrwir_execution_exit_t *output_exit) {
    string_slice_of(text, bounds, write_langtype, buffer, capacity,
                    output_length, output_exit);
}

static void string_format_of(const vmbin_value_location_t *value_location,
                             int64_t base, int is_signed,
                             runtimec_string_view_t write_langtype,
                             uint8_t *buffer, size_t capacity,
                             size_t *output_length,
                             binrwir_execution_exit_t *output_exit) {
    char digits[80];
    size_t digit_count = 0U;
    uint64_t magnitude;
    int64_t value = 0;
    int negative;
    string_text_t text;

    *output_length = 0U;
    if (value_location->size != sizeof(int64_t) || base < 2 || base > 36) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    memcpy(&value, value_location->read_bytes, sizeof(value));
    negative = (is_signed != 0 && value < 0) ? 1 : 0;
    
    magnitude = negative != 0 ? (uint64_t)(-(value + 1)) + 1U : (uint64_t)value;
    do {
        const uint64_t remainder = magnitude % (uint64_t)base;

        digits[digit_count++] = remainder < 10U
                                    ? (char)('0' + remainder)
                                    : (char)('a' + (remainder - 10U));
        magnitude /= (uint64_t)base;
    } while (magnitude != 0U && digit_count < sizeof(digits));
    text.count = 0U;
    if (negative != 0) {
        text.codepoints[text.count++] = (uint32_t)'-';
    }
    while (digit_count > 0U) {
        text.codepoints[text.count++] = (uint32_t)(uint8_t)digits[--digit_count];
    }
    
    if (string_text_encode(write_langtype, &text, buffer, capacity,
                           output_length) != RUNTIMEC_STATUS_SUCCESS) {
        *output_length = 0U;
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
    }
}

static inline void string_format_build(const vmbin_value_location_t *value,
                                       int64_t base, int is_signed,
                                       runtimec_string_view_t write_langtype,
                                       uint8_t *buffer, size_t capacity,
                                       size_t *output_length,
                                       binrwir_execution_exit_t *output_exit) {
    string_format_of(value, base, is_signed, write_langtype, buffer, capacity,
                     output_length, output_exit);
}

static void string_parse_number_of(const string_text_t *text, int allow_float,
                                   int is_signed, uint8_t *output,
                                   size_t output_size,
                                   binrwir_execution_exit_t *output_exit) {
    char ascii[STRING_TEXT_CODEPOINT_CAPACITY + 1U];
    size_t ordinal = 0U;

    for (; ordinal < text->count; ++ordinal) {
        
        if (text->codepoints[ordinal] > 0x7FU) {
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                               VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                               output_exit->logical_program_counter);
            return;
        }
        ascii[ordinal] = (char)(uint8_t)text->codepoints[ordinal];
    }
    ascii[ordinal] = '\0';
    if (ordinal == 0U) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    {
        char *parse_end = NULL;
        uint8_t scratch[sizeof(double)];

        if (output_size > sizeof(scratch)) {
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                               VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                               output_exit->logical_program_counter);
            return;
        }
        memset(scratch, 0, sizeof(scratch));
        if (allow_float != 0) {
            const double parsed = strtod(ascii, &parse_end);

            memcpy(scratch, &parsed, sizeof(parsed));
        } else if (is_signed != 0) {
            const long long parsed = strtoll(ascii, &parse_end, 10);

            memcpy(scratch, &parsed, sizeof(parsed));
        } else {
            const unsigned long long parsed = strtoull(ascii, &parse_end, 10);

            memcpy(scratch, &parsed, sizeof(parsed));
        }
        
        if (parse_end == ascii || *parse_end != '\0' ||
            (is_signed == 0 && allow_float == 0 && ascii[0] == '-')) {
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                               VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                               output_exit->logical_program_counter);
            return;
        }
        memcpy(output, scratch, output_size);
    }
}

VMBIN_HANDLER_SET_BY_ENCODING(string_set_handler, string_set_patch_build,
                               string_set_rebuild_build)

VMBIN_HANDLER_TEXT_INDEX2(string_slice_handler, string_slice_build, int64_t)
VMBIN_HANDLER_FORMAT_INTEGER(string_format_integer_handler, string_format_build, 1)
VMBIN_HANDLER_FORMAT_INTEGER(string_format_unsigned_handler, string_format_build, 0)

static inline void string_parse_integer_build(const string_text_t *text,
                                              uint8_t *buffer, size_t capacity,
                                              binrwir_execution_exit_t *output_exit) {
    string_parse_number_of(text, 0, 1, buffer, capacity, output_exit);
}
VMBIN_HANDLER_UNARY_TEXT(string_parse_integer_handler, string_parse_integer_build)

static inline void string_parse_unsigned_build(const string_text_t *text,
                                               uint8_t *buffer, size_t capacity,
                                               binrwir_execution_exit_t *output_exit) {
    string_parse_number_of(text, 0, 0, buffer, capacity, output_exit);
}
VMBIN_HANDLER_UNARY_TEXT(string_parse_unsigned_handler, string_parse_unsigned_build)

static inline void string_parse_float_build(const string_text_t *text,
                                            uint8_t *buffer, size_t capacity,
                                            binrwir_execution_exit_t *output_exit) {
    string_parse_number_of(text, 1, 0, buffer, capacity, output_exit);
}
VMBIN_HANDLER_UNARY_TEXT(string_parse_float_handler, string_parse_float_build)

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
