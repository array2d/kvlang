
#define _POSIX_C_SOURCE 200809L

#include "vmbin/handlers/host.h"

#include "contract/char_codec.h"
#include "contract/langtype.h"
#include "contract/diagnostic_band.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "binrwir/execution_exit.h"
#include "binrwir/binvthread_frame.h"
#include "binrwir/instruction.h"
#include "binrwir/operation_identifiers.h"
#include "binrwir/binfunc.h"
#include "contract/member_key_text.h"
#include "contract/status.h"
#include "contract/pathspace_interface.h"
#include "vmbin/frame_target_key.h"
#include "vmbin/directory_walk.h"
#include "vmbin/handlers/handler_call.h"
#include "vmbin/value_access.h"
#include "vmbin/vthread_status.h"

#define RUNTIMEC_HOST_MEMBER_SEPARATOR "\xC2\xB7"
#define RUNTIMEC_HOST_MEMBER_SEPARATOR_LENGTH 2U

#define RUNTIMEC_HOST_KEY_CAPACITY 1024U

static runtimec_status_t runtimec_host_static_key(
    const binrwir_binvthread_frame_t *frame, const binrwir_argument_t *argument,
    char *buffer, size_t capacity) {
    runtimec_string_view_t name;
    runtimec_string_view_t langtype;

    if (argument->reference_kind != BINRWIR_REFERENCE_STATIC) {
        return RUNTIMEC_STATUS_UNSUPPORTED;
    }
    if (binrwir_binfunc_get_symbol(frame->program, (uint16_t)argument->slot,
                                   &name, &langtype) !=
            RUNTIMEC_STATUS_SUCCESS ||
        name.size == 0U || name.size >= capacity) {
        return RUNTIMEC_STATUS_INVALID;
    }
    memcpy(buffer, name.data, name.size);
    buffer[name.size] = '\0';
    return RUNTIMEC_STATUS_SUCCESS;
}

typedef enum runtimec_host_diagnostic {
    RUNTIMEC_HOST_DIAG_NONE = 0,
    
    RUNTIMEC_HOST_DIAG_ARITY = 0x4601,
    
    RUNTIMEC_HOST_DIAG_MISSING_HANDLE = 0x4602,
    
    RUNTIMEC_HOST_DIAG_KEY_UNRESOLVED = 0x4603,
    
    RUNTIMEC_HOST_DIAG_BACKEND_FAILED = 0x4604,
    
    RUNTIMEC_HOST_DIAG_OPERAND_INVALID = 0x4605,
    
    RUNTIMEC_HOST_DIAG_WRITE_FAILED = 0x4606,
    
    RUNTIMEC_HOST_DIAG_MALFORMED_WIRE = 0x4607,
    
    RUNTIMEC_HOST_DIAG_UNSUPPORTED = 0x4608
} runtimec_host_diagnostic_t;

_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(RUNTIMEC_HOST_DIAG_UNSUPPORTED,
                                           RUNTIMEC_DIAGNOSTIC_BAND_HOST),
               "host diagnostic must stay inside its band");
_Static_assert(RUNTIMEC_HOST_DIAG_NONE == 0,
               "0 is reserved for \"unset\"; failure paths must never report it");

static runtimec_status_t runtimec_host_fail(
    binrwir_execution_exit_t *output_exit, runtimec_status_t status,
    uint32_t detail_code, uint32_t source_program_counter) {
    *output_exit = (binrwir_execution_exit_t){0};
    output_exit->kind = BINRWIR_EXECUTION_EXIT_ERROR;
    output_exit->status = status;
    output_exit->detail_code = detail_code;
    output_exit->logical_program_counter = source_program_counter;
    output_exit->resume_logical_program_counter = source_program_counter;
    return status;
}

static bool runtimec_host_prepare_call(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit, size_t expected_reads,
    void **output_handle, char keys[][RUNTIMEC_HOST_KEY_CAPACITY]) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    runtimec_status_t status;

    *output_exit = (binrwir_execution_exit_t){0};
    if (instruction->read_arguments.count != expected_reads) {
        runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_HOST_DIAG_ARITY, source_program_counter);
        return false;
    }
    status = runtimec_vthread_status_handle(frame, output_handle);
    for (size_t ordinal = 0U; ordinal < expected_reads &&
                              status == RUNTIMEC_STATUS_SUCCESS;
         ++ordinal) {
        status = runtimec_host_static_key(
            frame, &instruction->read_arguments.data[ordinal], keys[ordinal],
            RUNTIMEC_HOST_KEY_CAPACITY);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        runtimec_host_fail(output_exit, status, RUNTIMEC_HOST_DIAG_KEY_UNRESOLVED, source_program_counter);
        return false;
    }
    return true;
}

