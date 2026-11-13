#ifndef VMBIN_HANDLERS_HANDLER_CALL_H
#define VMBIN_HANDLERS_HANDLER_CALL_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "binrwir/execution_exit.h"

#define VMBIN_HANDLER_MAX_INDEX_COUNT 8

#define RUNTIMEC_CONTAINER_DIMENSION_UNBOUNDED INT64_C(-1)
#include <pathspace/pathspace_xvalue.h>

#include "contract/diagnostic_band.h"
#include "contract/pathspace_interface.h"
#include "binrwir/binvthread_frame.h"
#include "binrwir/instruction.h"
#include "contract/error.h"
#include "contract/status.h"
#include "contract/view.h"
#include <pathlang/xvalue.h>
#include <pathspace/pathspace_xvalue.h>
#include "vmbin/frame_target_key.h"
#include "vmbin/value_access.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMBIN_HANDLER_LANGTYPE_CAPACITY 256U
#define VMBIN_HANDLER_KEY_CAPACITY 512U

typedef enum vmbin_handler_call_diagnostic {
    VMBIN_HANDLER_CALL_DIAG_NONE = 0,
    
    VMBIN_HANDLER_CALL_DIAG_WRONG_ARITY = 0x4A01,
    
    VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED = 0x4A02,
    
    VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH = 0x4A03,
    
    VMBIN_HANDLER_CALL_DIAG_LITERAL_IS_READ_ONLY = 0x4A04,
    
    VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED = 0x4A05,
    
    VMBIN_HANDLER_CALL_DIAG_INSUFFICIENT_CAPACITY = 0x4A06,
    
    VMBIN_HANDLER_CALL_DIAG_UNSUPPORTED_LANGTYPE = 0x4A07,
    
    VMBIN_HANDLER_CALL_DIAG_MALFORMED_BODY = 0x4A08,
    
    VMBIN_HANDLER_CALL_DIAG_NON_FINITE = 0x4A09,
    
    VMBIN_HANDLER_CALL_DIAG_KEY_TOO_LONG = 0x4A0A,
    
    VMBIN_HANDLER_CALL_DIAG_STRING_TOO_LONG = 0x4A0B
} vmbin_handler_call_diagnostic_t;

_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(VMBIN_HANDLER_CALL_DIAG_LITERAL_IS_READ_ONLY, RUNTIMEC_DIAGNOSTIC_BAND_HANDLER_CALL),
               "VMBIN_HANDLER_CALL_DIAG_LITERAL_IS_READ_ONLY must stay inside its diagnostic band");
_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(VMBIN_HANDLER_CALL_DIAG_INSUFFICIENT_CAPACITY, RUNTIMEC_DIAGNOSTIC_BAND_HANDLER_CALL),
               "VMBIN_HANDLER_CALL_DIAG_INSUFFICIENT_CAPACITY must stay inside its diagnostic band");
_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(VMBIN_HANDLER_CALL_DIAG_NON_FINITE, RUNTIMEC_DIAGNOSTIC_BAND_HANDLER_CALL),
               "VMBIN_HANDLER_CALL_DIAG_NON_FINITE must stay inside its diagnostic band");
_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(VMBIN_HANDLER_CALL_DIAG_KEY_TOO_LONG, RUNTIMEC_DIAGNOSTIC_BAND_HANDLER_CALL),
               "VMBIN_HANDLER_CALL_DIAG_KEY_TOO_LONG must stay inside its diagnostic band");
_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(VMBIN_HANDLER_CALL_DIAG_STRING_TOO_LONG, RUNTIMEC_DIAGNOSTIC_BAND_HANDLER_CALL),
               "VMBIN_HANDLER_CALL_DIAG_STRING_TOO_LONG must stay inside its diagnostic band");

#define VMBIN_HANDLER_KEY_TEXT_MAXIMUM UINT32_C(1023)

