
#include "contract/kvspace_interface.h"
#include "vmbin/directory_walk.h"
#include "vmbin/handlers/scatter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "binrwir/execution_exit.h"
#include "binrwir/binvthread_frame.h"
#include "binrwir/instruction.h"
#include "binrwir/operation_identifiers.h"
#include "contract/error.h"
#include "contract/status.h"
#include "contract/view.h"
#include <kvlang/xvalue.h>
#include <kvspace/kvspace_xvalue.h>
#include "vmbin/handlers/container.h"
#include "vmbin/frame_target_key.h"
#include "vmbin/handlers/handler_call.h"
#include "vmbin/value_access.h"

#define SCATTER_KEY_CAPACITY 512U
#define SCATTER_BODY_CAPACITY 512U

#define SCATTER_MEMBER_SEPARATOR "\xC2\xB7"

static bool scatter_member_prefix(const char *base, char *buffer,
                                  size_t capacity,
                                  binrwir_execution_exit_t *output_exit);
static runtimec_status_t scatter_member_key(const char *base, int64_t coordinate,
                                            char *buffer, size_t capacity);

static bool scatter_container_empty(const binrwir_binvthread_frame_t *frame,
                                    const char *base,
                                    binrwir_execution_exit_t *output_exit) {
    char directory[SCATTER_KEY_CAPACITY + 2U];
    char message[256];
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;

    if (kvlangXvalueHeadNewSlack(NULL, 0U, "stringkeymap", &wire,
                                  &wire_size) != 0 ||
        wire == NULL) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                           VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED,
                           output_exit->logical_program_counter);
        return false;
    }
    message[0] = '\0';
    if (!runtimec_kvspace_write_wire(frame->kvspace_handle, base, wire,
                                   (uint32_t)wire_size, message,
                                   sizeof(message))) {
        free(wire);
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                           VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED,
                           output_exit->logical_program_counter);
        return false;
    }
    free(wire);
    if (!scatter_member_prefix(base, directory, sizeof(directory),
                               output_exit)) {
        return false;
    }
    message[0] = '\0';
    if (kvspaceMkindex(frame->kvspace_handle, directory, message,
                       (uint32_t)sizeof(message)) != 0) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                           VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED,
                           output_exit->logical_program_counter);
        return false;
    }
    return true;
}

static void scatter_container_create(const binrwir_binvthread_frame_t *frame,
                                     const binrwir_instruction_view_t *instruction,
                                     binrwir_execution_exit_t *output_exit) {
    runtimec_string_view_t declared;
    char key[SCATTER_KEY_CAPACITY];
    char langtype_text[SCATTER_KEY_CAPACITY];
    char directory[SCATTER_KEY_CAPACITY + 2U];
    char message[256];
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    runtimec_error_t error;

    runtimec_error_reset(&error);
    if (vmbin_value_access_langtype(
            frame, &instruction->write_arguments.data[0], &declared, &error) !=
            RUNTIMEC_STATUS_SUCCESS ||
        declared.size + 1U > sizeof(langtype_text)) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    memcpy(langtype_text, declared.data, declared.size);
    langtype_text[declared.size] = '\0';
    
    if (kvlangXvalueHeadNewSlack(NULL, 0U, langtype_text, &wire,
                                  &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                           VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED,
                           output_exit->logical_program_counter);
        return;
    }
    if (vmbin_frame_target_key(frame, &instruction->write_arguments.data[0], key,
                               sizeof(key)) != RUNTIMEC_STATUS_SUCCESS) {
        free(wire);
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,
                           output_exit->logical_program_counter);
        return;
    }
    message[0] = '\0';
    if (strlen(key) + 2U > sizeof(directory)) {
        free(wire);
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    if (!runtimec_kvspace_write_wire(frame->kvspace_handle, key, wire,
                                   (uint32_t)wire_size, message,
                                   sizeof(message))) {
        free(wire);
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                           VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED,
                           output_exit->logical_program_counter);
        return;
    }
    free(wire);
    
    if (!scatter_member_prefix(key, directory, sizeof(directory),
                               output_exit)) {
        return;
    }
    message[0] = '\0';
    if (kvspaceMkindex(frame->kvspace_handle, directory, message,
                       (uint32_t)sizeof(message)) != 0) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                           VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED,
                           output_exit->logical_program_counter);
    }
}