static runtimec_status_t runtimec_host_finish_call(
    binrwir_execution_exit_t *output_exit, int operation_status,
    uint32_t source_program_counter) {
    if (operation_status != 0) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                                  RUNTIMEC_HOST_DIAG_BACKEND_FAILED, source_program_counter);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static bool runtimec_host_read_uint64(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, uint64_t *output_value,
    binrwir_execution_exit_t *output_exit, uint32_t source_program_counter) {
    vmbin_value_location_t location;
    runtimec_error_t error;

    runtimec_error_reset(&error);
    if (vmbin_value_access_locate_body(frame, argument, &location, &error) !=
            RUNTIMEC_STATUS_SUCCESS ||
        location.read_bytes == NULL || location.size != sizeof(*output_value)) {
        runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_HOST_DIAG_OPERAND_INVALID, source_program_counter);
        return false;
    }
    memcpy(output_value, location.read_bytes, sizeof(*output_value));
    return true;
}

static runtimec_status_t runtimec_host_store_int64(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, int64_t value,
    binrwir_execution_exit_t *output_exit, uint32_t source_program_counter) {
    runtimec_error_t error;

    runtimec_error_reset(&error);
    if (vmbin_value_access_write_body(
            frame, &instruction->write_arguments.data[0],
            runtimec_bytes_view_make(&value, sizeof(value)),
            &error) != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                                  RUNTIMEC_HOST_DIAG_WRITE_FAILED, source_program_counter);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static uint64_t runtimec_host_random_u64(void) {
    uint64_t value = 0U;
    FILE *source = fopen("/dev/urandom", "rb");

    if (source == NULL) {
        return 0U;
    }
    if (fread(&value, sizeof(value), 1U, source) != 1U) {
        value = 0U;
    }
    fclose(source);
    return value;
}

static runtimec_status_t runtimec_host_random_unsigned_64(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;

    *output_exit = (binrwir_execution_exit_t){0};
    if (instruction->write_arguments.count != 1U) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_ARITY, source_program_counter);
    }
    return runtimec_host_store_int64(frame, instruction,
                                     (int64_t)runtimec_host_random_u64(),
                                     output_exit, source_program_counter);
}

static runtimec_status_t runtimec_host_random_signed_63(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;

    *output_exit = (binrwir_execution_exit_t){0};
    if (instruction->write_arguments.count != 1U) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_ARITY, source_program_counter);
    }
    return runtimec_host_store_int64(
        frame, instruction, (int64_t)(runtimec_host_random_u64() >> 1U),
        output_exit, source_program_counter);
}

static runtimec_status_t runtimec_host_random_below(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    uint64_t bound = 0U;
    uint64_t value;

    *output_exit = (binrwir_execution_exit_t){0};
    if (instruction->read_arguments.count != 1U ||
        instruction->write_arguments.count != 1U) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_ARITY, source_program_counter);
    }
    if (!runtimec_host_read_uint64(frame, &instruction->read_arguments.data[0],
                                   &bound, output_exit,
                                   source_program_counter)) {
        return RUNTIMEC_STATUS_ERROR;
    }
    value = bound == 0U ? 0U : runtimec_host_random_u64() % bound;
    return runtimec_host_store_int64(frame, instruction, (int64_t)value,
                                     output_exit, source_program_counter);
}

static runtimec_status_t runtimec_host_vthread_setstatus(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    char status_key[RUNTIMEC_HOST_KEY_CAPACITY];
    vmbin_value_location_t status_location;
    void *handle = NULL;
    runtimec_error_t error;

    runtimec_error_reset(&error);
    *output_exit = (binrwir_execution_exit_t){0};
    if (instruction->read_arguments.count != 1U) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_ARITY, source_program_counter);
    }
    if (runtimec_vthread_status_handle(frame, &handle) !=
            RUNTIMEC_STATUS_SUCCESS ||
        runtimec_vthread_status_key(
            frame->frame_path, runtimec_string_view_from_c_string("/‥status"),
            status_key, sizeof(status_key)) != RUNTIMEC_STATUS_SUCCESS ||
        vmbin_value_access_locate_body(frame,
                                       &instruction->read_arguments.data[0],
                                       &status_location,
                                       &error) != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_OPERAND_INVALID, source_program_counter);
    }
    if (runtimec_vthread_status_write(
            handle, status_key,
            runtimec_bytes_view_make(status_location.read_bytes,
                                     status_location.size)) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                                  RUNTIMEC_HOST_DIAG_BACKEND_FAILED, source_program_counter);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t runtimec_host_vthread_sleep(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    uint64_t duration_nanoseconds = 0U;

    *output_exit = (binrwir_execution_exit_t){0};
    if (instruction->read_arguments.count != 1U) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_KEY_UNRESOLVED, source_program_counter);
    }
    if (!runtimec_host_read_uint64(frame, &instruction->read_arguments.data[0],
                                   &duration_nanoseconds, output_exit,
                                   source_program_counter)) {
        return RUNTIMEC_STATUS_ERROR;
    }
    if (duration_nanoseconds > 0U) {
        const struct timespec interval = {
            .tv_sec = (time_t)(duration_nanoseconds / UINT64_C(1000000000)),
            .tv_nsec = (long)(duration_nanoseconds % UINT64_C(1000000000)),
        };

        nanosleep(&interval, NULL);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t runtimec_host_vthread_not_implemented(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    (void)frame;
    return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_UNSUPPORTED,
                              RUNTIMEC_HOST_DIAG_UNSUPPORTED,
                              instruction->source_logical_program_counter);
}

