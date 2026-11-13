#ifndef VMBIN_HANDLERS_HANDLER_CALL_TEXT_H
#define VMBIN_HANDLERS_HANDLER_CALL_TEXT_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "contract/error.h"
#include "contract/status.h"
#include "contract/view.h"
#include "vmbin/handlers/handler_call.h"
#include <pathspace/pathspace_xvalue.h>
#include "vmbin/frame_target_key.h"
#include "vmbin/handlers/string_text.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMBIN_HANDLER_CREATE_BUFFER_CAPACITY 4096U
#define VMBIN_HANDLER_CREATE_KEY_CAPACITY 512U

typedef enum vmbin_handler_call_text_diagnostic {
    VMBIN_HANDLER_CALL_TEXT_DIAG_NONE = 0,
    
    VMBIN_HANDLER_CALL_TEXT_DIAG_MALFORMED = 0x4B01,
    
    VMBIN_HANDLER_CALL_TEXT_DIAG_UNSUPPORTED_ENCODING = 0x4B02
} vmbin_handler_call_text_diagnostic_t;

static inline void vmbin_handler_fail_decode(
    binrwir_execution_exit_t *output_exit, runtimec_status_t status,
    uint32_t source_program_counter) {
    vmbin_handler_fail(
        output_exit, status,
        status == RUNTIMEC_STATUS_UNSUPPORTED
            ? VMBIN_HANDLER_CALL_TEXT_DIAG_UNSUPPORTED_ENCODING
            : VMBIN_HANDLER_CALL_TEXT_DIAG_MALFORMED,
        source_program_counter);
}

