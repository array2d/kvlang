
#include "vmkv/local_types.h"

#include <stdio.h>
#include <string.h>

#include "contract/error.h"
#include "vmkv/parameter_type.h"
#include "vmkv/rwir.h"
#include "vmkv/rwir_manifest.h"
#include "vmkv/rwir.h"

enum {
    VMKV_LOCAL_TYPES_DIAG_INVALID_REQUEST = 40,
    VMKV_LOCAL_TYPES_DIAG_CONFLICT = 41,
    VMKV_LOCAL_TYPES_DIAG_NOT_WRITTEN = 42,
    VMKV_LOCAL_TYPES_DIAG_NOT_CONCRETE = 43,
    VMKV_LOCAL_TYPES_DIAG_OUT_OF_MEMORY = 44
};

#define VMKV_LOCAL_TYPES_MESSAGE_CAPACITY 192U
#define VMKV_LOCAL_TYPES_INITIAL_CAPACITY 8U

typedef struct vmkv_local_type_record {
    runtimec_string_view_t name;
    runtimec_string_view_t langtype;
    bool written;
} vmkv_local_type_record_t;

struct vmkv_local_types {
    runtimec_allocator_t allocator;
    vmkv_local_type_record_t *records;
    size_t count;
    size_t capacity;
    char message[VMKV_LOCAL_TYPES_MESSAGE_CAPACITY];
};

static runtimec_status_t vmkv_local_types_fail(runtimec_error_t *error,
                                               runtimec_status_t status,
                                               int64_t code,
                                               runtimec_string_view_t message,
                                               runtimec_program_counter_t logical_program_counter) {
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT, code,
                       message, logical_program_counter);
    return status;
}

static bool vmkv_local_types_view_equals(runtimec_string_view_t left,
                                        runtimec_string_view_t right) {
    return left.size == right.size &&
           (left.size == 0U || memcmp(left.data, right.data, left.size) == 0);
}

static bool vmkv_local_types_name_of(const vmkv_rwir_argument_view_t *argument,
                                     runtimec_string_view_t *output_name) {
    *output_name = runtimec_string_view_make(NULL, 0U);
    if (argument->literal.bytes.data != NULL ||
        argument->key_addressability != VMKV_KEY_ADDRESSABILITY_DYNAMIC ||
        argument->key.size == 0U || argument->key.data[0] == '*') {
        return false;
    }
    *output_name = runtimec_string_view_make(argument->key.data,
                                             argument->key.size);
    return true;
}

static vmkv_local_type_record_t *vmkv_local_types_record(
    vmkv_local_types_t *local_types, runtimec_string_view_t name,
    runtimec_program_counter_t logical_program_counter, runtimec_error_t *error) {
    for (size_t ordinal = 0U; ordinal < local_types->count; ++ordinal) {
        if (vmkv_local_types_view_equals(local_types->records[ordinal].name,
                                         name)) {
            return &local_types->records[ordinal];
        }
    }
    if (local_types->count == local_types->capacity) {
        const size_t capacity = local_types->capacity == 0U
                                    ? VMKV_LOCAL_TYPES_INITIAL_CAPACITY
                                    : local_types->capacity * 2U;
        vmkv_local_type_record_t *grown = runtimec_allocator_allocate(
            &local_types->allocator, capacity * sizeof(*grown),
            _Alignof(vmkv_local_type_record_t));
        if (grown == NULL) {
            vmkv_local_types_fail(
                error, RUNTIMEC_STATUS_OUT_OF_MEMORY,
                VMKV_LOCAL_TYPES_DIAG_OUT_OF_MEMORY,
                runtimec_string_view_from_c_string(
                    "vmkv local types: allocation failed"),
                logical_program_counter);
            return NULL;
        }
        if (local_types->records != NULL) {
            memcpy(grown, local_types->records,
                   local_types->count * sizeof(*grown));
            runtimec_allocator_deallocate(
                &local_types->allocator, local_types->records,
                local_types->capacity * sizeof(*grown),
                _Alignof(vmkv_local_type_record_t));
        }
        local_types->records = grown;
        local_types->capacity = capacity;
    }
    vmkv_local_type_record_t *record = &local_types->records[local_types->count];
    record->name = name;
    record->langtype = runtimec_string_view_make(NULL, 0U);
    record->written = false;
    local_types->count += 1U;
    return record;
}