static runtimec_status_t runtimec_host_single_path_operation(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit,
    int (*operation)(void *handle, const char *path, char *error,
                     uint32_t error_capacity)) {
    char keys[1][RUNTIMEC_HOST_KEY_CAPACITY];
    char message[256];
    void *handle = NULL;

    if (!runtimec_host_prepare_call(frame, instruction, output_exit, 1U,
                                    &handle, keys)) {
        return RUNTIMEC_STATUS_ERROR;
    }
    message[0] = '\0';
    return runtimec_host_finish_call(
        output_exit, operation(handle, keys[0], message, sizeof(message)),
        instruction->source_logical_program_counter);
}

static runtimec_status_t runtimec_host_copy_operation(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit,
    int (*operation)(void *handle, const char *source,
                     const char *destination, char *error,
                     uint32_t error_capacity)) {
    char keys[1][RUNTIMEC_HOST_KEY_CAPACITY];
    char destination[RUNTIMEC_HOST_KEY_CAPACITY];
    char message[256];
    void *handle = NULL;
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;

    if (instruction->write_arguments.count != 1U ||
        !runtimec_host_prepare_call(frame, instruction, output_exit, 1U,
                                    &handle, keys)) {
        return RUNTIMEC_STATUS_ERROR;
    }
    if (runtimec_host_static_key(frame,
                                 &instruction->write_arguments.data[0],
                                 destination, sizeof(destination)) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_KEY_UNRESOLVED,
                                  source_program_counter);
    }
    message[0] = '\0';
    return runtimec_host_finish_call(
        output_exit,
        operation(handle, keys[0], destination, message, sizeof(message)),
        source_program_counter);
}

static int runtimec_host_delete_one(void *handle, const char *path, char *error,
                                    uint32_t error_capacity) {
    const char *keys[1];

    keys[0] = path;
    
    return pathspaceFree(handle, keys[0], error, error_capacity);
}

static runtimec_status_t runtimec_host_pathspace_delete(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return runtimec_host_single_path_operation(frame, instruction, output_exit,
                                               runtimec_host_delete_one);
}
static runtimec_status_t runtimec_host_pathspace_delete_tree(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return runtimec_host_single_path_operation(frame, instruction, output_exit,
                                               pathspaceDeltree);
}
static runtimec_status_t runtimec_host_pathspace_copy(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return runtimec_host_copy_operation(frame, instruction, output_exit,
                                            pathspaceCp);
}
static runtimec_status_t runtimec_host_pathspace_copy_directory(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return runtimec_host_copy_operation(frame, instruction, output_exit,
                                            pathspaceCpdir);
}

static runtimec_status_t runtimec_host_pathspace_make_index(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    char keys[1][RUNTIMEC_HOST_KEY_CAPACITY];
    char message[256];
    void *handle = NULL;

    if (!runtimec_host_prepare_call(frame, instruction, output_exit, 1U,
                                    &handle, keys)) {
        return RUNTIMEC_STATUS_ERROR;
    }
    message[0] = '\0';
    return runtimec_host_finish_call(
        output_exit,
        pathspaceMkindex(handle, keys[0], message, sizeof(message)),
        source_program_counter);
}

static runtimec_status_t runtimec_host_pathspace_list_length(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    char keys[1][RUNTIMEC_HOST_KEY_CAPACITY];
    int64_t count = 0;
    void *handle = NULL;

    if (instruction->write_arguments.count != 1U ||
        !runtimec_host_prepare_call(frame, instruction, output_exit, 1U,
                                    &handle, keys)) {
        return RUNTIMEC_STATUS_ERROR;
    }
    
    {
        runtimec_directory_walk_t walk;
        if (!runtimec_directory_walk_open(&walk, NULL, handle, keys[0], false,
                                          false) ||
            !runtimec_directory_walk_count(&walk, &count)) {
            count = 0;
        }
        (void)runtimec_directory_walk_close(&walk);
    }
    return runtimec_host_store_int64(frame, instruction, (int64_t)count,
                                     output_exit, source_program_counter);
}