static inline void scatter_new_build(const binrwir_binvthread_frame_t *frame,
                                     const binrwir_instruction_view_t *instruction,
                                     binrwir_execution_exit_t *output_exit) {
    scatter_container_create(frame, instruction, output_exit);
}

VMBIN_HANDLER_SCATTER_CREATE(scatter_new_handler, scatter_new_build)

static runtimec_status_t scatter_whole_wire(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_bytes_view_t *output,
    runtimec_error_t *error) {
    vmbin_value_location_t location;
    runtimec_status_t status =
        vmbin_value_access_locate(frame, argument, &location, error);

    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (location.is_whole_wire) {
        *output = runtimec_bytes_view_make(location.read_bytes, location.size);
        return RUNTIMEC_STATUS_SUCCESS;
    }
    {
        const runtimec_xvalue_address_t base = runtimec_value_body_xvalue_address(
            (runtimec_value_body_address_t)(uintptr_t)location.read_bytes);
        const kvspaceXvalueHead *head;

        if (base == RUNTIMEC_XVALUE_ADDRESS_NONE) {
            return RUNTIMEC_STATUS_INVALID;
        }
        head = (const kvspaceXvalueHead *)(const void *)(uintptr_t)base;
        {
            const uint64_t total = kvspaceXvalueHeadTotalLen(head);

            if (total == 0U || total > UINT32_MAX) {
                return RUNTIMEC_STATUS_INVALID;
            }
            *output = runtimec_bytes_view_make(head, (size_t)total);
        }
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static bool scatter_base_key(const binrwir_binvthread_frame_t *frame,
                             const binrwir_argument_t *argument, char *buffer,
                             size_t capacity,
                             binrwir_execution_exit_t *output_exit) {
    if (vmbin_frame_target_key(frame, argument, buffer, capacity) !=
        RUNTIMEC_STATUS_SUCCESS) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,
                           output_exit->logical_program_counter);
        return false;
    }
    return true;
}

static runtimec_status_t scatter_member_coordinate(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, size_t ordinal,
    int64_t *output_coordinate, binrwir_execution_exit_t *output_exit) {
    vmbin_value_location_t position;
    runtimec_error_t error;

    runtimec_error_reset(&error);
    if (vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[ordinal], &position,
            &error) != RUNTIMEC_STATUS_SUCCESS ||
        position.size != sizeof(int64_t)) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return RUNTIMEC_STATUS_INVALID;
    }
    memcpy(output_coordinate, position.read_bytes, sizeof(int64_t));
    if (*output_coordinate < 0) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static bool scatter_member_copy(const binrwir_binvthread_frame_t *frame,
                                const char *source_base, int64_t source_coordinate,
                                const char *target_base,
                                int64_t target_coordinate, char *source_member,
                                char *target_member, size_t member_capacity,
                                binrwir_execution_exit_t *output_exit) {
    const uint8_t *stored = NULL;
    uint32_t stored_size = 0U;
    char message[256];

    if (scatter_member_key(source_base, source_coordinate, source_member,
                           member_capacity) != RUNTIMEC_STATUS_SUCCESS ||
        scatter_member_key(target_base, target_coordinate, target_member,
                           member_capacity) != RUNTIMEC_STATUS_SUCCESS) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return false;
    }
    if (!runtimec_kvspace_read_wire(frame->kvspace_handle, source_member,
                                    &stored, &stored_size) ||
        stored == NULL) {
        return true; 
    }
    message[0] = '\0';
    if (!runtimec_kvspace_write_wire(frame->kvspace_handle, target_member, stored,
                                   stored_size, message, sizeof(message))) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                           VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED,
                           output_exit->logical_program_counter);
        return false;
    }
    return true;
}

