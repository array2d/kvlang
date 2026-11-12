#ifndef VMBIN_HANDLERS_SCALAR_CHARACTER_H
#define VMBIN_HANDLERS_SCALAR_CHARACTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "binrwir/execution_exit.h"
#include "binrwir/binvthread_frame.h"
#include "binrwir/instruction.h"
#include "vmbin/handlers/scalar.h"
#include "vmbin/handlers/string_text.h"
#include "vmbin/value_access.h"

#define RUNTIMEC_SCALAR_READ_SIZED(frame, instruction, read_type, output_name,  \
                                   output_exit)                                \
    vmbin_value_location_t output_name##_position_;                            \
    const read_type *output_name = NULL;                                       \
    do {                                                                       \
        runtimec_error_t output_name##_error_;                                 \
        runtimec_error_reset(&output_name##_error_);                           \
        if (vmbin_value_access_locate_body(                                    \
                (frame), &(instruction)->read_arguments.data[0],               \
                &output_name##_position_, &output_name##_error_) !=            \
            RUNTIMEC_STATUS_SUCCESS) {                                         \
            runtimec_scalar_fail((output_exit),                                \
                                 RUNTIMEC_SCALAR_DIAG_LOCATE_FAILED);          \
            return RUNTIMEC_STATUS_ERROR;                                      \
        }                                                                      \
        if (output_name##_position_.size != sizeof(read_type)) {               \
            runtimec_scalar_fail((output_exit),                                \
                                 RUNTIMEC_SCALAR_DIAG_LENGTH_MISMATCH);        \
            return RUNTIMEC_STATUS_ERROR;                                      \
        }                                                                      \
        output_name = (const read_type *)(const void *)                        \
            output_name##_position_.read_bytes;                                \
    } while (0)

#define RUNTIMEC_SCALAR_THUNK_END(output_exit)                                 \
    return (output_exit)->kind == BINRWIR_EXECUTION_EXIT_ERROR                 \
               ? (output_exit)->status                                         \
               : RUNTIMEC_STATUS_SUCCESS

#define VMBIN_HANDLER_UNARY_TO_CHARACTER(handler_name, function, source_type,   \
                                         to_code_point, encoding)              \
    static runtimec_status_t handler_name(                                     \
        binrwir_binvthread_frame_t *frame,                                      \
        const binrwir_instruction_view_t *instruction,                         \
        binrwir_execution_exit_t *output_exit) {                               \
        RUNTIMEC_SCALAR_READ_SIZED(frame, instruction, source_type, source_,   \
                                   output_exit);                               \
        {                                                                      \
            const uint32_t source_value_ = (uint32_t)(to_code_point);          \
            function(frame, instruction, source_value_, encoding,              \
                     output_exit);                                             \
        }                                                                      \
        RUNTIMEC_SCALAR_THUNK_END(output_exit);                                \
    }

#define VMBIN_HANDLER_CHARACTER_TO_NUMBER(handler_name, function, decoder,      \
                                          target_width, target_signed,         \
                                          target_is_float)                     \
    static runtimec_status_t handler_name(                                     \
        binrwir_binvthread_frame_t *frame,                                      \
        const binrwir_instruction_view_t *instruction,                         \
        binrwir_execution_exit_t *output_exit) {                               \
        function(frame, instruction, decoder, target_width, target_signed,     \
                 target_is_float, output_exit);                                \
        RUNTIMEC_SCALAR_THUNK_END(output_exit);                                \
    }

#define VMBIN_HANDLER_CHARACTER_TO_CHARACTER(handler_name, function, decoder,   \
                                             encoding)                         \
    static runtimec_status_t handler_name(                                     \
        binrwir_binvthread_frame_t *frame,                                      \
        const binrwir_instruction_view_t *instruction,                         \
        binrwir_execution_exit_t *output_exit) {                               \
        function(frame, instruction, decoder, encoding, output_exit);          \
        RUNTIMEC_SCALAR_THUNK_END(output_exit);                                \
    }

#define VMBIN_HANDLER_CHARACTER_EQUAL(handler_name, function, left_decoder,     \
                                      right_decoder, invert)                   \
    static runtimec_status_t handler_name(                                     \
        binrwir_binvthread_frame_t *frame,                                      \
        const binrwir_instruction_view_t *instruction,                         \
        binrwir_execution_exit_t *output_exit) {                               \
        function(frame, instruction, left_decoder, right_decoder, invert,      \
                 output_exit);                                                 \
        RUNTIMEC_SCALAR_THUNK_END(output_exit);                                \
    }

#define VMBIN_HANDLER_CHARACTER_CONCAT(handler_name, function, left_decoder,    \
                                       right_decoder)                          \
    static runtimec_status_t handler_name(                                     \
        binrwir_binvthread_frame_t *frame,                                      \
        const binrwir_instruction_view_t *instruction,                         \
        binrwir_execution_exit_t *output_exit) {                               \
        function(frame, instruction, left_decoder, right_decoder,              \
                 output_exit);                                                 \
        RUNTIMEC_SCALAR_THUNK_END(output_exit);                                \
    }

#ifdef __cplusplus
extern "C" {
#endif

typedef enum runtimec_character_encoding {
    RUNTIMEC_CHARACTER_ENCODING_UTF8 = 0,
    RUNTIMEC_CHARACTER_ENCODING_UTF32 = 1,
    RUNTIMEC_CHARACTER_ENCODING_ASCII = 2
} runtimec_character_encoding_t;

typedef bool (*string_text_decode_function_t)(const uint8_t *bytes, size_t size,
                                              string_text_t *output);

bool runtimec_scalar_character_code_point(const uint8_t *bytes, size_t size,
                                          string_text_decode_function_t decode,
                                          uint32_t *output_code_point);

void runtimec_scalar_value_to_character(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, uint32_t code_point,
    runtimec_character_encoding_t target_encoding,
    binrwir_execution_exit_t *output_exit);

void runtimec_scalar_character_to_value(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    string_text_decode_function_t decode_source,
    runtimec_character_encoding_t target_encoding,
    binrwir_execution_exit_t *output_exit);

void runtimec_scalar_character_to_number(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    string_text_decode_function_t decode_source, uint32_t target_width,
    bool target_signed, bool target_is_float,
    binrwir_execution_exit_t *output_exit);

void runtimec_scalar_concat(binrwir_binvthread_frame_t *frame,
                            const binrwir_instruction_view_t *instruction,
                            string_text_decode_function_t decode_left,
                            string_text_decode_function_t decode_right,
                            binrwir_execution_exit_t *output_exit);

void runtimec_scalar_character_equal(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    string_text_decode_function_t decode_left,
    string_text_decode_function_t decode_right, bool invert,
    binrwir_execution_exit_t *output_exit);

#ifdef __cplusplus
}
#endif

#endif