static runtimec_status_t runtimec_host_pathspace_list_at(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    char path[RUNTIMEC_HOST_KEY_CAPACITY];
    char name[RUNTIMEC_HOST_KEY_CAPACITY];
    vmbin_value_location_t index_location;
    int64_t index = 0;
    void *handle = NULL;
    runtimec_error_t error;
    runtimec_status_t status;

    runtimec_error_reset(&error);
    *output_exit = (binrwir_execution_exit_t){0};
    if (instruction->read_arguments.count != 2U ||
        instruction->write_arguments.count != 1U) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_UNSUPPORTED, source_program_counter);
    }
    status = runtimec_vthread_status_handle(frame, &handle);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_host_static_key(frame, &instruction->read_arguments.data[0],
                                          path, sizeof(path));
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[1], &index_location, &error);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, status, RUNTIMEC_HOST_DIAG_ARITY, source_program_counter);
    }
    if (index_location.read_bytes == NULL ||
        index_location.size != sizeof(index)) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_KEY_UNRESOLVED, source_program_counter);
    }
    memcpy(&index, index_location.read_bytes, sizeof(index));
    if (index >= 0) {
        runtimec_directory_walk_t walk;
        const char *entry_name = NULL;
        uint32_t name_length = 0U;
        const bool listed =
            runtimec_directory_walk_open(&walk, NULL, handle, path, false,
                                         false) &&
            runtimec_directory_walk_at(&walk, index, &entry_name,
                                       &name_length) &&
            entry_name != NULL && name_length != 0U &&
            (size_t)name_length < sizeof(name);
        if (listed) {
            memcpy(name, entry_name, name_length);
        }
        (void)runtimec_directory_walk_close(&walk);
        if (listed) {
            status = vmbin_value_access_write_body(
                frame, &instruction->write_arguments.data[0],
                runtimec_bytes_view_make(name, (size_t)name_length), &error);
            if (status != RUNTIMEC_STATUS_SUCCESS) {
                return runtimec_host_fail(output_exit, status,
                                          RUNTIMEC_HOST_DIAG_ARITY, source_program_counter);
            }
        }
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t runtimec_host_write_pointer_wire(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, const uint8_t *wire,
    size_t wire_size, binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    char key[RUNTIMEC_HOST_KEY_CAPACITY];
    char message[256];
    runtimec_error_t error;
    runtimec_status_t status;

    runtimec_error_reset(&error);
    status = vmbin_value_access_argument_key(
        frame, &instruction->write_arguments.data[0], key, sizeof(key), &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_KEY_UNRESOLVED,
                                  source_program_counter);
    }
    message[0] = '\0';
    if (!runtimec_pathspace_write_wire(frame->pathspace_handle, key, wire,
                                   (uint32_t)wire_size, message,
                                   sizeof(message))) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                                  RUNTIMEC_HOST_DIAG_BACKEND_FAILED,
                                  source_program_counter);
    }
    status = vmbin_value_access_bind_write_target(
        frame, &instruction->write_arguments.data[0],
        runtimec_key_view_make(key, strlen(key)), &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, status,
                                  RUNTIMEC_HOST_DIAG_WRITE_FAILED,
                                  source_program_counter);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

#define RUNTIMEC_HOST_VTHREAD_PREFIX "/vthread/"

static runtimec_status_t runtimec_host_vthread_path_root_end(
    runtimec_key_view_t frame_path, size_t *output_root_end,
    binrwir_execution_exit_t *output_exit, uint32_t source_program_counter) {
    unsigned slash_count = 0U;
    size_t ordinal;

    for (ordinal = 1U; ordinal < frame_path.size; ++ordinal) {
        if (frame_path.data[ordinal] != '/') {
            continue;
        }
        slash_count += 1U;
        if (slash_count == 2U) {
            *output_root_end = ordinal;
            return RUNTIMEC_STATUS_SUCCESS;
        }
    }
    return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                              RUNTIMEC_HOST_DIAG_KEY_UNRESOLVED,
                              source_program_counter);
}

static runtimec_status_t runtimec_host_vthread_identifier_from_path(
    runtimec_key_view_t frame_path, size_t root_end,
    int32_t *output_identifier, binrwir_execution_exit_t *output_exit,
    uint32_t source_program_counter) {
    const size_t prefix = sizeof(RUNTIMEC_HOST_VTHREAD_PREFIX) - 1U;
    int32_t value = 0;
    size_t index;

    if (root_end <= prefix) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_KEY_UNRESOLVED,
                                  source_program_counter);
    }
    for (index = prefix; index < root_end; ++index) {
        const int digit = frame_path.data[index] - '0';

        if (digit < 0 || digit > 9 || value > (INT32_MAX - digit) / 10) {
            return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                      RUNTIMEC_HOST_DIAG_KEY_UNRESOLVED,
                                      source_program_counter);
        }
        value = value * 10 + digit;
    }
    *output_identifier = value;
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t runtimec_host_vthread_stack_from_path(
    runtimec_key_view_t frame_path, size_t root_end,
    int32_t *output_stack_identifier, binrwir_execution_exit_t *output_exit,
    uint32_t source_program_counter) {
    bool have_stack = false;
    int32_t value = 0;
    size_t index;

    for (index = root_end + 1U; index < frame_path.size; ++index) {
        const char character = frame_path.data[index];

        if (character == '[') {
            have_stack = true;
            continue;
        }
        if (!have_stack) {
            return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                      RUNTIMEC_HOST_DIAG_KEY_UNRESOLVED,
                                      source_program_counter);
        }
        if (character == ']') {
            break;
        }
        
        if (character < '0' || character > '9') {
            break;
        }
        value = value * 10 + (character - '0');
    }
    if (!have_stack) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_KEY_UNRESOLVED,
                                  source_program_counter);
    }
    *output_stack_identifier = value;
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t runtimec_host_vthread_write_results(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, int32_t vthread_identifier,
    int32_t stack_identifier, binrwir_execution_exit_t *output_exit,
    uint32_t source_program_counter) {
    int32_t identifier = vthread_identifier;
    
    int32_t position[2];

    position[0] = stack_identifier;
    position[1] = (int32_t)frame->logical_program_counter;
    if (vmbin_value_access_write_body(
            frame, &instruction->write_arguments.data[0],
            runtimec_bytes_view_make((const uint8_t *)(const void *)&identifier,
                                     sizeof(identifier)),
            NULL) != RUNTIMEC_STATUS_SUCCESS ||
        vmbin_value_access_write_body(
            frame, &instruction->write_arguments.data[1],
            runtimec_bytes_view_make((const uint8_t *)(const void *)position,
                                     sizeof(position)),
            NULL) != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_WRITE_FAILED,
                                  source_program_counter);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t runtimec_host_vthread_pc_precondition(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit,
    uint32_t source_program_counter) {
    if (instruction->read_arguments.count != 0U ||
        instruction->write_arguments.count != 2U) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_ARITY,
                                  source_program_counter);
    }
    if (frame == NULL || frame->frame_path.size == 0U ||
        frame->frame_path.data == NULL) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_KEY_UNRESOLVED,
                                  source_program_counter);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t runtimec_host_vthread_pc(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    int32_t vthread_identifier = 0;
    int32_t stack_identifier = 0;
    size_t root_end = 0U;
    runtimec_status_t status;

    *output_exit = (binrwir_execution_exit_t){0};
    status = runtimec_host_vthread_pc_precondition(frame, instruction,
                                                   output_exit,
                                                   source_program_counter);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_host_vthread_path_root_end(
            frame->frame_path, &root_end, output_exit, source_program_counter);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_host_vthread_identifier_from_path(
            frame->frame_path, root_end, &vthread_identifier, output_exit,
            source_program_counter);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_host_vthread_stack_from_path(
            frame->frame_path, root_end, &stack_identifier, output_exit,
            source_program_counter);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_host_vthread_write_results(
            frame, instruction, vthread_identifier, stack_identifier,
            output_exit, source_program_counter);
    }
    return status;
}