static void scatter_member_read(const binrwir_binvthread_frame_t *frame,
                                const binrwir_instruction_view_t *instruction,
                                uint8_t *output, size_t output_capacity,
                                binrwir_execution_exit_t *output_exit) {
    char base[SCATTER_KEY_CAPACITY];
    char member[SCATTER_KEY_CAPACITY];
    vmbin_value_location_t coordinate_position;
    const uint8_t *stored = NULL;
    uint32_t stored_size = 0U;
    int64_t coordinate = 0;
    runtimec_error_t error;

    runtimec_error_reset(&error);
    if (!scatter_base_key(frame, &instruction->read_arguments.data[0], base,
                          sizeof(base), output_exit)) {
        return;
    }
    if (vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[1], &coordinate_position,
            &error) != RUNTIMEC_STATUS_SUCCESS ||
        coordinate_position.size != sizeof(coordinate)) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    memcpy(&coordinate, coordinate_position.read_bytes, sizeof(coordinate));
    if (coordinate < 0 ||
        scatter_member_key(base, coordinate, member, sizeof(member)) !=
            RUNTIMEC_STATUS_SUCCESS) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    
    if (!runtimec_kvspace_read_wire(frame->kvspace_handle, member, &stored,
                                    &stored_size) ||
        stored == NULL || stored_size < KVSPACE_XVALUE_HEAD_LEN) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,
                           output_exit->logical_program_counter);
        return;
    }
    
    if (stored_size > output_capacity) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    memcpy(output, stored, stored_size);
}

static inline void scatter_at_build(const binrwir_binvthread_frame_t *frame,
                                    const binrwir_instruction_view_t *instruction,
                                    uint8_t *output, size_t output_capacity,
                                    binrwir_execution_exit_t *output_exit) {
    scatter_member_read(frame, instruction, output, output_capacity, output_exit);
}

static void scatter_member_write(const binrwir_binvthread_frame_t *frame,
                                 const char *member,
                                 const runtimec_bytes_view_t *value,
                                 binrwir_execution_exit_t *output_exit) {
    char message[256];

    message[0] = '\0';
    if (!runtimec_kvspace_write_wire(frame->kvspace_handle, member, value->data,
                                   (uint32_t)value->size, message,
                                   sizeof(message))) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                           VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED,
                           output_exit->logical_program_counter);
    }
}

static void scatter_store_at(const binrwir_binvthread_frame_t *frame,
                             const binrwir_instruction_view_t *instruction,
                             binrwir_execution_exit_t *output_exit) {
    char base[SCATTER_KEY_CAPACITY];
    char member[SCATTER_KEY_CAPACITY];
    runtimec_bytes_view_t value;
    int64_t coordinate = 0;
    runtimec_error_t error;

    runtimec_error_reset(&error);
    if (!scatter_base_key(frame, &instruction->write_arguments.data[0], base,
                          sizeof(base), output_exit) ||
        scatter_member_coordinate(frame, instruction, 1U, &coordinate,
                                  output_exit) != RUNTIMEC_STATUS_SUCCESS ||
        scatter_whole_wire(frame, &instruction->read_arguments.data[2], &value,
                           &error) != RUNTIMEC_STATUS_SUCCESS ||
        scatter_member_key(base, coordinate, member, sizeof(member)) !=
            RUNTIMEC_STATUS_SUCCESS) {
        if (output_exit->kind != BINRWIR_EXECUTION_EXIT_ERROR) {
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                               VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                               output_exit->logical_program_counter);
        }
        return;
    }
    scatter_member_write(frame, member, &value, output_exit);
}

static inline void scatter_set_build(const binrwir_binvthread_frame_t *frame,
                                     const binrwir_instruction_view_t *instruction,
                                     binrwir_execution_exit_t *output_exit) {
    scatter_store_at(frame, instruction, output_exit);
}