static bool vmkv_local_types_is_concrete(runtimec_string_view_t langtype) {
    if (langtype.size == 0U || langtype.data == NULL ||
        memchr(langtype.data, '|', langtype.size) != NULL) {
        return false;
    }
    static const char rwir_kind[] = "rwir";
    static const char rwfunc_kind[] = "rwfunc";
    if (langtype.size == sizeof(rwir_kind) - 1U &&
        memcmp(langtype.data, rwir_kind, sizeof(rwir_kind) - 1U) == 0) {
        return false;
    }
    if (langtype.size == sizeof(rwfunc_kind) - 1U &&
        memcmp(langtype.data, rwfunc_kind, sizeof(rwfunc_kind) - 1U) == 0) {
        return false;
    }
    return true;
}

static runtimec_status_t vmkv_local_types_note_write(
    vmkv_local_types_t *local_types, runtimec_string_view_t name,
    runtimec_string_view_t langtype, bool *output_usable,
    runtimec_program_counter_t logical_program_counter, runtimec_error_t *error) {
    if (!vmkv_local_types_is_concrete(langtype)) {
        (void)snprintf(local_types->message, sizeof(local_types->message),
                       "vmkv local types: local '%.*s' is written with a "
                       "non-concrete langtype ('%.*s')",
                       (int)name.size, name.data, (int)langtype.size,
                       langtype.data != NULL ? langtype.data : "");
        *output_usable = false;
        return vmkv_local_types_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED,
            VMKV_LOCAL_TYPES_DIAG_NOT_CONCRETE,
            runtimec_string_view_from_c_string(local_types->message),
            logical_program_counter);
    }
    *output_usable = true;
    vmkv_local_type_record_t *record = vmkv_local_types_record(
        local_types, name, logical_program_counter, error);
    if (record == NULL) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    if (!record->written) {
        record->langtype = langtype;
        record->written = true;
        return RUNTIMEC_STATUS_SUCCESS;
    }
    if (vmkv_local_types_view_equals(record->langtype, langtype)) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    (void)snprintf(local_types->message, sizeof(local_types->message),
                   "vmkv local types: local '%.*s' is written as both '%.*s' "
                   "and '%.*s'",
                   (int)name.size, name.data, (int)record->langtype.size,
                   record->langtype.data, (int)langtype.size, langtype.data);
    *output_usable = false;
    return vmkv_local_types_fail(
        error, RUNTIMEC_STATUS_ERROR, VMKV_LOCAL_TYPES_DIAG_CONFLICT,
        runtimec_string_view_from_c_string(local_types->message),
        logical_program_counter);
}

static runtimec_program_counter_t vmkv_local_types_program_counter(
    uint32_t line) {
    return (runtimec_program_counter_t){
        RUNTIMEC_PROGRAM_COUNTER_STACK_IDENTIFIER_NONE, (int32_t)line};
}