static runtimec_status_t runtimec_host_build_pointer_wire(
    binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *declared_source, const char *target,
    uint8_t **output_wire, uint32_t *output_wire_size,
    runtimec_error_t *error) {
    runtimec_string_view_t declared;
    char kindexpr[RUNTIMEC_HOST_KEY_CAPACITY];
    size_t kindexpr_size = 0U;
    runtimec_status_t status;

    status = vmbin_value_access_langtype(frame, declared_source, &declared,
                                         error);
    if (status != RUNTIMEC_STATUS_SUCCESS || declared.size == 0U) {
        return RUNTIMEC_STATUS_INVALID;
    }
    
    while (kindexpr_size < declared.size &&
           (declared.data[kindexpr_size] == '*' ||
            declared.data[kindexpr_size] == '@')) {
        kindexpr_size += 1U;
    }
    if (declared.size == kindexpr_size ||
        declared.size - kindexpr_size + 1U > sizeof(kindexpr)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    memcpy(kindexpr, declared.data + kindexpr_size, declared.size - kindexpr_size);
    kindexpr[declared.size - kindexpr_size] = '\0';
    
    uint64_t wire_size = 0U;
    if (pathlangXvalueHeadNewPtr((const uint8_t *)target,
                                (uint32_t)strlen(target), kindexpr, output_wire,
                                &wire_size) != 0 ||
        *output_wire == NULL || wire_size > UINT32_MAX) {
        return RUNTIMEC_STATUS_ERROR;
    }
    *output_wire_size = (uint32_t)wire_size;
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t runtimec_host_address_of_wire(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, char *target,
    size_t target_capacity, uint8_t **output_wire, uint32_t *output_wire_size,
    runtimec_error_t *error, binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    runtimec_status_t status;

    if (instruction->read_arguments.count != 1U ||
        instruction->write_arguments.count != 1U) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_ARITY,
                                  source_program_counter);
    }
    status = vmbin_value_access_argument_key(
        frame, &instruction->read_arguments.data[0], target, target_capacity,
        error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_KEY_UNRESOLVED,
                                  source_program_counter);
    }
    status = runtimec_host_build_pointer_wire(
        frame, &instruction->read_arguments.data[0], target, output_wire,
        output_wire_size, error);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    return runtimec_host_fail(
        output_exit,
        status == RUNTIMEC_STATUS_ERROR ? RUNTIMEC_STATUS_ERROR
                                        : RUNTIMEC_STATUS_INVALID,
        status == RUNTIMEC_STATUS_ERROR ? RUNTIMEC_HOST_DIAG_BACKEND_FAILED
                                        : RUNTIMEC_HOST_DIAG_KEY_UNRESOLVED,
        source_program_counter);
}

static runtimec_status_t runtimec_host_key_address_of(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    char target[RUNTIMEC_HOST_KEY_CAPACITY];
    uint8_t *wire = NULL;
    uint32_t wire_size = 0U;
    runtimec_error_t error;
    runtimec_status_t status;

    runtimec_error_reset(&error);
    *output_exit = (binrwir_execution_exit_t){0};
    status = runtimec_host_address_of_wire(frame, instruction, target,
                                           sizeof(target), &wire, &wire_size,
                                           &error, output_exit);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    
    {
        runtimec_status_t created = runtimec_host_write_pointer_wire(
            frame, instruction, wire, (size_t)wire_size, output_exit);
        free(wire);
        return created;
    }
}