static void scatter_largest_coordinate(const binrwir_binvthread_frame_t *frame,
                                       const char *base, int64_t *output_largest,
                                       binrwir_execution_exit_t *output_exit) {
    char directory[SCATTER_KEY_CAPACITY + 2U];
    char name[SCATTER_KEY_CAPACITY];
    int64_t count = 0;

    *output_largest = -1;
    if (!scatter_member_prefix(base, directory, sizeof(directory),
                               output_exit)) {
        return;
    }
    runtimec_directory_walk_t walk;
    if (!runtimec_directory_walk_open(&walk, NULL, frame->kvspace_handle,
                                      directory, false, false) ||
        !runtimec_directory_walk_count(&walk, &count)) {
        (void)runtimec_directory_walk_close(&walk);
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                           VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,
                           output_exit->logical_program_counter);
        return;
    }
    for (int64_t ordinal = 0; ordinal < count; ++ordinal) {
        const char *entry_name = NULL;
        uint32_t name_size = 0U;

        if (!runtimec_directory_walk_at(&walk, ordinal, &entry_name,
                                        &name_size) ||
            name_size >= sizeof(name)) {
            continue;
        }
        memcpy(name, entry_name, name_size);
        name[name_size] = '\0';
        if (name[0] == '[') {
            char *end_of_number = NULL;
            const long long parsed = strtoll(name + 1, &end_of_number, 10);

            if (end_of_number != NULL && *end_of_number == ']' &&
                end_of_number[1] == '\0' && parsed > *output_largest) {
                *output_largest = (int64_t)parsed;
            }
        }
    }
    (void)runtimec_directory_walk_close(&walk);
}

static void scatter_append_value(const binrwir_binvthread_frame_t *frame,
                                 const binrwir_instruction_view_t *instruction,
                                 binrwir_execution_exit_t *output_exit) {
    char base[SCATTER_KEY_CAPACITY];
    char member[SCATTER_KEY_CAPACITY];
    runtimec_bytes_view_t wire;
    int64_t largest = -1;
    runtimec_error_t error;

    runtimec_error_reset(&error);
    if (!scatter_base_key(frame, &instruction->write_arguments.data[0], base,
                          sizeof(base), output_exit)) {
        return;
    }
    scatter_largest_coordinate(frame, base, &largest, output_exit);
    if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) {
        return;
    }
    if (scatter_member_key(base, largest + 1, member, sizeof(member)) !=
            RUNTIMEC_STATUS_SUCCESS ||
        scatter_whole_wire(frame, &instruction->read_arguments.data[1], &wire,
                           &error) != RUNTIMEC_STATUS_SUCCESS) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    scatter_member_write(frame, member, &wire, output_exit);
}

static inline void scatter_append_build(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    scatter_append_value(frame, instruction, output_exit);
}