static inline void vmbin_handler_fail(binrwir_execution_exit_t *output_exit,
                                      runtimec_status_t status,
                                      uint32_t detail_code,
                                      uint32_t source_program_counter) {
    *output_exit = (binrwir_execution_exit_t){0};
    output_exit->kind = BINRWIR_EXECUTION_EXIT_ERROR;
    output_exit->status = status;
    output_exit->detail_code = detail_code;
    output_exit->logical_program_counter = source_program_counter;
    output_exit->resume_logical_program_counter = source_program_counter;
}

#define VMBIN_HANDLER_LOCATE_READ(frame, instruction, ordinal, location,           \
                                  output_exit)                                     \
    do {                                                                           \
        runtimec_error_t vmbin_handler_error_;                                     \
        runtimec_status_t vmbin_handler_status_;                                   \
                                                                                   \
        runtimec_error_reset(&vmbin_handler_error_);                               \
        vmbin_handler_status_ = vmbin_value_access_locate_body(                    \
            (frame), &(instruction)->read_arguments.data[(ordinal)], (location),   \
            &vmbin_handler_error_);                                                \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail(output_exit, vmbin_handler_status_,                 \
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,              \
                               (instruction)->source_logical_program_counter);     \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
    } while (0)

#define VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, ordinal, location,          \
                                   output_exit)                                    \
    do {                                                                           \
        runtimec_error_t vmbin_handler_error_;                                     \
        runtimec_status_t vmbin_handler_status_;                                   \
                                                                                   \
        runtimec_error_reset(&vmbin_handler_error_);                               \
                                \
        vmbin_handler_status_ =                                                   \
            vmbin_value_access_locate_write(                                       \
                (frame), &(instruction)->write_arguments.data[(ordinal)],          \
                (location), &vmbin_handler_error_);                                \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail(output_exit, vmbin_handler_status_,                 \
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,              \
                               (instruction)->source_logical_program_counter);     \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
    } while (0)

#define VMBIN_HANDLER_REQUIRE_WRITABLE(position, output_exit, instruction)         \
    do {                                                                           \
        if ((position).write_bytes == NULL) {                                      \
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,               \
                               VMBIN_HANDLER_CALL_DIAG_LITERAL_IS_READ_ONLY,       \
                               (instruction)->source_logical_program_counter);     \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
    } while (0)

static inline runtimec_status_t vmbin_handler_write_new_value(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, const uint8_t *bytes,
    size_t length, binrwir_execution_exit_t *output_exit) {
    char langtype_buffer[VMBIN_HANDLER_LANGTYPE_CAPACITY];
    char key_buffer[VMBIN_HANDLER_KEY_CAPACITY];
    runtimec_string_view_t declared;
    runtimec_error_t error;
    runtimec_status_t status;
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    char message[256];
    runtimec_key_view_t target_key;

    runtimec_error_reset(&error);
    status = vmbin_value_access_langtype(
        frame, &instruction->write_arguments.data[0], &declared, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS ||
        declared.size + 1U > sizeof(langtype_buffer)) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           instruction->source_logical_program_counter);
        return RUNTIMEC_STATUS_ERROR;
    }
    memcpy(langtype_buffer, declared.data, declared.size);
    langtype_buffer[declared.size] = '\0';
    if (pathlangXvalueHeadNewSlack(bytes, (uint64_t)length,
                                  langtype_buffer, &wire, &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                           VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED,
                           instruction->source_logical_program_counter);
        return RUNTIMEC_STATUS_ERROR;
    }
    
    status = vmbin_value_access_argument_key(
        frame, &instruction->write_arguments.data[0], key_buffer,
        sizeof(key_buffer), &error);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        message[0] = '\0';
        if (!runtimec_pathspace_write_wire(frame->pathspace_handle, key_buffer, wire,
                                         (uint32_t)wire_size, message,
                                         sizeof(message))) {
            status = RUNTIMEC_STATUS_ERROR;
        }
    }
    free(wire);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        vmbin_handler_fail(output_exit, status,
                           VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED,
                           instruction->source_logical_program_counter);
        return RUNTIMEC_STATUS_ERROR;
    }
    
    target_key = runtimec_key_view_make(
        key_buffer, strlen(key_buffer));
    status = vmbin_value_access_bind_write_target(
        frame, &instruction->write_arguments.data[0], target_key, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        vmbin_handler_fail(output_exit, status,
                           VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED,
                           instruction->source_logical_program_counter);
        return RUNTIMEC_STATUS_ERROR;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

#define VMBIN_HANDLER_AS(position, type, output_exit, instruction)                 \
    (((position).size == sizeof(type))                                             \
         ? (type *)(void *)(position).write_bytes                                  \
         : (vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,               \
                               VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,            \
                               (instruction)->source_logical_program_counter),     \
            (type *)NULL))