static runtimec_status_t runtimec_host_key_tostr_fail(
    binrwir_execution_exit_t *output_exit, runtimec_status_t status,
    int64_t diagnostic, size_t required_length,
    uint32_t source_program_counter);

static bool runtimec_host_path_to_utf8(runtimec_langtype_t parsed,
                                       vmbin_value_location_t value,
                                       char *output, size_t capacity,
                                       size_t *output_length) {
    const bool is_utf32 = parsed.base.size >= 10U &&
                          memcmp(parsed.base.data, "char/utf32", 10U) == 0;
    const bool is_ascii = parsed.base.size >= 10U &&
                          memcmp(parsed.base.data, "char/ascii", 10U) == 0;

    *output_length = 0U;
    if (is_utf32) {
        return runtimec_char_utf32_to_utf8(value.read_bytes, value.size, output,
                                           capacity, output_length);
    }
    if (is_ascii) {
        return runtimec_char_ascii_to_utf8(value.read_bytes, value.size, output,
                                           capacity, output_length);
    }
    if (value.size >= capacity) {
        return false;
    }
    if (value.size != 0U) {
        memcpy(output, value.read_bytes, value.size);
    }
    *output_length = value.size;
    return true;
}

static bool runtimec_host_base_is_character(runtimec_string_view_t langtype) {
    runtimec_langtype_t parsed;

    if (runtimec_langtype_parse(langtype, &parsed) != RUNTIMEC_STATUS_SUCCESS) {
        return false;
    }
    return parsed.base.size >= (sizeof("char/") - 1U) &&
           memcmp(parsed.base.data, "char/", sizeof("char/") - 1U) == 0;
}

static runtimec_status_t runtimec_host_resolve_base_path(
    binrwir_binvthread_frame_t *frame, const binrwir_argument_t *base,
    char *base_path, size_t capacity, size_t *output_length,
    binrwir_execution_exit_t *output_exit, uint32_t source_program_counter) {
    vmbin_value_location_t value;
    runtimec_string_view_t declared;
    runtimec_error_t error;

    runtimec_error_reset(&error);
    if (vmbin_value_access_locate_body(frame, base, &value, &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_OPERAND_INVALID,
                                  source_program_counter);
    }
    if (vmbin_value_access_langtype(frame, base, &declared, &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        declared = runtimec_string_view_make(NULL, 0U);
    }
    
    if (declared.size != 0U && declared.data[0] == '*') {
        *output_length = value.size;
        if (*output_length >= capacity) {
            return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                      VMBIN_HANDLER_CALL_DIAG_KEY_TOO_LONG,
                                      source_program_counter);
        }
        if (*output_length != 0U) {
            memcpy(base_path, value.read_bytes, *output_length);
        }
    } else if (vmbin_value_access_langtype_is_structref(declared)) {
        *output_length = value.size;
        if (*output_length >= capacity) {
            return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                      VMBIN_HANDLER_CALL_DIAG_KEY_TOO_LONG,
                                      source_program_counter);
        }
        if (*output_length != 0U) {
            memcpy(base_path, value.read_bytes, *output_length);
        }
    } else if (runtimec_host_base_is_character(declared)) {
        runtimec_langtype_t parsed;

        if (runtimec_langtype_parse(declared, &parsed) !=
                RUNTIMEC_STATUS_SUCCESS ||
            !runtimec_host_path_to_utf8(parsed, value, base_path, capacity,
                                        output_length)) {
            return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                      VMBIN_HANDLER_CALL_DIAG_MALFORMED_BODY,
                                      source_program_counter);
        }
        
        if (*output_length == 0U || base_path[0] != '/') {
            return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                      VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,
                                      source_program_counter);
        }
    } else if (vmbin_value_access_langtype_is_container(declared) &&
               value.size != 0U && value.read_bytes != NULL &&
               value.read_bytes[0] == '/') {
        
        *output_length = value.size;
        if (*output_length >= capacity) {
            return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                      VMBIN_HANDLER_CALL_DIAG_KEY_TOO_LONG,
                                      source_program_counter);
        }
        memcpy(base_path, value.read_bytes, *output_length);
    } else {
        if (vmbin_value_access_argument_key(frame, base, base_path, capacity,
                                            &error) != RUNTIMEC_STATUS_SUCCESS) {
            return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                      VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,
                                      source_program_counter);
        }
        *output_length = strlen(base_path);
    }
    base_path[*output_length] = '\0';
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t runtimec_host_resolve_key_text(
    binrwir_binvthread_frame_t *frame, const binrwir_argument_t *key,
    char *key_text, size_t capacity, size_t *output_length,
    binrwir_execution_exit_t *output_exit, uint32_t source_program_counter) {
    runtimec_string_view_t langtype;
    vmbin_value_location_t value;
    runtimec_error_t error;
    runtimec_status_t status;

    runtimec_error_reset(&error);
    status = vmbin_value_access_langtype(frame, key, &langtype, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS || langtype.size == 0U) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_UNSUPPORTED,
                                  VMBIN_HANDLER_CALL_DIAG_UNSUPPORTED_LANGTYPE,
                                  source_program_counter);
    }
    status = vmbin_value_access_locate_body(frame, key, &value, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, status,
                                  RUNTIMEC_HOST_DIAG_OPERAND_INVALID,
                                  source_program_counter);
    }
    status = runtimec_member_key_text(
        langtype, runtimec_bytes_view_make(value.read_bytes, value.size),
        key_text, capacity, output_length, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_key_tostr_fail(output_exit, status, error.code,
                                            *output_length,
                                            source_program_counter);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t runtimec_host_resolve_join(
    const char *base_path, size_t base_length, const char *key_text,
    size_t key_length, char *target, size_t capacity,
    binrwir_execution_exit_t *output_exit, uint32_t source_program_counter) {
    if (base_length + RUNTIMEC_HOST_MEMBER_SEPARATOR_LENGTH + key_length + 1U >
        capacity) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  VMBIN_HANDLER_CALL_DIAG_KEY_TOO_LONG,
                                  source_program_counter);
    }
    memcpy(target, base_path, base_length);
    memcpy(target + base_length, RUNTIMEC_HOST_MEMBER_SEPARATOR,
           RUNTIMEC_HOST_MEMBER_SEPARATOR_LENGTH);
    memcpy(target + base_length + RUNTIMEC_HOST_MEMBER_SEPARATOR_LENGTH,
           key_text, key_length);
    target[base_length + RUNTIMEC_HOST_MEMBER_SEPARATOR_LENGTH + key_length] =
        '\0';
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t runtimec_host_resolve_write(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, const char *target,
    binrwir_execution_exit_t *output_exit, uint32_t source_program_counter) {
    uint8_t *wire = NULL;
    uint32_t wire_size = 0U;
    runtimec_error_t error;
    runtimec_status_t status;

    runtimec_error_reset(&error);
    status = runtimec_host_build_pointer_wire(
        frame, &instruction->write_arguments.data[0], target, &wire,
        &wire_size, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(
            output_exit,
            status == RUNTIMEC_STATUS_ERROR ? RUNTIMEC_STATUS_ERROR
                                            : RUNTIMEC_STATUS_INVALID,
            status == RUNTIMEC_STATUS_ERROR
                ? RUNTIMEC_HOST_DIAG_BACKEND_FAILED
                : VMBIN_HANDLER_CALL_DIAG_UNSUPPORTED_LANGTYPE,
            source_program_counter);
    }
    status = runtimec_host_write_pointer_wire(frame, instruction, wire,
                                              (size_t)wire_size, output_exit);
    free(wire);
    return status;
}