static void scatter_expand_from_compact(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    runtimec_string_view_t source_langtype;
    vmbin_value_location_t source;
    runtimec_container_langtype_t source_type;
    char base[SCATTER_KEY_CAPACITY];
    char member[SCATTER_KEY_CAPACITY];
    char element_langtype[64];
    runtimec_error_t error;

    runtimec_error_reset(&error);
    if (vmbin_value_access_langtype(
            frame, &instruction->read_arguments.data[0], &source_langtype,
            &error) != RUNTIMEC_STATUS_SUCCESS ||
        runtimec_container_langtype_parse(source_langtype, &source_type) !=
            RUNTIMEC_STATUS_SUCCESS ||
        source_type.element_size == 0U ||
        vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[0], &source,
            &error) != RUNTIMEC_STATUS_SUCCESS) {
        if (output_exit->kind != BINRWIR_EXECUTION_EXIT_ERROR) {
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                               VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                               output_exit->logical_program_counter);
        }
        return;
    }
    
    if (!runtimec_container_open_marker(frame, instruction, base, sizeof(base),
                                        output_exit)) {
        return;
    }
    if (source_type.base.size + 1U > sizeof(element_langtype)) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    memcpy(element_langtype, source_type.base.data, source_type.base.size);
    element_langtype[source_type.base.size] = '\0';
    if (!scatter_container_empty(frame, base, output_exit)) {
        return;
    }
    
    {
        const size_t element_count = source.size / source_type.element_size;

        for (size_t ordinal = 0U; ordinal < element_count; ++ordinal) {
            uint8_t *wire = NULL;
            uint64_t wire_size = 0U;
            char message[256];

            if (scatter_member_key(base, (int64_t)ordinal, member,
                                   sizeof(member)) != RUNTIMEC_STATUS_SUCCESS ||
                kvlangXvalueHeadNewSlack(
                    source.read_bytes + ordinal * source_type.element_size,
                    source_type.element_size,
                    element_langtype, &wire, &wire_size) != 0) {
                vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                                   VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED,
                                   output_exit->logical_program_counter);
                return;
            }
            message[0] = '\0';
            if (!runtimec_kvspace_write_wire(frame->kvspace_handle, member, wire,
                                           (uint32_t)wire_size, message,
                                           sizeof(message))) {
                free(wire);
                vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                                   VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED,
                                   output_exit->logical_program_counter);
                return;
            }
            free(wire);
        }
    }
}

static inline void scatter_newscatter_build(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    scatter_expand_from_compact(frame, instruction, output_exit);
}

static void scatter_pack_to_compact(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    char base[SCATTER_KEY_CAPACITY];
    char directory[SCATTER_KEY_CAPACITY + 2U];
    char member[SCATTER_KEY_CAPACITY];
    runtimec_string_view_t target_langtype;
    int64_t member_count = 0;
    int64_t declared_count;
    uint8_t body[SCATTER_BODY_CAPACITY];
    size_t body_size = 0U;
    runtimec_error_t error;

    runtimec_error_reset(&error);
    if (!scatter_base_key(frame, &instruction->read_arguments.data[0], base,
                          sizeof(base), output_exit) ||
        vmbin_value_access_langtype(
            frame, &instruction->write_arguments.data[0], &target_langtype,
            &error) != RUNTIMEC_STATUS_SUCCESS) {
        if (output_exit->kind != BINRWIR_EXECUTION_EXIT_ERROR) {
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,
                               output_exit->logical_program_counter);
        }
        return;
    }
    
    declared_count = runtimec_container_shape_numel(target_langtype);
    if (declared_count < 0 ||
        !scatter_member_prefix(base, directory, sizeof(directory),
                               output_exit)) {
        if (output_exit->kind != BINRWIR_EXECUTION_EXIT_ERROR) {
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                               RUNTIMEC_CONTAINER_DIAG_UNPARSEABLE_LANGTYPE,
                               output_exit->logical_program_counter);
        }
        return;
    }
    runtimec_directory_walk_t walk;
    if (!runtimec_directory_walk_open(&walk, NULL, frame->kvspace_handle,
                                      directory, false, false) ||
        !runtimec_directory_walk_count(&walk, &member_count)) {
        (void)runtimec_directory_walk_close(&walk);
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                           VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,
                           output_exit->logical_program_counter);
        return;
    }
    (void)runtimec_directory_walk_close(&walk);
    if ((int64_t)member_count != declared_count) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_CONTAINER_DIAG_ELEMENT_COUNT_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    
    for (int64_t ordinal = 0; ordinal < declared_count; ++ordinal) {
        const uint8_t *stored = NULL;
        uint32_t stored_size = 0U;
        uint64_t element_length;

        if (scatter_member_key(base, ordinal, member, sizeof(member)) !=
            RUNTIMEC_STATUS_SUCCESS) {
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                               VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                               output_exit->logical_program_counter);
            return;
        }
        if (!runtimec_kvspace_read_wire(frame->kvspace_handle, member,
                                       &stored, &stored_size) ||
            stored == NULL || stored_size < KVSPACE_XVALUE_HEAD_LEN) {
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                               RUNTIMEC_CONTAINER_DIAG_SCATTER_HAS_HOLE,
                               output_exit->logical_program_counter);
            return;
        }
        element_length = kvspaceXvalueHeadLen(
            (const kvspaceXvalueHead *)(const void *)stored);
        if (body_size + element_length > sizeof(body)) {
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_OUT_OF_MEMORY,
                               RUNTIMEC_CONTAINER_DIAG_NEW_BODY_EXCEEDS_BUFFER,
                               output_exit->logical_program_counter);
            return;
        }
        memcpy(body + body_size, stored + KVSPACE_XVALUE_HEAD_LEN,
               (size_t)element_length);
        body_size += (size_t)element_length;
    }
    
    (void)vmbin_handler_write_new_value(frame, instruction, body, body_size,
                                        output_exit);
}