#define VMBIN_HANDLER_AS_READ(position, type, output_exit, instruction)            \
    (((position).size == sizeof(type))                                             \
         ? (const type *)(const void *)(position).read_bytes                       \
         : (vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,               \
                               VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,            \
                               (instruction)->source_logical_program_counter),     \
            (const type *)NULL))

#define VMBIN_HANDLER_REQUIRE(position, type, output_exit, instruction)            \
    do {                                                                           \
        if ((position).size != sizeof(type)) {                                     \
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,               \
                               VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,            \
                               (instruction)->source_logical_program_counter);     \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
    } while (0)

#define VMBIN_HANDLER_REQUIRE_ARITY(instruction, reads, writes, output_exit)       \
    do {                                                                           \
        if ((instruction)->read_arguments.count != (reads) ||                      \
            (instruction)->write_arguments.count != (writes)) {                    \
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,               \
                               VMBIN_HANDLER_CALL_DIAG_WRONG_ARITY,                \
                               (instruction)->source_logical_program_counter);     \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
    } while (0)

#define VMBIN_HANDLER_REQUIRE_ARITY_FAIL_(output_exit, instruction)                \
    do {                                                                           \
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,                   \
                           VMBIN_HANDLER_CALL_DIAG_WRONG_ARITY,                    \
                           (instruction)->source_logical_program_counter);         \
        return RUNTIMEC_STATUS_ERROR;                                              \
    } while (0)

#define VMBIN_HANDLER_UNARY_TYPED(handler_name, function, read_type, write_type)   \
    static runtimec_status_t handler_name(                                      \
                             binrwir_binvthread_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t read_position_;                                     \
        vmbin_value_location_t write_position_;                                    \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 1U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &read_position_,         \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        VMBIN_HANDLER_REQUIRE(read_position_, read_type, output_exit, instruction);\
        VMBIN_HANDLER_REQUIRE_WRITABLE(write_position_, output_exit, instruction); \
        VMBIN_HANDLER_REQUIRE(write_position_, write_type, output_exit,            \
                              instruction);                                        \
        function((const read_type *)(const void *)read_position_.read_bytes,       \
                 (write_type *)(void *)write_position_.write_bytes);               \
        return RUNTIMEC_STATUS_SUCCESS;                                            \
    }

#define VMBIN_HANDLER_BINARY_TYPED(handler_name, function, read_type_first,        \
                                   read_type_second, write_type)                   \
    static runtimec_status_t handler_name(                                      \
                             binrwir_binvthread_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t first_position_;                                    \
        vmbin_value_location_t second_position_;                                   \
        vmbin_value_location_t write_position_;                                    \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 2U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &first_position_,        \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 1U, &second_position_,       \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        VMBIN_HANDLER_REQUIRE(first_position_, read_type_first, output_exit,       \
                              instruction);                                        \
        VMBIN_HANDLER_REQUIRE(second_position_, read_type_second, output_exit,     \
                              instruction);                                        \
        VMBIN_HANDLER_REQUIRE_WRITABLE(write_position_, output_exit, instruction); \
        VMBIN_HANDLER_REQUIRE(write_position_, write_type, output_exit,            \
                              instruction);                                        \
        function((const read_type_first *)(const void *)first_position_.read_bytes,\
                 (const read_type_second *)(const void *)second_position_.read_bytes,\
                 (write_type *)(void *)write_position_.write_bytes);               \
        return RUNTIMEC_STATUS_SUCCESS;                                            \
    }