static runtimec_status_t runtimec_host_pathlang_resolve(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    char base_path[RUNTIMEC_HOST_KEY_CAPACITY];
    char key_text[VMBIN_HANDLER_KEY_TEXT_MAXIMUM + 1U];
    char target[RUNTIMEC_HOST_KEY_CAPACITY];
    runtimec_status_t status;
    size_t base_length = 0U;
    size_t key_length = 0U;

    *output_exit = (binrwir_execution_exit_t){0};
    if (instruction->read_arguments.count != 2U ||
        instruction->write_arguments.count != 1U) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  VMBIN_HANDLER_CALL_DIAG_WRONG_ARITY,
                                  source_program_counter);
    }
    status = runtimec_host_resolve_base_path(
        frame, &instruction->read_arguments.data[0], base_path,
        sizeof(base_path), &base_length, output_exit, source_program_counter);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_host_resolve_key_text(
            frame, &instruction->read_arguments.data[1], key_text,
            sizeof(key_text), &key_length, output_exit, source_program_counter);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_host_resolve_join(
            base_path, base_length, key_text, key_length, target,
            sizeof(target), output_exit, source_program_counter);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    return runtimec_host_resolve_write(frame, instruction, target,
                                          output_exit, source_program_counter);
}

static vmbin_handler_call_diagnostic_t runtimec_host_key_tostr_diagnostic(
    int64_t code) {
    switch ((runtimec_member_key_text_diagnostic_t)code) {
    case RUNTIMEC_MEMBER_KEY_TEXT_DIAG_INSUFFICIENT_CAPACITY:
        return VMBIN_HANDLER_CALL_DIAG_INSUFFICIENT_CAPACITY;
    case RUNTIMEC_MEMBER_KEY_TEXT_DIAG_UNSUPPORTED_LANGTYPE:
        return VMBIN_HANDLER_CALL_DIAG_UNSUPPORTED_LANGTYPE;
    case RUNTIMEC_MEMBER_KEY_TEXT_DIAG_MALFORMED_BODY:
        return VMBIN_HANDLER_CALL_DIAG_MALFORMED_BODY;
    case RUNTIMEC_MEMBER_KEY_TEXT_DIAG_NON_FINITE:
        return VMBIN_HANDLER_CALL_DIAG_NON_FINITE;
    case RUNTIMEC_MEMBER_KEY_TEXT_DIAG_INVALID_REQUEST:
        return VMBIN_HANDLER_CALL_DIAG_WRONG_ARITY;
    }
    return VMBIN_HANDLER_CALL_DIAG_MALFORMED_BODY;
}