static inline void scatter_newcompact_build(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    scatter_pack_to_compact(frame, instruction, output_exit);
}

static void scatter_slice_between(const binrwir_binvthread_frame_t *frame,
                                  const binrwir_instruction_view_t *instruction,
                                  binrwir_execution_exit_t *output_exit) {
    char source_base[SCATTER_KEY_CAPACITY];
    char target_base[SCATTER_KEY_CAPACITY];
    char directory[SCATTER_KEY_CAPACITY + 2U];
    char source_member[SCATTER_KEY_CAPACITY];
    char target_member[SCATTER_KEY_CAPACITY];
    int64_t lo = 0;
    int64_t hi = 0;
    int64_t count = 0;

    if (!scatter_base_key(frame, &instruction->read_arguments.data[0],
                          source_base, sizeof(source_base), output_exit) ||
        scatter_member_coordinate(frame, instruction, 1U, &lo, output_exit) !=
            RUNTIMEC_STATUS_SUCCESS ||
        scatter_member_coordinate(frame, instruction, 2U, &hi, output_exit) !=
            RUNTIMEC_STATUS_SUCCESS ||
        !scatter_base_key(frame, &instruction->write_arguments.data[0],
                          target_base, sizeof(target_base), output_exit)) {
        return;
    }
    if (hi < lo || !scatter_member_prefix(source_base, directory,
                                          sizeof(directory), output_exit)) {
        if (output_exit->kind != BINRWIR_EXECUTION_EXIT_ERROR) {
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                               VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                               output_exit->logical_program_counter);
        }
        return;
    }
    
    {
        runtimec_directory_walk_t walk;
        if (!runtimec_directory_walk_open(&walk, NULL, frame->kvspace_handle,
                                          directory, false, false) ||
            !runtimec_directory_walk_count(&walk, &count)) {
            (void)runtimec_directory_walk_close(&walk);
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,
                               output_exit->logical_program_counter);
            return;
        }
        (void)runtimec_directory_walk_close(&walk);
    }
    if (hi > count) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    if (!scatter_container_empty(frame, target_base, output_exit)) {
        return;
    }
    for (int64_t ordinal = lo; ordinal < hi; ++ordinal) {
        if (!scatter_member_copy(frame, source_base, ordinal, target_base,
                                 ordinal - lo, source_member, target_member,
                                 sizeof(source_member), output_exit)) {
            return;
        }
    }
}

static inline void scatter_slice_build(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    scatter_slice_between(frame, instruction, output_exit);
}

VMBIN_HANDLER_SCATTER_READ_WRITE(scatter_at_handler, scatter_at_build)
VMBIN_HANDLER_SCATTER_WRITE_READS(scatter_set_handler, scatter_set_build)
VMBIN_HANDLER_SCATTER_WRITE_READS(scatter_append_handler, scatter_append_build)
VMBIN_HANDLER_SCATTER_EXPAND(scatter_newscatter_handler,
                            scatter_newscatter_build)
VMBIN_HANDLER_SCATTER_EXPAND(scatter_newcompact_handler,
                            scatter_newcompact_build)
VMBIN_HANDLER_SCATTER_CROSS(scatter_slice_handler, scatter_slice_build)