runtimec_status_t vmkv_local_types_collect(
    vmkv_function_source_t *function, runtimec_allocator_t allocator,
    vmkv_local_types_t **output_local_types, runtimec_error_t *error) {
    if (output_local_types != NULL) {
        *output_local_types = NULL;
    }
    runtimec_error_reset(error);
    if (function == NULL || output_local_types == NULL || error == NULL ||
        !runtimec_allocator_is_valid(&allocator)) {
        return vmkv_local_types_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMKV_LOCAL_TYPES_DIAG_INVALID_REQUEST,
            runtimec_string_view_from_c_string(
                "vmkv local types: invalid collect request"),
            RUNTIMEC_PROGRAM_COUNTER_NONE);
    }
    vmkv_local_types_t *local_types = runtimec_allocator_allocate(
        &allocator, sizeof(*local_types), _Alignof(vmkv_local_types_t));
    if (local_types == NULL) {
        return vmkv_local_types_fail(
            error, RUNTIMEC_STATUS_OUT_OF_MEMORY,
            VMKV_LOCAL_TYPES_DIAG_OUT_OF_MEMORY,
            runtimec_string_view_from_c_string(
                "vmkv local types: allocation failed"),
            RUNTIMEC_PROGRAM_COUNTER_NONE);
    }
    memset(local_types, 0, sizeof(*local_types));
    local_types->allocator = allocator;

    vmkv_rwir_reader_t *reader = NULL;
    runtimec_status_t status = vmkv_rwir_reader_create(function, &reader, error);
    vmkv_rwir_read_result_t read_result = VMKV_RWIR_READ_RESULT_INVALID;
    vmkv_rwir_instruction_view_t instruction;
    while (status == RUNTIMEC_STATUS_SUCCESS) {
        status = vmkv_rwir_reader_next(reader, &read_result, &instruction, error);
        if (status != RUNTIMEC_STATUS_SUCCESS ||
            read_result != VMKV_RWIR_READ_RESULT_INSTRUCTION) {
            break;
        }
        const runtimec_program_counter_t logical_program_counter =
            vmkv_local_types_program_counter(instruction.logical_program_counter);
        const bool call_target = vmkv_rwir_manifest_is_call(
            instruction.operation_name);
        for (size_t ordinal = 0U; ordinal < instruction.read_argument_count;
             ++ordinal) {
            if (call_target && ordinal == 0U) {
                continue; 
            }
            runtimec_string_view_t name = runtimec_string_view_make(NULL, 0U);
            if (!vmkv_local_types_name_of(&instruction.read_arguments[ordinal],
                                          &name)) {
                continue;
            }
            if (vmkv_local_types_record(local_types, name,
                                        logical_program_counter,
                                        error) == NULL) {
                status = RUNTIMEC_STATUS_OUT_OF_MEMORY;
                break;
            }
        }
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            break;
        }
        for (size_t ordinal = 0U; ordinal < instruction.write_argument_count;
             ++ordinal) {
            runtimec_string_view_t name = runtimec_string_view_make(NULL, 0U);
            if (!vmkv_local_types_name_of(&instruction.write_arguments[ordinal],
                                          &name)) {
                continue;
            }
            bool usable = false;
            status = vmkv_local_types_note_write(
                local_types, name,
                instruction.write_arguments[ordinal].parameter_type_name,
                &usable, logical_program_counter, error);
            if (status != RUNTIMEC_STATUS_SUCCESS) {
                break;
            }
        }
    }
    if (reader != NULL) {
        vmkv_rwir_reader_destroy(reader);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        for (size_t ordinal = 0U; ordinal < local_types->count; ++ordinal) {
            const vmkv_local_type_record_t *record =
                &local_types->records[ordinal];
            if (record->written) {
                continue;
            }
            
            if (vmkv_rwir_key_is_member(
                    runtimec_key_view_make(record->name.data,
                                           record->name.size))) {
                continue;
            }
            (void)snprintf(local_types->message,
                           sizeof(local_types->message),
                           "vmkv local types: local '%.*s' is read but never "
                           "written",
                           (int)record->name.size, record->name.data);
            status = vmkv_local_types_fail(
                error, RUNTIMEC_STATUS_UNSUPPORTED,
                VMKV_LOCAL_TYPES_DIAG_NOT_WRITTEN,
                runtimec_string_view_from_c_string(local_types->message),
                RUNTIMEC_PROGRAM_COUNTER_NONE);
            break;
        }
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        (void)vmkv_local_types_destroy(local_types, allocator);
        return status;
    }
    *output_local_types = local_types;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmkv_local_types_destroy(vmkv_local_types_t *local_types,
                                           runtimec_allocator_t allocator) {
    if (local_types == NULL || !runtimec_allocator_is_valid(&allocator)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (local_types->records != NULL) {
        runtimec_allocator_deallocate(
            &allocator, local_types->records,
            local_types->capacity * sizeof(*local_types->records),
            _Alignof(vmkv_local_type_record_t));
    }
    runtimec_allocator_deallocate(&allocator, local_types,
                                  sizeof(*local_types),
                                  _Alignof(vmkv_local_types_t));
    return RUNTIMEC_STATUS_SUCCESS;
}

bool vmkv_local_types_find(const vmkv_local_types_t *local_types,
                           runtimec_string_view_t name,
                           runtimec_string_view_t *output_langtype) {
    if (output_langtype != NULL) {
        *output_langtype = runtimec_string_view_make(NULL, 0U);
    }
    if (local_types == NULL || output_langtype == NULL) {
        return false;
    }
    for (size_t ordinal = 0U; ordinal < local_types->count; ++ordinal) {
        const vmkv_local_type_record_t *record = &local_types->records[ordinal];
        if (!vmkv_local_types_view_equals(record->name, name) ||
            !record->written) {
            continue;
        }
        *output_langtype = record->langtype;
        return true;
    }
    return false;
}
