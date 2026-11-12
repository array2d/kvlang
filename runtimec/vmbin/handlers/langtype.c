
#include "vmbin/handlers/langtype.h"

#include <string.h>

#include "binrwir/execution_exit.h"
#include "binrwir/binvthread_frame.h"
#include "binrwir/instruction.h"
#include "binrwir/operation_identifiers.h"
#include "contract/error.h"
#include "contract/program_counter.h"
#include "contract/status.h"
#include "contract/view.h"
#include "vmbin/value_access.h"

#define LANGTYPE_KIND_CAPACITY 256U

#define LANGTYPE_DIMENSION_LIMIT 8U

typedef enum langtype_diagnostic {
    LANGTYPE_DIAG_INVALID_REQUEST = 1,
    LANGTYPE_DIAG_UNPARSEABLE = 2,
    LANGTYPE_DIAG_WRITE_FAILED = 3
} langtype_diagnostic_t;

typedef struct langtype_parsed {
    runtimec_string_view_t kind;
    uint32_t dimension_count;
} langtype_parsed_t;

static runtimec_status_t langtype_fail(runtimec_error_t *error,
                                       runtimec_status_t status,
                                       langtype_diagnostic_t code,
                                       const char *message) {
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                       (int64_t)code,
                       runtimec_string_view_from_c_string(message),
                       RUNTIMEC_PROGRAM_COUNTER_NONE);
    return status;
}

static bool langtype_parse_dimension(const char *text, size_t size,
                                     uint32_t *output_value) {
    uint32_t value = 0U;
    size_t ordinal;

    if (size == 0U) {
        return false;
    }
    for (ordinal = 0U; ordinal < size; ++ordinal) {
        const char digit = text[ordinal];

        if (digit < '0' || digit > '9') {
            return false;
        }
        if (value > (UINT32_MAX - (uint32_t)(digit - '0')) / 10U) {
            return false;
        }
        value = value * 10U + (uint32_t)(digit - '0');
    }
    *output_value = value;
    return true;
}

static bool langtype_parse(runtimec_string_view_t text,
                           langtype_parsed_t *output_parsed) {
    size_t cursor = 0U;
    size_t kind_start;
    uint32_t dimension_count = 0U;

    output_parsed->kind = runtimec_string_view_make(NULL, 0U);
    output_parsed->dimension_count = 0U;
    if (text.data == NULL && text.size != 0U) {
        return false;
    }
    
    if (cursor < text.size &&
        (text.data[cursor] == '*' || text.data[cursor] == '@')) {
        cursor += 1U;
    }
    if (cursor >= text.size) {
        return false;
    }
    if (text.data[cursor] == '[') {
        const size_t close = cursor + 1U;
        size_t scan = close;

        while (scan < text.size && text.data[scan] != ']') {
            ++scan;
        }
        if (scan >= text.size) {
            return false;
        }
        if (scan == close) {
            
            dimension_count = 1U;
        } else {
            size_t element_start = close;

            while (element_start <= scan) {
                size_t element_end = element_start;
                uint32_t dimension = 0U;

                while (element_end < scan && text.data[element_end] != ',') {
                    ++element_end;
                }
                if (!langtype_parse_dimension(text.data + element_start,
                                              element_end - element_start,
                                              &dimension)) {
                    return false;
                }
                if (dimension_count >= LANGTYPE_DIMENSION_LIMIT) {
                    return false;
                }
                dimension_count += 1U;
                element_start = element_end + 1U;
            }
        }
        cursor = scan + 1U;
    }
    kind_start = cursor;
    while (cursor < text.size) {
        const char character = text.data[cursor];

        if (character == '|' || character == '?') {
            return false;
        }
        ++cursor;
    }
    if (cursor == kind_start) {
        return false;
    }
    output_parsed->kind =
        runtimec_string_view_make(text.data + kind_start, cursor - kind_start);
    output_parsed->dimension_count = dimension_count;
    return true;
}

static runtimec_string_view_t langtype_strip_declaration_prefix(
    runtimec_string_view_t kind) {
    static const char prefix[] = "def ";

    if (kind.size > sizeof(prefix) - 1U &&
        memcmp(kind.data, prefix, sizeof(prefix) - 1U) == 0) {
        return runtimec_string_view_make(kind.data + (sizeof(prefix) - 1U),
                                         kind.size - (sizeof(prefix) - 1U));
    }
    return kind;
}

static runtimec_status_t langtype_write(
    binrwir_binvthread_frame_t *frame, const binrwir_argument_t *argument,
    const void *bytes, size_t size, runtimec_error_t *error) {
    return vmbin_value_access_write_body(
        frame, argument, runtimec_bytes_view_make(bytes, size), error);
}

static runtimec_status_t langtype_execute(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, runtimec_error_t *error) {
    runtimec_string_view_t langtype = runtimec_string_view_make(NULL, 0U);
    langtype_parsed_t parsed;
    runtimec_string_view_t kind;
    uint64_t dimension_value;
    runtimec_status_t status;

    if (instruction->read_arguments.count != 1U ||
        instruction->write_arguments.count == 0U ||
        instruction->write_arguments.count > 2U) {
        return langtype_fail(
            error, RUNTIMEC_STATUS_INVALID, LANGTYPE_DIAG_INVALID_REQUEST,
            "parselangtype takes one read and one or two writes");
    }
    status = vmbin_value_access_langtype(
        frame, &instruction->read_arguments.data[0], &langtype, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (!langtype_parse(langtype, &parsed)) {
        return langtype_fail(
            error, RUNTIMEC_STATUS_INVALID, LANGTYPE_DIAG_UNPARSEABLE,
            "parselangtype received a kindexpr a value side cannot express");
    }
    kind = langtype_strip_declaration_prefix(parsed.kind);
    status = langtype_write(frame, &instruction->write_arguments.data[0],
                            kind.data, kind.size, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return langtype_fail(error, status, LANGTYPE_DIAG_WRITE_FAILED,
                             "parselangtype could not write the kind");
    }
    if (instruction->write_arguments.count > 1U) {
        dimension_value = (uint64_t)parsed.dimension_count;
        status = langtype_write(frame, &instruction->write_arguments.data[1],
                                &dimension_value, sizeof(dimension_value),
                                error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return langtype_fail(error, status, LANGTYPE_DIAG_WRITE_FAILED,
                                 "parselangtype could not write the ndim");
        }
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t runtimec_langtype_parse(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    runtimec_error_t error;

    (void)output_exit;
    runtimec_error_reset(&error);
    return langtype_execute(frame, instruction, &error);
}

static const binrwir_registry_entry_t langtype_entries[] = {
    
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_PARSE_LANGTYPE,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = runtimec_langtype_parse},
    
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_XVALUE_PARSE_LANGTYPE,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = runtimec_langtype_parse},
};

const binrwir_registry_entry_t *runtimec_langtype_handler_entries(
    size_t *output_count) {
    if (output_count == NULL) {
        return NULL;
    }
    *output_count = sizeof(langtype_entries) / sizeof(langtype_entries[0]);
    return langtype_entries;
}
