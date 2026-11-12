
#include "vmbin/handlers/scalar_character.h"

#include <string.h>

#include "binrwir/execution_exit.h"
#include "binrwir/binvthread_frame.h"
#include "binrwir/instruction.h"
#include "contract/char_codec.h"
#include "contract/error.h"
#include "contract/status.h"
#include "contract/view.h"
#include "vmbin/handlers/scalar.h"
#include "vmbin/handlers/scalar_special.h"
#include "vmbin/handlers/string_text.h"
#include "vmbin/value_access.h"

static void scalar_character_store_utf8(
    binrwir_binvthread_frame_t *frame,
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
        
        runtimec_scalar_fail(output_exit,
                             RUNTIMEC_SCALAR_DIAG_LENGTH_MISMATCH);
        return;
    }
    if (size != 0U) {
        memcpy(location.write_bytes, bytes, size);
    }
}

void runtimec_scalar_concat(binrwir_binvthread_frame_t *frame,
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

void runtimec_scalar_character_equal(
    binrwir_binvthread_frame_t *frame,
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

void runtimec_scalar_value_to_character(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, uint32_t code_point,
    runtimec_character_encoding_t target_encoding,
    binrwir_execution_exit_t *output_exit) {
    uint8_t encoded[RUNTIMEC_CHAR_CODEC_UTF8_MAX_BYTES];
    size_t encoded_size = 0U;
    uint32_t as_utf32 = code_point;

    if (code_point > UINT32_C(0x10FFFF) ||
        (code_point >= UINT32_C(0xD800) && code_point <= UINT32_C(0xDFFF))) {
        
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

void runtimec_scalar_character_to_value(
    binrwir_binvthread_frame_t *frame,
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

void runtimec_scalar_character_to_number(
    binrwir_binvthread_frame_t *frame,
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