static bool scatter_member_prefix(const char *base, char *buffer,
                                  size_t capacity,
                                  binrwir_execution_exit_t *output_exit) {
    const int written =
        snprintf(buffer, capacity, "%s%s", base, SCATTER_MEMBER_SEPARATOR);

    if (written < 0 || (size_t)written >= capacity) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return false;
    }
    return true;
}

static void scatter_member_count(const binrwir_binvthread_frame_t *frame,
                                 const binrwir_instruction_view_t *instruction,
                                 int64_t *out,
                                 binrwir_execution_exit_t *output_exit) {
    char base[SCATTER_KEY_CAPACITY];
    char directory[SCATTER_KEY_CAPACITY + 2U];
    int64_t count = 0;

    if (!scatter_base_key(frame, &instruction->read_arguments.data[0], base,
                          sizeof(base), output_exit)) {
        return;
    }
    if (!scatter_member_prefix(base, directory, sizeof(directory),
                               output_exit)) {
        return;
    }
    {
        runtimec_directory_walk_t walk;
        if (!runtimec_directory_walk_open(&walk, NULL, frame->kvspace_handle,
                                          directory, false, false) ||
            !runtimec_directory_walk_count(&walk, &count)) {
            (void)runtimec_directory_walk_close(&walk);
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,
                               output_exit->logical_program_counter);
            return;
        }
        (void)runtimec_directory_walk_close(&walk);
    }
    *out = (int64_t)count;
}

static inline void scatter_length_build(const binrwir_binvthread_frame_t *frame,
                                        const binrwir_instruction_view_t *instruction,
                                        int64_t *out,
                                        binrwir_execution_exit_t *output_exit) {
    scatter_member_count(frame, instruction, out, output_exit);
}

VMBIN_HANDLER_SCATTER_READ_SCALAR(scatter_length_handler, scatter_length_build,
                                  int64_t)

static runtimec_status_t scatter_member_key(const char *base, int64_t coordinate,
                                            char *buffer, size_t capacity) {
    char coordinate_text[32];
    const int coordinate_written =
        snprintf(coordinate_text, sizeof(coordinate_text), "[%lld]",
                 (long long)coordinate);
    const size_t base_length = strlen(base);
    const size_t separator_length = sizeof(SCATTER_MEMBER_SEPARATOR) - 1U;
    size_t total_length;

    if (coordinate_written < 0 ||
        (size_t)coordinate_written >= sizeof(coordinate_text)) {
        return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
    }
    total_length = base_length + separator_length + (size_t)coordinate_written;
    if (total_length + 1U > capacity) {
        return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
    }
    memcpy(buffer, base, base_length);
    memcpy(buffer + base_length, SCATTER_MEMBER_SEPARATOR, separator_length);
    memcpy(buffer + base_length + separator_length, coordinate_text,
           (size_t)coordinate_written);
    buffer[total_length] = '\0';
    return RUNTIMEC_STATUS_SUCCESS;
}

static const binrwir_registry_entry_t runtimec_scatter_entry_table[] = {
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_SCATTER_ARRAY_NEW,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scatter_new_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_SCATTER_ARRAY_LENGTH,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scatter_length_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_SCATTER_ARRAY_AT,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scatter_at_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_SCATTER_ARRAY_SET,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scatter_set_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_SCATTER_ARRAY_APPEND,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scatter_append_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_SCATTER_ARRAY_NEW_SCATTER,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scatter_newscatter_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_SCATTER_ARRAY_NEW_COMPACT,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scatter_newcompact_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_SCATTER_ARRAY_SLICE,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scatter_slice_handler},
};

const binrwir_registry_entry_t *runtimec_scatter_handler_entries(
    size_t *output_count) {
    if (output_count == NULL) {
        return NULL;
    }
    *output_count = sizeof(runtimec_scatter_entry_table) /
                    sizeof(runtimec_scatter_entry_table[0]);
    return runtimec_scatter_entry_table;
}