static runtimec_status_t runtimec_host_key_tostr_operand(
    binrwir_binvthread_frame_t *frame, const binrwir_argument_t *argument,
    runtimec_string_view_t *output_langtype, vmbin_value_location_t *output_value,
    binrwir_execution_exit_t *output_exit, uint32_t source_program_counter) {
    runtimec_error_t error;
    runtimec_status_t status;

    runtimec_error_reset(&error);
    
    status = vmbin_value_access_langtype(frame, argument, output_langtype,
                                         &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(
            output_exit, RUNTIMEC_STATUS_UNSUPPORTED,
            VMBIN_HANDLER_CALL_DIAG_UNSUPPORTED_LANGTYPE,
            source_program_counter);
    }
    
    status = vmbin_value_access_locate_body(frame, argument, output_value,
                                            &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, status,
                                  RUNTIMEC_HOST_DIAG_OPERAND_INVALID,
                                  source_program_counter);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t runtimec_host_key_tostr_fail(
    binrwir_execution_exit_t *output_exit, runtimec_status_t status,
    int64_t diagnostic, size_t required_length, uint32_t source_program_counter) {
    if (status == RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY &&
        required_length > VMBIN_HANDLER_KEY_TEXT_MAXIMUM) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_UNSUPPORTED,
                                  VMBIN_HANDLER_CALL_DIAG_KEY_TOO_LONG,
                                  source_program_counter);
    }
    return runtimec_host_fail(output_exit, status,
                              (uint32_t)runtimec_host_key_tostr_diagnostic(
                                  diagnostic),
                              source_program_counter);
}

static runtimec_status_t runtimec_host_key_tostr(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    char key_text[VMBIN_HANDLER_KEY_TEXT_MAXIMUM + 1U];
    runtimec_string_view_t langtype;
    vmbin_value_location_t value;
    runtimec_error_t error;
    runtimec_status_t status;
    size_t key_length = 0U;

    runtimec_error_reset(&error);
    *output_exit = (binrwir_execution_exit_t){0};
    
    if (instruction->read_arguments.count != 1U ||
        instruction->write_arguments.count != 1U) {
        return runtimec_host_fail(
            output_exit, RUNTIMEC_STATUS_INVALID,
            VMBIN_HANDLER_CALL_DIAG_WRONG_ARITY, source_program_counter);
    }
    status = runtimec_host_key_tostr_operand(
        frame, &instruction->read_arguments.data[0], &langtype, &value,
        output_exit, source_program_counter);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = runtimec_member_key_text(
        langtype, runtimec_bytes_view_make(value.read_bytes, value.size),
        key_text, sizeof(key_text), &key_length, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_key_tostr_fail(output_exit, status, error.code,
                                            key_length, source_program_counter);
    }
    
    return vmbin_handler_write_new_value(
        frame, instruction, (const uint8_t *)key_text, key_length, output_exit);
}

static const binrwir_registry_entry_t runtimec_host_entry_table[] = {
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_VTHREAD_PC,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = runtimec_host_vthread_pc},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_KEY_ADDRESS_OF,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = runtimec_host_key_address_of},
    
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_KEY_TOSTRING,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = runtimec_host_key_tostr},
    
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_PATHLANG_RESOLVE_POINTER,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = runtimec_host_pathlang_resolve},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_RANDOM_UNSIGNED_64,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_host_random_unsigned_64},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_RANDOM_SIGNED_63,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_host_random_signed_63},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_RANDOM_BELOW,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_host_random_below},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_VTHREAD_CREATE,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_host_vthread_not_implemented},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_VTHREAD_RUN,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_host_vthread_not_implemented},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_VTHREAD_CALL,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_host_vthread_not_implemented},
    
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_VTHREAD_SLEEP,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT | BINRWIR_REGISTRY_FLAG_MAY_YIELD,
     .handler = runtimec_host_vthread_sleep},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_VTHREAD_SET_STATUS,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_host_vthread_setstatus},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_PATHSPACE_DELETE,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_host_pathspace_delete},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_PATHSPACE_DELETE_TREE,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_host_pathspace_delete_tree},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_PATHSPACE_COPY,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_host_pathspace_copy},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_PATHSPACE_COPY_DIRECTORY,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_host_pathspace_copy_directory},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_PATHSPACE_LIST_LENGTH,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_host_pathspace_list_length},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_PATHSPACE_LIST_AT,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_host_pathspace_list_at},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_PATHSPACE_MAKE_INDEX,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_host_pathspace_make_index},
};

const binrwir_registry_entry_t *runtimec_host_handler_entries(
    size_t *output_count) {
    if (output_count == NULL) {
        return NULL;
    }
    *output_count =
        sizeof(runtimec_host_entry_table) / sizeof(runtimec_host_entry_table[0]);
    return runtimec_host_entry_table;
}