#define VMBIN_HANDLER_BINARY_TEXT_SCALAR(handler_name, function, write_type)       \
    static runtimec_status_t handler_name(                                         \
                             binrwir_binvthread_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t left_position_;                                     \
        vmbin_value_location_t right_position_;                                    \
        vmbin_value_location_t write_position_;                                    \
        runtimec_string_view_t left_langtype_;                                     \
        runtimec_string_view_t right_langtype_;                                    \
        string_text_t left_text_;                                                  \
        string_text_t right_text_;                                                 \
        runtimec_error_t vmbin_handler_error_;                                     \
        runtimec_status_t vmbin_handler_status_;                                   \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 2U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &left_position_,         \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 1U, &right_position_,        \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        VMBIN_HANDLER_REQUIRE_WRITABLE(write_position_, output_exit, instruction); \
        VMBIN_HANDLER_REQUIRE(write_position_, write_type, output_exit,            \
                              instruction);                                        \
        runtimec_error_reset(&vmbin_handler_error_);                               \
        vmbin_handler_status_ = vmbin_value_access_langtype(                       \
            frame, &instruction->read_arguments.data[0], &left_langtype_,          \
            &vmbin_handler_error_);                                                \
        if (vmbin_handler_status_ == RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_status_ = vmbin_value_access_langtype(                   \
                frame, &instruction->read_arguments.data[1], &right_langtype_,     \
                &vmbin_handler_error_);                                            \
        }                                                                          \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail(output_exit, vmbin_handler_status_,                 \
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,              \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        vmbin_handler_status_ = string_text_decode(                                \
            runtimec_bytes_view_make(left_position_.read_bytes,                    \
                                     left_position_.size),                         \
            left_langtype_, &left_text_);                                          \
        if (vmbin_handler_status_ == RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_status_ = string_text_decode(                            \
                runtimec_bytes_view_make(right_position_.read_bytes,               \
                                         right_position_.size),                    \
                right_langtype_, &right_text_);                                    \
        }                                                                          \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail_decode(output_exit, vmbin_handler_status_,          \
                                      instruction->source_logical_program_counter);\
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        function(&left_text_, &right_text_,                                        \
                 (write_type *)(void *)write_position_.write_bytes);               \
        return RUNTIMEC_STATUS_SUCCESS;                                            \
    }

#define VMBIN_HANDLER_UNARY_TEXT(handler_name, function)                           \
    static runtimec_status_t handler_name(                                         \
                             binrwir_binvthread_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t read_position_;                                     \
        vmbin_value_location_t write_position_;                                    \
        runtimec_string_view_t read_langtype_;                                     \
        string_text_t read_text_;                                                  \
        runtimec_error_t vmbin_handler_error_;                                     \
        runtimec_status_t vmbin_handler_status_;                                   \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 1U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &read_position_,         \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        runtimec_error_reset(&vmbin_handler_error_);                               \
        vmbin_handler_status_ = vmbin_value_access_langtype(                       \
            frame, &instruction->read_arguments.data[0], &read_langtype_,          \
            &vmbin_handler_error_);                                                \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail(output_exit, vmbin_handler_status_,                 \
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,              \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        vmbin_handler_status_ = string_text_decode(                                \
            runtimec_bytes_view_make(read_position_.read_bytes,                    \
                                     read_position_.size),                         \
            read_langtype_, &read_text_);                                          \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail_decode(output_exit, vmbin_handler_status_,          \
                                      instruction->source_logical_program_counter);\
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        function(&read_text_, write_position_.write_bytes,                         \
                 write_position_.size, output_exit);                               \
          \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }

#define VMBIN_HANDLER_TEXT2_INDEX1(handler_name, function, index_type)             \
    static runtimec_status_t handler_name(                                         \
                             binrwir_binvthread_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t first_position_;                                    \
        vmbin_value_location_t second_position_;                                   \
        vmbin_value_location_t index_position_;                                    \
        vmbin_value_location_t write_position_;                                    \
        runtimec_string_view_t first_langtype_;                                    \
        runtimec_string_view_t second_langtype_;                                   \
        string_text_t first_text_;                                                 \
        string_text_t second_text_;                                                \
        runtimec_error_t vmbin_handler_error_;                                     \
        runtimec_status_t vmbin_handler_status_;                                   \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 3U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &first_position_,        \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 1U, &second_position_,       \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 2U, &index_position_,        \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        VMBIN_HANDLER_REQUIRE(index_position_, index_type, output_exit,            \
                              instruction);                                        \
        runtimec_error_reset(&vmbin_handler_error_);                               \
        vmbin_handler_status_ = vmbin_value_access_langtype(                       \
            frame, &instruction->read_arguments.data[0], &first_langtype_,         \
            &vmbin_handler_error_);                                                \
        if (vmbin_handler_status_ == RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_status_ = vmbin_value_access_langtype(                   \
                frame, &instruction->read_arguments.data[1], &second_langtype_,    \
                &vmbin_handler_error_);                                            \
        }                                                                          \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail(output_exit, vmbin_handler_status_,                 \
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,              \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        vmbin_handler_status_ = string_text_decode(                                \
            runtimec_bytes_view_make(first_position_.read_bytes,                   \
                                     first_position_.size),                        \
            first_langtype_, &first_text_);                                        \
        if (vmbin_handler_status_ == RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_status_ = string_text_decode(                            \
                runtimec_bytes_view_make(second_position_.read_bytes,              \
                                         second_position_.size),                   \
                second_langtype_, &second_text_);                                  \
        }                                                                          \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail_decode(output_exit, vmbin_handler_status_,          \
                                      instruction->source_logical_program_counter);\
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        function(&first_text_, &second_text_,                                      \
                 (const index_type *)(const void *)index_position_.read_bytes,     \
                 write_position_.write_bytes, write_position_.size);               \
                          \
        return RUNTIMEC_STATUS_SUCCESS;                                            \
    }

#define VMBIN_HANDLER_TEXT_INDEX_SCALAR(handler_name, function, index_type,        \
                                        write_type)                                \
    static runtimec_status_t handler_name(                                         \
                             binrwir_binvthread_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t text_position_;                                     \
        vmbin_value_location_t index_position_;                                    \
        vmbin_value_location_t write_position_;                                    \
        runtimec_string_view_t text_langtype_;                                     \
        string_text_t text_;                                                       \
        runtimec_error_t vmbin_handler_error_;                                     \
        runtimec_status_t vmbin_handler_status_;                                   \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 2U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &text_position_,         \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 1U, &index_position_,        \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        VMBIN_HANDLER_REQUIRE_WRITABLE(write_position_, output_exit, instruction); \
        VMBIN_HANDLER_REQUIRE(index_position_, index_type, output_exit,            \
                              instruction);                                        \
        VMBIN_HANDLER_REQUIRE(write_position_, write_type, output_exit,            \
                              instruction);                                        \
        runtimec_error_reset(&vmbin_handler_error_);                               \
        vmbin_handler_status_ = vmbin_value_access_langtype(                       \
            frame, &instruction->read_arguments.data[0], &text_langtype_,          \
            &vmbin_handler_error_);                                                \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail(output_exit, vmbin_handler_status_,                 \
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,              \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        vmbin_handler_status_ = string_text_decode(                                \
            runtimec_bytes_view_make(text_position_.read_bytes,                    \
                                     text_position_.size),                         \
            text_langtype_, &text_);                                               \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail_decode(output_exit, vmbin_handler_status_,          \
                                      instruction->source_logical_program_counter);\
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        function(&text_,                                                          \
                 (const index_type *)(const void *)index_position_.read_bytes,     \
                 (write_type *)(void *)write_position_.write_bytes, output_exit);  \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }

#define VMBIN_HANDLER_UNARY_TEXT_SCALAR(handler_name, function, write_type)        \
    static runtimec_status_t handler_name(                                         \
                             binrwir_binvthread_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t read_position_;                                     \
        vmbin_value_location_t write_position_;                                    \
        runtimec_string_view_t read_langtype_;                                     \
        runtimec_error_t vmbin_handler_error_;                                     \
        runtimec_status_t vmbin_handler_status_;                                   \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 1U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &read_position_,         \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        VMBIN_HANDLER_REQUIRE_WRITABLE(write_position_, output_exit, instruction); \
        VMBIN_HANDLER_REQUIRE(write_position_, write_type, output_exit,            \
                              instruction);                                        \
        runtimec_error_reset(&vmbin_handler_error_);                               \
        vmbin_handler_status_ = vmbin_value_access_langtype(                       \
            frame, &instruction->read_arguments.data[0], &read_langtype_,          \
            &vmbin_handler_error_);                                                \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail(output_exit, vmbin_handler_status_,                 \
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,              \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        function(&read_position_, read_langtype_,                                  \
                 (write_type *)(void *)write_position_.write_bytes, output_exit);  \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }

#define VMBIN_HANDLER_TEXT_INDEX_CREATE(handler_name, function, index_type)        \
    static runtimec_status_t handler_name(                                         \
                             binrwir_binvthread_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t text_position_;                                     \
        vmbin_value_location_t index_position_;                                    \
        runtimec_string_view_t text_langtype_;                                     \
        uint8_t result_bytes_[VMBIN_HANDLER_CREATE_BUFFER_CAPACITY];               \
        size_t result_length_ = 0U;                                                \
        string_text_t text_;                                                       \
        runtimec_error_t vmbin_handler_error_;                                     \
        runtimec_status_t vmbin_handler_status_;                                   \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 2U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &text_position_,         \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 1U, &index_position_,        \
                                  output_exit);                                    \
        VMBIN_HANDLER_REQUIRE(index_position_, index_type, output_exit,            \
                              instruction);                                        \
        runtimec_error_reset(&vmbin_handler_error_);                               \
        vmbin_handler_status_ = vmbin_value_access_langtype(                       \
            frame, &instruction->read_arguments.data[0], &text_langtype_,          \
            &vmbin_handler_error_);                                                \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail(output_exit, vmbin_handler_status_,                 \
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,              \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        vmbin_handler_status_ = string_text_decode(                                \
            runtimec_bytes_view_make(text_position_.read_bytes,                    \
                                     text_position_.size),                         \
            text_langtype_, &text_);                                               \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail_decode(output_exit, vmbin_handler_status_,          \
                                      instruction->source_logical_program_counter);\
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
                        \
        runtimec_string_view_t write_langtype_;                                    \
        vmbin_handler_status_ = vmbin_value_access_langtype(                       \
            frame, &instruction->write_arguments.data[0], &write_langtype_,        \
            &vmbin_handler_error_);                                                \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail(output_exit, vmbin_handler_status_,                 \
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,              \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        function(&text_,                                                          \
                 (const index_type *)(const void *)index_position_.read_bytes,     \
                 write_langtype_, result_bytes_, sizeof(result_bytes_),            \
                 &result_length_, output_exit);                                    \
        return vmbin_handler_write_new_value(frame, instruction, result_bytes_,    \
                                             result_length_, output_exit);         \
    }

#define VMBIN_HANDLER_FORMAT_INTEGER(handler_name, function, is_signed_value)      \
    static runtimec_status_t handler_name(                                         \
                             binrwir_binvthread_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t value_position_;                                    \
        vmbin_value_location_t base_position_;                                     \
        int64_t base_ = 10;                                                        \
        uint8_t result_bytes_[VMBIN_HANDLER_CREATE_BUFFER_CAPACITY];               \
        size_t result_length_ = 0U;                                                \
                                                                                   \
        if (instruction->read_arguments.count < 1U ||                              \
            instruction->read_arguments.count > 2U ||                              \
            instruction->write_arguments.count != 1U) {                            \
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,               \
                               VMBIN_HANDLER_CALL_DIAG_WRONG_ARITY,                \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &value_position_,        \
                                  output_exit);                                    \
        if (instruction->read_arguments.count == 2U) {                             \
            VMBIN_HANDLER_LOCATE_READ(frame, instruction, 1U, &base_position_,     \
                                      output_exit);                                \
            VMBIN_HANDLER_REQUIRE(base_position_, int64_t, output_exit,            \
                                  instruction);                                    \
            memcpy(&base_, base_position_.read_bytes, sizeof(base_));              \
        }                                                                          \
                         \
        runtimec_string_view_t write_langtype_;                                    \
        {                                                                          \
            runtimec_error_t langtype_error_;                                      \
            runtimec_error_reset(&langtype_error_);                                \
            if (vmbin_value_access_langtype(                                       \
                    frame, &instruction->write_arguments.data[0],                  \
                    &write_langtype_, &langtype_error_) !=                             \
                RUNTIMEC_STATUS_SUCCESS) {                                         \
                vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,           \
                                   VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,          \
                                   instruction->source_logical_program_counter);   \
                return RUNTIMEC_STATUS_ERROR;                                      \
            }                                                                      \
        }                                                                          \
        function(&value_position_, base_, (is_signed_value), write_langtype_,      \
                 result_bytes_, sizeof(result_bytes_), &result_length_,            \
                 output_exit);                                                     \
        return vmbin_handler_write_new_value(frame, instruction, result_bytes_,    \
                                             result_length_, output_exit);         \
    }

#define VMBIN_HANDLER_TEXT_INDEX2(handler_name, function, index_type)              \
    static runtimec_status_t handler_name(                                         \
                             binrwir_binvthread_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t text_position_;                                     \
        vmbin_value_location_t bounds_[2];                                         \
        runtimec_string_view_t text_langtype_;                                     \
        uint8_t result_bytes_[VMBIN_HANDLER_CREATE_BUFFER_CAPACITY];               \
        size_t result_length_ = 0U;                                                \
        string_text_t text_;                                                       \
        runtimec_error_t vmbin_handler_error_;                                     \
        runtimec_status_t vmbin_handler_status_;                                   \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 3U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &text_position_,         \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 1U, &bounds_[0],             \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 2U, &bounds_[1],             \
                                  output_exit);                                    \
        runtimec_error_reset(&vmbin_handler_error_);                               \
        vmbin_handler_status_ = vmbin_value_access_langtype(                       \
            frame, &instruction->read_arguments.data[0], &text_langtype_,          \
            &vmbin_handler_error_);                                                \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail(output_exit, vmbin_handler_status_,                 \
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,              \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        vmbin_handler_status_ = string_text_decode(                                \
            runtimec_bytes_view_make(text_position_.read_bytes,                    \
                                     text_position_.size),                         \
            text_langtype_, &text_);                                               \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail_decode(output_exit, vmbin_handler_status_,          \
                                      instruction->source_logical_program_counter);\
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
                        \
        runtimec_string_view_t write_langtype_;                                    \
        vmbin_handler_status_ = vmbin_value_access_langtype(                       \
            frame, &instruction->write_arguments.data[0], &write_langtype_,        \
            &vmbin_handler_error_);                                                \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail(output_exit, vmbin_handler_status_,                 \
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,              \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        function(&text_, bounds_, write_langtype_, result_bytes_,                  \
                 sizeof(result_bytes_), &result_length_, output_exit);             \
        return vmbin_handler_write_new_value(frame, instruction, result_bytes_,    \
                                             result_length_, output_exit);         \
    }

#define VMBIN_HANDLER_SET_BY_ENCODING(handler_name, patch_function,                \
                                      rebuild_function)                            \
    static runtimec_status_t handler_name(                                         \
                             binrwir_binvthread_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t text_position_;                                     \
        vmbin_value_location_t write_position_;                                    \
        vmbin_value_location_t position_location_;                                 \
        vmbin_value_location_t replacement_position_;                              \
        runtimec_string_view_t replacement_langtype_;                              \
        runtimec_string_view_t text_langtype_;                                     \
        string_text_t text_;                                                       \
        string_text_t replacement_;                                                \
        uint8_t rebuilt_bytes_[VMBIN_HANDLER_CREATE_BUFFER_CAPACITY];               \
        size_t rebuilt_length_ = 0U;                                               \
        int64_t position_ = 0;                                                     \
        runtimec_error_t vmbin_handler_error_;                                     \
        runtimec_status_t vmbin_handler_status_;                                   \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 3U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &text_position_,         \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 1U, &position_location_,     \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 2U,                         \
                                  &replacement_position_, output_exit);            \
        VMBIN_HANDLER_REQUIRE(position_location_, int64_t, output_exit,            \
                              instruction);                                        \
        memcpy(&position_, position_location_.read_bytes, sizeof(position_));      \
        runtimec_error_reset(&vmbin_handler_error_);                               \
        vmbin_handler_status_ = vmbin_value_access_langtype(                       \
            frame, &instruction->read_arguments.data[0], &text_langtype_,          \
            &vmbin_handler_error_);                                                \
        if (vmbin_handler_status_ == RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_status_ = vmbin_value_access_langtype(                   \
                frame, &instruction->read_arguments.data[2],                       \
                &replacement_langtype_, &vmbin_handler_error_);                   \
        }                                                                          \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail(output_exit, vmbin_handler_status_,                 \
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,              \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        vmbin_handler_status_ = string_text_decode(                                \
            runtimec_bytes_view_make(text_position_.read_bytes,                    \
                                     text_position_.size),                         \
            text_langtype_, &text_);                                               \
        if (vmbin_handler_status_ == RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_status_ = string_text_decode(                            \
                runtimec_bytes_view_make(replacement_position_.read_bytes,         \
                                         replacement_position_.size),              \
                replacement_langtype_, &replacement_);                             \
        }                                                                          \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail_decode(output_exit, vmbin_handler_status_,          \
                                      instruction->source_logical_program_counter);\
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
                        \
        runtimec_string_view_t write_langtype_;                                    \
        vmbin_handler_status_ = vmbin_value_access_langtype(                       \
            frame, &instruction->write_arguments.data[0], &write_langtype_,        \
            &vmbin_handler_error_);                                                \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail(output_exit, vmbin_handler_status_,                 \
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,              \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
                               \
        if (string_text_encoding_is_fixed_width(text_langtype_)) {                 \
            VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,   \
                                       output_exit);                               \
            patch_function(&text_, &write_position_, &position_, &replacement_,    \
                           output_exit);                                           \
            return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                \
                       ? output_exit->status                                       \
                       : RUNTIMEC_STATUS_SUCCESS;                                  \
        }                                                                          \
        rebuild_function(&text_, &position_, &replacement_, write_langtype_,      \
                         rebuilt_bytes_, sizeof(rebuilt_bytes_),                   \
                         &rebuilt_length_, output_exit);                           \
        if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) {                   \
            return output_exit->status;                                            \
        }                                                                          \
        return vmbin_handler_write_new_value(frame, instruction, rebuilt_bytes_,   \
                                             rebuilt_length_, output_exit);        \
    }

#ifdef __cplusplus
}
#endif

#endif