#define VMBIN_HANDLER_TERNARY_TYPED(handler_name, function, read_type_first,       \
                                   read_type_second, read_type_third, write_type)  \
    static runtimec_status_t handler_name(                                      \
                             binrwir_binvthread_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t first_position_;                                    \
        vmbin_value_location_t second_position_;                                   \
        vmbin_value_location_t third_position_;                                    \
        vmbin_value_location_t write_position_;                                    \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 3U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &first_position_,        \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 1U, &second_position_,       \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 2U, &third_position_,        \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        VMBIN_HANDLER_REQUIRE(first_position_, read_type_first, output_exit,       \
                              instruction);                                        \
        VMBIN_HANDLER_REQUIRE(second_position_, read_type_second, output_exit,     \
                              instruction);                                        \
        VMBIN_HANDLER_REQUIRE(third_position_, read_type_third, output_exit,       \
                              instruction);                                        \
        VMBIN_HANDLER_REQUIRE_WRITABLE(write_position_, output_exit, instruction); \
        VMBIN_HANDLER_REQUIRE(write_position_, write_type, output_exit,            \
                              instruction);                                        \
        function((const read_type_first *)(const void *)first_position_.read_bytes,\
                 (const read_type_second *)(const void *)second_position_.read_bytes,\
                 (const read_type_third *)(const void *)third_position_.read_bytes,\
                 (write_type *)(void *)write_position_.write_bytes);               \
        return RUNTIMEC_STATUS_SUCCESS;                                            \
    }

#define VMBIN_HANDLER_UNARY_LOCATION(handler_name, function)                       \
    static runtimec_status_t handler_name(                                      \
                             binrwir_binvthread_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t read_position_;                                     \
        vmbin_value_location_t write_position_;                                    \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 1U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &read_position_,         \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        function(frame, instruction, &read_position_, &write_position_,            \
                 output_exit);                                                     \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }

#define VMBIN_HANDLER_BINARY_LOCATION(handler_name, function)                      \
    static runtimec_status_t handler_name(                                      \
                             binrwir_binvthread_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t first_position_;                                    \
        vmbin_value_location_t second_position_;                                   \
        vmbin_value_location_t write_position_;                                    \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 2U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &first_position_,        \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 1U, &second_position_,       \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        function(frame, instruction, &first_position_, &second_position_,          \
                 &write_position_, output_exit);                                   \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }

#define VMBIN_HANDLER_TERNARY_LOCATION(handler_name, function)                     \
    static runtimec_status_t handler_name(                                      \
                             binrwir_binvthread_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t first_position_;                                    \
        vmbin_value_location_t second_position_;                                   \
        vmbin_value_location_t third_position_;                                    \
        vmbin_value_location_t write_position_;                                    \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 3U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &first_position_,        \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 1U, &second_position_,       \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 2U, &third_position_,        \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        function(frame, instruction, &first_position_, &second_position_,          \
                 &third_position_, &write_position_, output_exit);                 \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }

#define VMBIN_HANDLER_UNARY_ARRAY(handler_name, function, write_type)              \
    static runtimec_status_t handler_name(                                         \
        binrwir_binvthread_frame_t *frame,                                          \
        const binrwir_instruction_view_t *instruction,                             \
        binrwir_execution_exit_t *output_exit) {                                   \
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
        function(read_position_.read_bytes, read_position_.size, read_langtype_,   \
                 (write_type *)(void *)write_position_.write_bytes, output_exit);  \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }

static inline runtimec_status_t vmbin_handler_prepare_indices(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, size_t index_count,
    int64_t *indices, vmbin_value_location_t *output_array,
    runtimec_string_view_t *output_array_langtype,
    binrwir_execution_exit_t *output_exit) {
    vmbin_value_location_t index_position;
    runtimec_error_t error;
    runtimec_status_t status;

    if (index_count == 0U ||
        index_count > VMBIN_HANDLER_MAX_INDEX_COUNT) {
        VMBIN_HANDLER_REQUIRE_ARITY_FAIL_(output_exit, instruction);
    }
    VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, output_array, output_exit);
    for (size_t ordinal = 0U; ordinal < index_count; ++ordinal) {
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, ordinal + 1U,
                                  &index_position, output_exit);
        VMBIN_HANDLER_REQUIRE(index_position, int64_t, output_exit, instruction);
        indices[ordinal] =
            *(const int64_t *)(const void *)index_position.read_bytes;
    }
    runtimec_error_reset(&error);
    status = vmbin_value_access_langtype(
        frame, &instruction->read_arguments.data[0], output_array_langtype,
        &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        vmbin_handler_fail(output_exit, status,
                           VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,
                           instruction->source_logical_program_counter);
        return RUNTIMEC_STATUS_ERROR;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

#define VMBIN_HANDLER_ARRAY_INDEX(handler_name, function, index_type)              \
    static runtimec_status_t handler_name(                                         \
        binrwir_binvthread_frame_t *frame,                                          \
        const binrwir_instruction_view_t *instruction,                             \
        binrwir_execution_exit_t *output_exit) {                                   \
        vmbin_value_location_t array_position_;                                    \
        vmbin_value_location_t write_position_;                                    \
        runtimec_string_view_t array_langtype_;                                    \
        int64_t indices_[VMBIN_HANDLER_MAX_INDEX_COUNT];                       \
        const size_t index_count_ = instruction->read_arguments.count - 1U;        \
                                                                                   \
        if (vmbin_handler_prepare_indices(frame, instruction, index_count_,        \
                                          indices_, &array_position_,              \
                                          &array_langtype_, output_exit) !=        \
            RUNTIMEC_STATUS_SUCCESS) {                                             \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        function(indices_, index_count_, write_position_.write_bytes,              \
                 write_position_.size, array_position_.read_bytes,                 \
                 array_position_.size, array_langtype_, output_exit);              \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }

#define VMBIN_HANDLER_ARRAY_INDEX_WRITE(handler_name, function, index_type)        \
    static runtimec_status_t handler_name(                                         \
        binrwir_binvthread_frame_t *frame,                                          \
        const binrwir_instruction_view_t *instruction,                             \
        binrwir_execution_exit_t *output_exit) {                                   \
        vmbin_value_location_t array_position_;                                    \
        vmbin_value_location_t value_position_;                                    \
        runtimec_string_view_t array_langtype_;                                    \
        int64_t indices_[VMBIN_HANDLER_MAX_INDEX_COUNT];                       \
        const size_t index_count_ = instruction->read_arguments.count - 2U;        \
                                                                                   \
        if (vmbin_handler_prepare_indices(frame, instruction, index_count_,        \
                                          indices_, &array_position_,              \
                                          &array_langtype_, output_exit) !=        \
            RUNTIMEC_STATUS_SUCCESS) {                                             \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, index_count_ + 1U,           \
                                  &value_position_, output_exit);                  \
        VMBIN_HANDLER_REQUIRE_WRITABLE(array_position_, output_exit, instruction); \
        function(indices_, index_count_, value_position_.read_bytes,               \
                 value_position_.size, array_position_.write_bytes,                \
                 array_position_.size, array_langtype_, output_exit);              \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }

#define VMBIN_HANDLER_CREATE_NONE(handler_name, function)                          \
    static runtimec_status_t handler_name(                                         \
        binrwir_binvthread_frame_t *frame,                                          \
        const binrwir_instruction_view_t *instruction,                             \
        binrwir_execution_exit_t *output_exit) {                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 0U, 1U, output_exit);             \
        function(frame, instruction, NULL, 0U, output_exit);                       \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }

#define VMBIN_HANDLER_CREATE_ONE(handler_name, function)                           \
    static runtimec_status_t handler_name(                                         \
        binrwir_binvthread_frame_t *frame,                                          \
        const binrwir_instruction_view_t *instruction,                             \
        binrwir_execution_exit_t *output_exit) {                                   \
        vmbin_value_location_t read_position_;                                     \
        runtimec_string_view_t read_langtype_;                                     \
        runtimec_error_t vmbin_handler_error_;                                     \
        runtimec_status_t vmbin_handler_status_;                                   \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 1U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &read_position_,         \
                                  output_exit);                                    \
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
        function(frame, instruction, &read_position_, &read_langtype_,             \
                 output_exit);                                                     \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }

#define VMBIN_HANDLER_CREATE_TWO(handler_name, function)                           \
    static runtimec_status_t handler_name(                                         \
        binrwir_binvthread_frame_t *frame,                                          \
        const binrwir_instruction_view_t *instruction,                             \
        binrwir_execution_exit_t *output_exit) {                                   \
        vmbin_value_location_t first_position_;                                    \
        vmbin_value_location_t second_position_;                                   \
        runtimec_string_view_t first_langtype_;                                    \
        runtimec_error_t vmbin_handler_error_;                                     \
        runtimec_status_t vmbin_handler_status_;                                   \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 2U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &first_position_,        \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 1U, &second_position_,       \
                                  output_exit);                                    \
        runtimec_error_reset(&vmbin_handler_error_);                               \
        vmbin_handler_status_ = vmbin_value_access_langtype(                       \
            frame, &instruction->read_arguments.data[0], &first_langtype_,         \
            &vmbin_handler_error_);                                                \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail(output_exit, vmbin_handler_status_,                 \
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,              \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        function(frame, instruction, &first_position_, &first_langtype_,           \
                 &second_position_, output_exit);                                  \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }

#define VMBIN_HANDLER_CREATE_VARIADIC(handler_name, function)                      \
    static runtimec_status_t handler_name(                                         \
        binrwir_binvthread_frame_t *frame,                                          \
        const binrwir_instruction_view_t *instruction,                             \
        binrwir_execution_exit_t *output_exit) {                                   \
        runtimec_string_view_t result_langtype_;                                   \
        runtimec_error_t vmbin_handler_error_;                                     \
        runtimec_status_t vmbin_handler_status_;                                   \
                                                                                   \
        if (instruction->write_arguments.count != 1U) {                            \
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,               \
                               VMBIN_HANDLER_CALL_DIAG_WRONG_ARITY,                \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
                  \
        runtimec_error_reset(&vmbin_handler_error_);                               \
        vmbin_handler_status_ = vmbin_value_access_langtype(                       \
            frame, &instruction->write_arguments.data[0], &result_langtype_,       \
            &vmbin_handler_error_);                                                \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail(output_exit, vmbin_handler_status_,                 \
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,              \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        function(frame, instruction, result_langtype_, output_exit);               \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }

#define VMBIN_HANDLER_CREATE_TERNARY(handler_name, function)                       \
    static runtimec_status_t handler_name(                                         \
        binrwir_binvthread_frame_t *frame,                                          \
        const binrwir_instruction_view_t *instruction,                             \
        binrwir_execution_exit_t *output_exit) {                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 3U, 1U, output_exit);             \
        function(frame, instruction, output_exit);                                 \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }

#define VMBIN_HANDLER_UNARY_TYPED_EXIT(handler_name, function, read_type,          \
                                       write_type)                                 \
    static runtimec_status_t handler_name(                                         \
                             binrwir_binvthread_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t read_position_;                                     \
        vmbin_value_location_t write_position_;                                    \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 1U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &read_position_,         \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        VMBIN_HANDLER_REQUIRE(read_position_, read_type, output_exit, instruction);\
        VMBIN_HANDLER_REQUIRE_WRITABLE(write_position_, output_exit, instruction); \
        VMBIN_HANDLER_REQUIRE(write_position_, write_type, output_exit,            \
                              instruction);                                        \
                                   \
        output_exit->logical_program_counter =                                     \
            instruction->source_logical_program_counter;                           \
        function((const read_type *)(const void *)read_position_.read_bytes,       \
                 (write_type *)(void *)write_position_.write_bytes, output_exit);  \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }

#define VMBIN_HANDLER_BINARY_TYPED_EXIT(handler_name, function, read_type_first,   \
                                        read_type_second, write_type)              \
    static runtimec_status_t handler_name(                                         \
                             binrwir_binvthread_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t first_position_;                                    \
        vmbin_value_location_t second_position_;                                   \
        vmbin_value_location_t write_position_;                                    \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 2U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &first_position_,        \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 1U, &second_position_,       \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        VMBIN_HANDLER_REQUIRE(first_position_, read_type_first, output_exit,       \
                              instruction);                                        \
        VMBIN_HANDLER_REQUIRE(second_position_, read_type_second, output_exit,     \
                              instruction);                                        \
        VMBIN_HANDLER_REQUIRE_WRITABLE(write_position_, output_exit, instruction); \
        VMBIN_HANDLER_REQUIRE(write_position_, write_type, output_exit,            \
                              instruction);                                        \
        output_exit->logical_program_counter =                                     \
            instruction->source_logical_program_counter;                           \
        function((const read_type_first *)(const void *)first_position_.read_bytes,\
                 (const read_type_second *)(const void *)second_position_.read_bytes,\
                 (write_type *)(void *)write_position_.write_bytes, output_exit);  \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }

#define VMBIN_HANDLER_TERNARY_TYPED_EXIT(handler_name, function, read_type_first,  \
                                         read_type_second, read_type_third,        \
                                         write_type)                               \
    static runtimec_status_t handler_name(                                         \
                             binrwir_binvthread_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t first_position_;                                    \
        vmbin_value_location_t second_position_;                                   \
        vmbin_value_location_t third_position_;                                    \
        vmbin_value_location_t write_position_;                                    \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 3U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &first_position_,        \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 1U, &second_position_,       \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 2U, &third_position_,        \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        VMBIN_HANDLER_REQUIRE(first_position_, read_type_first, output_exit,       \
                              instruction);                                        \
        VMBIN_HANDLER_REQUIRE(second_position_, read_type_second, output_exit,     \
                              instruction);                                        \
        VMBIN_HANDLER_REQUIRE(third_position_, read_type_third, output_exit,       \
                              instruction);                                        \
        VMBIN_HANDLER_REQUIRE_WRITABLE(write_position_, output_exit, instruction); \
        VMBIN_HANDLER_REQUIRE(write_position_, write_type, output_exit,            \
                              instruction);                                        \
        output_exit->logical_program_counter =                                     \
            instruction->source_logical_program_counter;                           \
        function((const read_type_first *)(const void *)first_position_.read_bytes,\
                 (const read_type_second *)(const void *)second_position_.read_bytes,\
                 (const read_type_third *)(const void *)third_position_.read_bytes,\
                 (write_type *)(void *)write_position_.write_bytes, output_exit);  \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }

#define VMBIN_HANDLER_SCATTER_READ_SCALAR(handler_name, function, write_type)      \
    static runtimec_status_t handler_name(                                         \
        binrwir_binvthread_frame_t *frame,                                          \
        const binrwir_instruction_view_t *instruction,                             \
        binrwir_execution_exit_t *output_exit) {                                   \
        vmbin_value_location_t write_position_;                                    \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 1U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        VMBIN_HANDLER_REQUIRE_WRITABLE(write_position_, output_exit, instruction); \
        VMBIN_HANDLER_REQUIRE(write_position_, write_type, output_exit,            \
                              instruction);                                        \
        output_exit->logical_program_counter =                                     \
            instruction->source_logical_program_counter;                           \
        function(frame, instruction,                                               \
                 (write_type *)(void *)write_position_.write_bytes, output_exit);  \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }

#define VMBIN_HANDLER_SCATTER_CREATE(handler_name, function)                       \
    static runtimec_status_t handler_name(                                         \
        binrwir_binvthread_frame_t *frame,                                          \
        const binrwir_instruction_view_t *instruction,                             \
        binrwir_execution_exit_t *output_exit) {                                   \
        if (instruction->write_arguments.count != 1U ||                            \
            frame->pathspace_handle == NULL) {                                       \
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,               \
                               VMBIN_HANDLER_CALL_DIAG_WRONG_ARITY,                \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        output_exit->logical_program_counter =                                     \
            instruction->source_logical_program_counter;                           \
        function(frame, instruction, output_exit);                                 \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }

#define VMBIN_HANDLER_SCATTER_READ_WRITE(handler_name, function)                   \
    static runtimec_status_t handler_name(                                         \
        binrwir_binvthread_frame_t *frame,                                          \
        const binrwir_instruction_view_t *instruction,                             \
        binrwir_execution_exit_t *output_exit) {                                   \
        vmbin_value_location_t write_position_;                                    \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 2U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        VMBIN_HANDLER_REQUIRE_WRITABLE(write_position_, output_exit, instruction); \
        output_exit->logical_program_counter =                                     \
            instruction->source_logical_program_counter;                           \
        function(frame, instruction, write_position_.write_bytes,                  \
                 write_position_.size, output_exit);                               \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }

#define VMBIN_HANDLER_SCATTER_WRITE_READS(handler_name, function)                  \
    static runtimec_status_t handler_name(                                         \
        binrwir_binvthread_frame_t *frame,                                          \
        const binrwir_instruction_view_t *instruction,                             \
        binrwir_execution_exit_t *output_exit) {                                   \
        if (instruction->read_arguments.count < 2U ||                              \
            instruction->write_arguments.count != 1U ||                            \
            frame->pathspace_handle == NULL) {                                       \
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,               \
                               VMBIN_HANDLER_CALL_DIAG_WRONG_ARITY,                \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        output_exit->logical_program_counter =                                     \
            instruction->source_logical_program_counter;                           \
        function(frame, instruction, output_exit);                                 \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }

#define VMBIN_HANDLER_SCATTER_CROSS(handler_name, function)                        \
    static runtimec_status_t handler_name(                                         \
        binrwir_binvthread_frame_t *frame,                                          \
        const binrwir_instruction_view_t *instruction,                             \
        binrwir_execution_exit_t *output_exit) {                                   \
        if (instruction->read_arguments.count < 2U ||                              \
            instruction->write_arguments.count != 1U ||                            \
            frame->pathspace_handle == NULL) {                                       \
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,               \
                               VMBIN_HANDLER_CALL_DIAG_WRONG_ARITY,                \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        output_exit->logical_program_counter =                                     \
            instruction->source_logical_program_counter;                           \
        function(frame, instruction, output_exit);                                 \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }

#define VMBIN_HANDLER_SCATTER_EXPAND(handler_name, function)                       \
    static runtimec_status_t handler_name(                                         \
        binrwir_binvthread_frame_t *frame,                                          \
        const binrwir_instruction_view_t *instruction,                             \
        binrwir_execution_exit_t *output_exit) {                                   \
        if (instruction->read_arguments.count != 1U ||                             \
            instruction->write_arguments.count != 1U ||                            \
            frame->pathspace_handle == NULL) {                                       \
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,               \
                               VMBIN_HANDLER_CALL_DIAG_WRONG_ARITY,                \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        output_exit->logical_program_counter =                                     \
            instruction->source_logical_program_counter;                           \
        function(frame, instruction, output_exit);                                 \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }

#ifdef __cplusplus
}
#endif

#endif
