
#include "vmbin/handlers/container.h"

#include <stdio.h>
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
#include "contract/kvspace_interface.h"
#include <kvlang/xvalue.h>
#include <kvspace/kvspace_xvalue.h>
#include "vmbin/frame_target_key.h"
#include "vmbin/handlers/handler_call.h"
#include "vmbin/value_access.h"

#define CONTAINER_KEY_CAPACITY 512U

#define CONTAINER_NEW_BODY_CAPACITY 8192U

#define CONTAINER_SCALAR_CAPACITY 8U

runtimec_status_t runtimec_container_langtype_parse(
    runtimec_string_view_t langtype, runtimec_container_langtype_t *output) {
    static const struct {
        const char *text;
        uint32_t size;
    } widths[] = {
        {"int8", 1U},    {"uint8", 1U},   {"bool", 1U},
        {"int16", 2U},   {"uint16", 2U},  {"int32", 4U},
        {"uint32", 4U},  {"float32", 4U}, {"int64", 8U},
        {"uint64", 8U},  {"float64", 8U}, {"time", 8U},
        {"time/duration", 8U},
        {"char/utf8", 1U}, {"char/ascii", 1U}, {"char/utf32", 4U},
    };
    size_t offset = 0U;
    int32_t dimension_count = 0;

    output->base = runtimec_string_view_make(NULL, 0U);
    output->element_size = 0U;
    output->dimension_count = 0;
    memset(output->dimensions, 0, sizeof(output->dimensions));
    if (langtype.size == 0U) {
        return RUNTIMEC_STATUS_UNSUPPORTED;
    }
    
    while (offset < langtype.size && langtype.data[offset] == '[') {
        const char *closing = memchr(langtype.data + offset, ']',
                                     langtype.size - offset);
        size_t cursor = offset + 1U;

        if (closing == NULL) {
            return RUNTIMEC_STATUS_UNSUPPORTED;
        }
        
        if (closing == langtype.data + offset + 1U) {
            if (dimension_count >= VMBIN_HANDLER_MAX_INDEX_COUNT) {
                return RUNTIMEC_STATUS_UNSUPPORTED;
            }
            output->dimensions[dimension_count] = RUNTIMEC_CONTAINER_DIMENSION_UNBOUNDED;
            ++dimension_count;
            offset = (size_t)(closing - langtype.data) + 1U;
            continue;
        }
        while (cursor < (size_t)(closing - langtype.data)) {
            int64_t value = 0;
            int consumed = 0;

            while (cursor < (size_t)(closing - langtype.data)) {
                const char character = langtype.data[cursor];

                if (character < '0' || character > '9') {
                    break;
                }
                
                if (value > (INT64_MAX - (character - '0')) / 10) {
                    return RUNTIMEC_STATUS_UNSUPPORTED;
                }
                value = value * 10 + (character - '0');
                ++cursor;
                consumed = 1;
            }
            if (consumed == 0) {
                return RUNTIMEC_STATUS_UNSUPPORTED;
            }
            
            if (value <= 0 ||
                dimension_count >= VMBIN_HANDLER_MAX_INDEX_COUNT) {
                return RUNTIMEC_STATUS_UNSUPPORTED;
            }
            output->dimensions[dimension_count] = value;
            ++dimension_count;
            if (cursor < (size_t)(closing - langtype.data)) {
                if (langtype.data[cursor] != ',') {
                    return RUNTIMEC_STATUS_UNSUPPORTED;
                }
                ++cursor;
            }
        }
        offset = (size_t)(closing - langtype.data) + 1U;
    }
    if (dimension_count == 0 || offset >= langtype.size) {
        return RUNTIMEC_STATUS_UNSUPPORTED;
    }
    output->base = runtimec_string_view_make(langtype.data + offset,
                                             langtype.size - offset);
    output->dimension_count = dimension_count;
    for (size_t ordinal = 0U; ordinal < sizeof(widths) / sizeof(widths[0]);
         ++ordinal) {
        const size_t size = strlen(widths[ordinal].text);
        if (output->base.size == size &&
            memcmp(output->base.data, widths[ordinal].text, size) == 0) {
            output->element_size = widths[ordinal].size;
            return RUNTIMEC_STATUS_SUCCESS;
        }
    }
    return RUNTIMEC_STATUS_UNSUPPORTED;
}

static runtimec_status_t container_fail(
    binrwir_execution_exit_t *output_exit, runtimec_status_t status,
    uint32_t source_program_counter) {
    *output_exit = (binrwir_execution_exit_t){0};
    output_exit->kind = BINRWIR_EXECUTION_EXIT_ERROR;
    output_exit->status = status;
    output_exit->logical_program_counter = source_program_counter;
    output_exit->resume_logical_program_counter = source_program_counter;
    return status;
}

static runtimec_status_t container_fail_with_detail(
    binrwir_execution_exit_t *output_exit, runtimec_status_t status,
    uint32_t source_program_counter, runtimec_container_diagnostic_t detail) {
    runtimec_status_t returned =
        container_fail(output_exit, status, source_program_counter);

    output_exit->detail_code = (uint32_t)detail;
    return returned;
}

static const char *container_langtype_text(runtimec_string_view_t langtype,
                                           char *buffer, size_t capacity) {
    if (langtype.size == 0U || langtype.size + 1U > capacity) {
        return NULL;
    }
    memcpy(buffer, langtype.data, langtype.size);
    buffer[langtype.size] = '\0';
    return buffer;
}

static const char *container_langtype_text_from_argument(
    const vmbin_value_location_t *value, runtimec_string_view_t value_langtype,
    const uint8_t *bytes, size_t size, char *buffer, size_t capacity) {
    
    static const char utf32_kind[] = "char/utf32";
    const char *base = value_langtype.data;
    size_t base_size = value_langtype.size;
    if (base != NULL && base_size != 0U && base[0] == '[') {
        while (base_size != 0U && base[0] != ']') {
            ++base;
            --base_size;
        }
        if (base_size != 0U) {
            ++base;
            --base_size;
        }
    }
    const bool is_utf32 = base_size == sizeof(utf32_kind) - 1U &&
                          memcmp(base, utf32_kind, sizeof(utf32_kind) - 1U) == 0;
    (void)value;

    if (size == 0U) {
        return NULL;
    }
    if (is_utf32) {
        size_t used = 0U;
        
        if (!runtimec_char_utf32_to_utf8(bytes, size, buffer, capacity, &used)) {
            return NULL;
        }
        if (used + 1U > capacity) {
            return NULL;
        }
        buffer[used] = '\0';
        return buffer;
    }
    return container_langtype_text(
        runtimec_string_view_make((const char *)bytes, size), buffer, capacity);
}

static runtimec_status_t container_publish(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, const uint8_t *body,
    size_t body_size, binrwir_execution_exit_t *output_exit) {
    return vmbin_handler_write_new_value(frame, instruction, body, body_size,
                                         output_exit);
}

static int64_t container_pack_elements(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, size_t element_size,
    uint8_t *body, size_t capacity,
    runtimec_container_diagnostic_t *output_detail) {
    size_t body_size = 0U;
    runtimec_error_t error;

    *output_detail = RUNTIMEC_CONTAINER_DIAG_NONE;
    runtimec_error_reset(&error);
    for (size_t ordinal = 0U; ordinal < instruction->read_arguments.count;
         ++ordinal) {
        vmbin_value_location_t element;

        if (vmbin_value_access_locate_body(
                frame, &instruction->read_arguments.data[ordinal], &element,
                &error) != RUNTIMEC_STATUS_SUCCESS) {
            *output_detail = RUNTIMEC_CONTAINER_DIAG_UNPARSEABLE_LANGTYPE;
            return -1;
        }
        if (element.size != element_size) {
            *output_detail = RUNTIMEC_CONTAINER_DIAG_VALUE_WIDTH_MISMATCH;
            return -1;
        }
        if (body_size + element.size > capacity) {
            *output_detail = RUNTIMEC_CONTAINER_DIAG_NEW_BODY_EXCEEDS_BUFFER;
            return -1;
        }
        memcpy(body + body_size, element.read_bytes, element.size);
        body_size += element.size;
    }
    return (int64_t)body_size;
}

static void container_create_from_elements(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    runtimec_string_view_t result_langtype,
    binrwir_execution_exit_t *output_exit) {
    uint8_t body[CONTAINER_NEW_BODY_CAPACITY];
    runtimec_container_langtype_t declared_type;
    runtimec_container_diagnostic_t detail;
    int64_t body_size;

    if (runtimec_container_langtype_parse(result_langtype, &declared_type) !=
            RUNTIMEC_STATUS_SUCCESS ||
        declared_type.element_size == 0U) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_CONTAINER_DIAG_UNPARSEABLE_LANGTYPE,
                           output_exit->logical_program_counter);
        return;
    }
    body_size = container_pack_elements(frame, instruction,
                                        declared_type.element_size, body,
                                        sizeof(body), &detail);
    if (body_size < 0) {
        vmbin_handler_fail(output_exit,
                           detail == RUNTIMEC_CONTAINER_DIAG_NEW_BODY_EXCEEDS_BUFFER
                               ? RUNTIMEC_STATUS_OUT_OF_MEMORY
                               : RUNTIMEC_STATUS_INVALID,
                           detail, output_exit->logical_program_counter);
        return;
    }
    (void)container_publish(frame, instruction, body, (size_t)body_size,
                            output_exit);
}

static inline void container_new_build(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    runtimec_string_view_t result_langtype,
    binrwir_execution_exit_t *output_exit) {
    container_create_from_elements(frame, instruction, result_langtype, output_exit);
}

VMBIN_HANDLER_CREATE_VARIADIC(container_new_handler, container_new_build)

static int64_t container_slice_bounds(size_t body_size, size_t element_size,
                                      int64_t lo, int64_t hi,
                                      size_t *output_byte_length) {
    *output_byte_length = 0U;
    if (element_size == 0U || lo < 0 || hi < lo ||
        (uint64_t)hi > body_size / element_size) {
        return -1;
    }
    *output_byte_length = (size_t)(hi - lo) * element_size;
    return lo * (int64_t)element_size;
}

static void container_create_from_slice(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    vmbin_value_location_t source;
    vmbin_value_location_t bounds[2];
    runtimec_container_langtype_t type;
    runtimec_string_view_t source_langtype;
    int64_t lo = 0;
    int64_t hi = 0;
    size_t byte_length = 0U;
    int64_t offset;
    runtimec_error_t error;

    runtimec_error_reset(&error);
    if (vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[0], &source, &error) !=
            RUNTIMEC_STATUS_SUCCESS ||
        vmbin_value_access_langtype(
            frame, &instruction->read_arguments.data[0], &source_langtype,
            &error) != RUNTIMEC_STATUS_SUCCESS ||
        runtimec_container_langtype_parse(source_langtype, &type) !=
            RUNTIMEC_STATUS_SUCCESS ||
        vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[1], &bounds[0], &error) !=
            RUNTIMEC_STATUS_SUCCESS ||
        vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[2], &bounds[1], &error) !=
            RUNTIMEC_STATUS_SUCCESS ||
        bounds[0].size != sizeof(lo) || bounds[1].size != sizeof(hi)) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,
                           output_exit->logical_program_counter);
        return;
    }
    memcpy(&lo, bounds[0].read_bytes, sizeof(lo));
    memcpy(&hi, bounds[1].read_bytes, sizeof(hi));
    offset = container_slice_bounds(source.size, type.element_size, lo, hi,
                                    &byte_length);
    if (offset < 0) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_CONTAINER_DIAG_INDEX_OUT_OF_RANGE,
                           output_exit->logical_program_counter);
        return;
    }
    (void)container_publish(frame, instruction, source.read_bytes + offset,
                            byte_length, output_exit);
}

static inline void container_newslice_build(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    container_create_from_slice(frame, instruction, output_exit);
}

VMBIN_HANDLER_CREATE_TERNARY(container_newslice_handler, container_newslice_build)

int64_t runtimec_container_shape_numel(runtimec_string_view_t langtype) {
    int64_t total = 1;
    size_t offset = 1U;

    if (langtype.size < 3U || langtype.data[0] != '[') {
        return -1;
    }
    while (offset < langtype.size && langtype.data[offset] != ']') {
        int64_t value = 0;

        if (langtype.data[offset] < '0' || langtype.data[offset] > '9') {
            return -1;
        }
        while (offset < langtype.size && langtype.data[offset] >= '0' &&
               langtype.data[offset] <= '9') {
            value = value * 10 + (langtype.data[offset] - '0');
            ++offset;
        }
        total *= value;
        if (offset < langtype.size && langtype.data[offset] == ',') {
            ++offset;
        }
    }
    if (offset >= langtype.size || langtype.data[offset] != ']') {
        return -1;
    }
    
    return offset + 1U < langtype.size ? total : -1;
}

static int64_t container_langtype_total_bytes(runtimec_string_view_t langtype) {
    runtimec_container_langtype_t type;
    const int64_t element_count = runtimec_container_shape_numel(langtype);

    if (element_count < 0 ||
        runtimec_container_langtype_parse(langtype, &type) !=
            RUNTIMEC_STATUS_SUCCESS ||
        type.element_size == 0U) {
        return -1;
    }
    return element_count * (int64_t)type.element_size;
}

static runtimec_status_t container_replace_langtype(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, int langtype_from_value,
    binrwir_execution_exit_t *output_exit) {
    vmbin_value_location_t source;
    runtimec_string_view_t replacement;
    runtimec_error_t error;
    runtimec_status_t status;
    char langtype_text[CONTAINER_KEY_CAPACITY];
    const char *text;
    const uint32_t counter = instruction->source_logical_program_counter;

    runtimec_error_reset(&error);
    if (instruction->write_arguments.count != 1U) {
        return container_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    status = vmbin_value_access_locate_body(
        frame, &instruction->read_arguments.data[0], &source, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return container_fail(output_exit, status, counter);
    }
    if (langtype_from_value != 0) {
        
        vmbin_value_location_t text_location;
        runtimec_string_view_t argument_type;

        status = vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[1], &text_location, &error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return container_fail(output_exit, status, counter);
        }
        
        if (vmbin_value_access_langtype(
                frame, &instruction->read_arguments.data[1], &argument_type,
                &error) != RUNTIMEC_STATUS_SUCCESS ||
            container_langtype_text_from_argument(
                &text_location, argument_type, text_location.read_bytes,
                text_location.size, langtype_text,
                sizeof(langtype_text)) == NULL) {
            return container_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
        }
        replacement = runtimec_string_view_make(langtype_text,
                                                strlen(langtype_text));
    } else {
        
        status = vmbin_value_access_langtype(
            frame, &instruction->write_arguments.data[0], &replacement, &error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return container_fail(output_exit, status, counter);
        }
    }
    text = container_langtype_text(replacement, langtype_text,
                                   sizeof(langtype_text));
    if (text == NULL) {
        return container_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    
    if (langtype_from_value == 0) {
        runtimec_string_view_t source_langtype;
        int64_t new_bytes;
        int64_t old_bytes;

        status = vmbin_value_access_langtype(
            frame, &instruction->read_arguments.data[0], &source_langtype, &error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return container_fail(output_exit, status, counter);
        }
        new_bytes = container_langtype_total_bytes(replacement);
        old_bytes = container_langtype_total_bytes(source_langtype);
        if (new_bytes < 0 || old_bytes < 0 ||
            (size_t)new_bytes != source.size || (size_t)old_bytes != source.size) {
            return container_fail_with_detail(
                output_exit, RUNTIMEC_STATUS_INVALID, counter,
                RUNTIMEC_CONTAINER_DIAG_RESHAPE_SIZE_MISMATCH);
        }
    } else {
        
        runtimec_container_langtype_t new_type;

        if (runtimec_container_langtype_parse(replacement, &new_type) !=
                RUNTIMEC_STATUS_SUCCESS ||
            new_type.element_size == 0U ||
            source.size % new_type.element_size != 0U) {
            return container_fail_with_detail(
                output_exit, RUNTIMEC_STATUS_INVALID, counter,
                RUNTIMEC_CONTAINER_DIAG_VALUE_WIDTH_MISMATCH);
        }
    }
    return container_publish(frame, instruction, source.read_bytes, source.size,
                             output_exit);
}

static int64_t container_fill_body(runtimec_string_view_t langtype,
                                   const uint8_t *filler, size_t filler_size,
                                   int has_filler, uint8_t *body,
                                   size_t capacity,
                                   runtimec_container_diagnostic_t *output_detail) {
    runtimec_container_langtype_t type;
    const int64_t element_count = runtimec_container_shape_numel(langtype);
    uint8_t element[CONTAINER_SCALAR_CAPACITY];

    *output_detail = RUNTIMEC_CONTAINER_DIAG_NONE;
    if (element_count < 0 ||
        runtimec_container_langtype_parse(langtype, &type) !=
            RUNTIMEC_STATUS_SUCCESS ||
        type.element_size == 0U) {
        *output_detail = RUNTIMEC_CONTAINER_DIAG_UNPARSEABLE_LANGTYPE;
        return -1;
    }
    if (has_filler != 0 && filler_size != type.element_size) {
        *output_detail = RUNTIMEC_CONTAINER_DIAG_VALUE_WIDTH_MISMATCH;
        return -1;
    }
    if ((uint64_t)element_count * type.element_size > capacity) {
        *output_detail = RUNTIMEC_CONTAINER_DIAG_NEW_BODY_EXCEEDS_BUFFER;
        return -1;
    }
    memset(element, 0, sizeof(element));
    if (has_filler != 0) {
        memcpy(element, filler, type.element_size);
    }
    for (int64_t ordinal = 0; ordinal < element_count; ++ordinal) {
        memcpy(body + (size_t)ordinal * type.element_size, element,
               type.element_size);
    }
    return element_count * (int64_t)type.element_size;
}

static void container_create_from_fill(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    vmbin_value_location_t langtype_value;
    vmbin_value_location_t filler;
    runtimec_string_view_t langtype_argument_type;
    runtimec_container_diagnostic_t detail;
    char langtype_text[CONTAINER_KEY_CAPACITY];
    uint8_t body[CONTAINER_NEW_BODY_CAPACITY];
    int64_t body_size;
    runtimec_error_t error;

    runtimec_error_reset(&error);
    if (vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[0], &langtype_value,
            &error) != RUNTIMEC_STATUS_SUCCESS) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,
                           output_exit->logical_program_counter);
        return;
    }
    memset(&filler, 0, sizeof(filler));
    if (instruction->read_arguments.count == 2U &&
        vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[1], &filler,
            &error) != RUNTIMEC_STATUS_SUCCESS) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,
                           output_exit->logical_program_counter);
        return;
    }
    
    if (vmbin_value_access_langtype(
            frame, &instruction->read_arguments.data[0], &langtype_argument_type,
            &error) != RUNTIMEC_STATUS_SUCCESS ||
        container_langtype_text_from_argument(
            &langtype_value, langtype_argument_type, langtype_value.read_bytes,
            langtype_value.size, langtype_text, sizeof(langtype_text)) == NULL) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_CONTAINER_DIAG_UNPARSEABLE_LANGTYPE,
                           output_exit->logical_program_counter);
        return;
    }
    body_size = container_fill_body(
        runtimec_string_view_make(langtype_text, strlen(langtype_text)),
        filler.read_bytes, filler.size,
        instruction->read_arguments.count == 2U ? 1 : 0, body, sizeof(body),
        &detail);
    if (body_size < 0) {
        vmbin_handler_fail(output_exit,
                           detail == RUNTIMEC_CONTAINER_DIAG_NEW_BODY_EXCEEDS_BUFFER
                               ? RUNTIMEC_STATUS_OUT_OF_MEMORY
                               : RUNTIMEC_STATUS_INVALID,
                           detail, output_exit->logical_program_counter);
        return;
    }
    (void)container_publish(frame, instruction, body, (size_t)body_size,
                            output_exit);
}

static inline void container_fill_build(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    runtimec_string_view_t result_langtype,
    binrwir_execution_exit_t *output_exit) {
    (void)result_langtype; 
    container_create_from_fill(frame, instruction, output_exit);
}

VMBIN_HANDLER_CREATE_VARIADIC(container_fill_handler, container_fill_build)

static void container_element_count(size_t body_size,
                                    runtimec_string_view_t langtype,
                                    int64_t *out,
                                    binrwir_execution_exit_t *output_exit) {
    runtimec_container_langtype_t type;

    if (runtimec_container_langtype_parse(langtype, &type) !=
            RUNTIMEC_STATUS_SUCCESS ||
        type.element_size == 0U) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_CONTAINER_DIAG_UNPARSEABLE_LANGTYPE,
                           output_exit->logical_program_counter);
        return;
    }
    *out = (int64_t)(body_size / type.element_size);
}

static inline void container_numel_compute(const uint8_t *body, size_t size,
                                           runtimec_string_view_t langtype,
                                           int64_t *out,
                                           binrwir_execution_exit_t *output_exit) {
    (void)body;
    container_element_count(size, langtype, out, output_exit);
}

VMBIN_HANDLER_UNARY_ARRAY(container_numel_handler, container_numel_compute,
                           int64_t)

static inline bool container_parse_or_fail(runtimec_string_view_t langtype,
                                           runtimec_container_langtype_t *type,
                                           binrwir_execution_exit_t *output_exit) {
    if (runtimec_container_langtype_parse(langtype, type) ==
        RUNTIMEC_STATUS_SUCCESS) {
        return true;
    }
    vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                       RUNTIMEC_CONTAINER_DIAG_UNPARSEABLE_LANGTYPE,
                       output_exit->logical_program_counter);
    return false;
}

static inline void container_dim_compute(const uint8_t *body, size_t size,
                                         runtimec_string_view_t langtype,
                                         int64_t *out,
                                         binrwir_execution_exit_t *output_exit) {
    runtimec_container_langtype_t type;

    (void)body;
    (void)size;
    if (container_parse_or_fail(langtype, &type, output_exit)) {
        *out = (int64_t)type.dimension_count;
    }
}

VMBIN_HANDLER_UNARY_ARRAY(container_dim_handler, container_dim_compute, int64_t)

static inline void container_bodylen_compute(const uint8_t *body, size_t size,
                                             runtimec_string_view_t langtype,
                                             int64_t *out,
                                             binrwir_execution_exit_t *output_exit) {
    (void)body;
    (void)langtype;
    (void)output_exit;
    *out = (int64_t)size;
}

VMBIN_HANDLER_UNARY_ARRAY(container_bodylen_handler, container_bodylen_compute,
                           int64_t)

static runtimec_status_t container_finish(runtimec_status_t status,
                                          binrwir_execution_exit_t *output_exit,
                                          uint32_t source_program_counter) {
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    vmbin_handler_fail(output_exit, status,
                       RUNTIMEC_CONTAINER_DIAG_VALUE_WIDTH_MISMATCH,
                       source_program_counter);
    return RUNTIMEC_STATUS_ERROR;
}

static runtimec_status_t container_langtype_transfer(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction) {
    runtimec_string_view_t langtype;
    runtimec_error_t error;

    runtimec_error_reset(&error);
    const runtimec_status_t status = vmbin_value_access_langtype(
        frame, &instruction->read_arguments.data[0], &langtype, &error);
    return status == RUNTIMEC_STATUS_SUCCESS
               ? vmbin_value_access_write_body(
                     frame, &instruction->write_arguments.data[0],
                     runtimec_bytes_view_make(langtype.data, langtype.size),
                     &error)
               : status;
}

static bool container_unary_arity(const binrwir_instruction_view_t *instruction,
                                  binrwir_execution_exit_t *output_exit) {
    if (instruction->read_arguments.count == 1U &&
        instruction->write_arguments.count == 1U) {
        return true;
    }
    vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                       VMBIN_HANDLER_CALL_DIAG_WRONG_ARITY,
                       instruction->source_logical_program_counter);
    return false;
}

static bool container_target_key(binrwir_binvthread_frame_t *frame,
                                 const binrwir_instruction_view_t *instruction,
                                 char *buffer, size_t capacity,
                                 binrwir_execution_exit_t *output_exit) {
    runtimec_error_t error;

    runtimec_error_reset(&error);
    if (vmbin_value_access_argument_key(
            frame, &instruction->write_arguments.data[0], buffer, capacity,
            &error) == RUNTIMEC_STATUS_SUCCESS) {
        return true;
    }
    
    vmbin_handler_fail(output_exit,
                       error.status != RUNTIMEC_STATUS_SUCCESS
                           ? error.status
                           : RUNTIMEC_STATUS_INVALID,
                       RUNTIMEC_CONTAINER_DIAG_TARGET_KEY_UNRESOLVED,
                       instruction->source_logical_program_counter);
    return false;
}

static runtimec_status_t container_langtype_handler(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    if (!container_unary_arity(instruction, output_exit)) {
        return RUNTIMEC_STATUS_ERROR;
    }
    return container_finish(container_langtype_transfer(frame, instruction),
                            output_exit,
                            instruction->source_logical_program_counter);
}

static void container_element_offset(
    size_t body_size, runtimec_string_view_t langtype, const int64_t *indices,
    size_t index_count, runtimec_container_langtype_t *output_type,
    int64_t *output_linear, binrwir_execution_exit_t *output_exit) {
    int64_t total = 1;
    int64_t linear = 0;
    size_t dimension;

    *output_linear = 0;
    if (runtimec_container_langtype_parse(langtype, output_type) !=
            RUNTIMEC_STATUS_SUCCESS ||
        output_type->element_size == 0U) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_CONTAINER_DIAG_UNPARSEABLE_LANGTYPE,
                           output_exit->logical_program_counter);
        return;
    }
    if ((int32_t)index_count != output_type->dimension_count) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_CONTAINER_DIAG_INDEX_OUT_OF_RANGE,
                           output_exit->logical_program_counter);
        return;
    }
    
    if (output_type->dimensions[0] == RUNTIMEC_CONTAINER_DIMENSION_UNBOUNDED) {
        if (index_count != 1U) {
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                               RUNTIMEC_CONTAINER_DIAG_INDEX_OUT_OF_RANGE,
                               output_exit->logical_program_counter);
            return;
        }
        if (body_size % output_type->element_size != 0U) {
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                               RUNTIMEC_CONTAINER_DIAG_RESHAPE_SIZE_MISMATCH,
                               output_exit->logical_program_counter);
            return;
        }
        total = (int64_t)(body_size / output_type->element_size);
        for (dimension = 1U; dimension < index_count; ++dimension) {
            if (output_type->dimensions[dimension] <= 0) {
                vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                   RUNTIMEC_CONTAINER_DIAG_UNPARSEABLE_LANGTYPE,
                                   output_exit->logical_program_counter);
                return;
            }
        }
    } else {
        for (dimension = 0U; dimension < index_count; ++dimension) {
            if (output_type->dimensions[dimension] <= 0) {
                vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                   RUNTIMEC_CONTAINER_DIAG_UNPARSEABLE_LANGTYPE,
                                   output_exit->logical_program_counter);
                return;
            }
            
            if (total > INT64_MAX / output_type->dimensions[dimension]) {
                vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                   RUNTIMEC_CONTAINER_DIAG_UNPARSEABLE_LANGTYPE,
                                   output_exit->logical_program_counter);
                return;
            }
            total *= output_type->dimensions[dimension];
        }
    }
    if (total != (int64_t)(body_size / output_type->element_size) ||
        body_size % output_type->element_size != 0U) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_CONTAINER_DIAG_RESHAPE_SIZE_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    for (dimension = 0U; dimension < index_count; ++dimension) {
        
        const int64_t bound = dimension == 0U
                                  ? total
                                  : output_type->dimensions[dimension];

        if (indices[dimension] < 0 || indices[dimension] >= bound) {
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                               RUNTIMEC_CONTAINER_DIAG_INDEX_OUT_OF_RANGE,
                               output_exit->logical_program_counter);
            return;
        }
        linear = linear * bound + indices[dimension];
    }
    *output_linear = linear;
}

static inline bool container_element_position(
    size_t body_size, runtimec_string_view_t langtype, const int64_t *indices,
    size_t index_count, runtimec_container_langtype_t *output_type,
    int64_t *output_linear, binrwir_execution_exit_t *output_exit) {
    container_element_offset(body_size, langtype, indices, index_count,
                             output_type, output_linear, output_exit);
    return output_exit->kind != BINRWIR_EXECUTION_EXIT_ERROR;
}

static void container_store_element(uint8_t *target, int64_t linear,
                                    uint32_t element_size, const uint8_t *value,
                                    size_t value_size,
                                    binrwir_execution_exit_t *output_exit) {
    if (value_size != element_size) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_CONTAINER_DIAG_VALUE_WIDTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    memcpy(target + (size_t)linear * element_size, value, value_size);
}

static void container_set_multi_compute(const int64_t *indices,
                                        size_t index_count,
                                        const uint8_t *value,
                                        size_t value_size, uint8_t *target,
                                        size_t target_size,
                                        runtimec_string_view_t langtype,
                                        binrwir_execution_exit_t *output_exit) {
    runtimec_container_langtype_t type;
    int64_t linear = 0;

    if (container_element_position(target_size, langtype, indices, index_count, &type, &linear, output_exit))
        container_store_element(target, linear, type.element_size, value, value_size, output_exit);
}

static void container_load_element(const uint8_t *body, int64_t linear,
                                   uint32_t element_size, uint8_t *element,
                                   size_t element_capacity,
                                   binrwir_execution_exit_t *output_exit) {
    if (element_size > element_capacity) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                           RUNTIMEC_CONTAINER_DIAG_VALUE_WIDTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    memcpy(element, body + (size_t)linear * element_size, element_size);
}

static void container_at_multi_compute(const int64_t *indices,
                                       size_t index_count, uint8_t *element,
                                       size_t element_capacity,
                                       const uint8_t *body, size_t body_size,
                                       runtimec_string_view_t langtype,
                                       binrwir_execution_exit_t *output_exit) {
    runtimec_container_langtype_t type;
    int64_t linear = 0;

    if (container_element_position(body_size, langtype, indices, index_count, &type, &linear, output_exit))
        container_load_element(body, linear, type.element_size, element, element_capacity, output_exit);
}

VMBIN_HANDLER_ARRAY_INDEX(container_at_handler, container_at_multi_compute,
                             int64_t)

VMBIN_HANDLER_ARRAY_INDEX_WRITE(container_set_handler,
                                container_set_multi_compute, int64_t)

static runtimec_status_t container_newreshape_handler(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    if (instruction->read_arguments.count < 1U) {
        VMBIN_HANDLER_REQUIRE_ARITY_FAIL_(output_exit, instruction);
    }
    return container_replace_langtype(frame, instruction, 0, output_exit);
}

static void container_newreinterpret_build(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    const vmbin_value_location_t *source,
    const runtimec_string_view_t *source_langtype,
    const vmbin_value_location_t *langtype_value,
    binrwir_execution_exit_t *output_exit) {
    (void)source;
    (void)source_langtype;
    (void)langtype_value;
    (void)container_replace_langtype(frame, instruction, 1, output_exit);
}
VMBIN_HANDLER_CREATE_TWO(container_newreinterpret_handler,
                         container_newreinterpret_build)

static void container_write_or_fail(binrwir_binvthread_frame_t *frame,
                                    const binrwir_instruction_view_t *instruction,
                                    const vmbin_value_location_t *source,
                                    binrwir_execution_exit_t *output_exit,
                                    runtimec_error_t *error) {
    const runtimec_status_t status = vmbin_value_access_write_body(
        frame, &instruction->write_arguments.data[0],
        runtimec_bytes_view_make(source->read_bytes, source->size), error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        vmbin_handler_fail(output_exit, status,
                           RUNTIMEC_CONTAINER_DIAG_VALUE_WIDTH_MISMATCH,
                           instruction->source_logical_program_counter);
    }
}

static void container_copy_build(binrwir_binvthread_frame_t *frame,
                                 const binrwir_instruction_view_t *instruction,
                                 const vmbin_value_location_t *source,
                                 const runtimec_string_view_t *source_langtype,
                                 binrwir_execution_exit_t *output_exit) {
    runtimec_error_t error;

    (void)source_langtype;

    runtimec_error_reset(&error);
    
    container_write_or_fail(frame, instruction, source, output_exit, &error);
}

static const char container_member_separator[] = "\xC2\xB7";

static bool container_member_key(const char *slot_key, const char *suffix,
                                 size_t suffix_size, char *buffer,
                                 size_t capacity) {
    const size_t slot_size = strlen(slot_key);
    const size_t separator_size = sizeof(container_member_separator) - 1U;

    if (slot_size + separator_size + suffix_size + 1U > capacity) {
        return false;
    }
    memcpy(buffer, slot_key, slot_size);
    memcpy(buffer + slot_size, container_member_separator, separator_size);
    if (suffix_size != 0U) {
        memcpy(buffer + slot_size + separator_size, suffix, suffix_size);
    }
    buffer[slot_size + separator_size + suffix_size] = '\0';
    return true;
}

static bool container_argument_is_name(binrwir_binvthread_frame_t *frame,
                                       const binrwir_argument_t *argument) {
    runtimec_string_view_t declared;
    runtimec_error_t error;

    if (argument->reference_kind != BINRWIR_REFERENCE_LITERAL) {
        return false;
    }
    runtimec_error_reset(&error);
    if (vmbin_value_access_langtype(frame, argument, &declared, &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return false;
    }
    for (size_t ordinal = 0U; ordinal + 5U <= declared.size; ++ordinal) {
        if (memcmp(declared.data + ordinal, "char", 4U) == 0) {
            return true;
        }
    }
    return false;
}

static runtimec_status_t container_write_member(
    binrwir_binvthread_frame_t *frame, const char *slot_key,
    const char *suffix, size_t suffix_size,
    const binrwir_argument_t *value_argument, runtimec_error_t *error) {
    vmbin_value_location_t element;
    runtimec_string_view_t declared;
    char langtype_text[256];
    char member_key[VMBIN_HANDLER_KEY_CAPACITY];
    char message[256];
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    runtimec_status_t status;

    if (!container_member_key(slot_key, suffix, suffix_size, member_key,
                              sizeof(member_key))) {
        return RUNTIMEC_STATUS_INVALID;
    }
    status = vmbin_value_access_locate_body(frame, value_argument, &element,
                                             error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = vmbin_value_access_langtype(frame, value_argument, &declared,
                                          error);
    if (status != RUNTIMEC_STATUS_SUCCESS ||
        declared.size + 1U > sizeof(langtype_text)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    memcpy(langtype_text, declared.data, declared.size);
    langtype_text[declared.size] = '\0';
    if (kvlangXvalueHeadNewSlack(element.read_bytes, (uint64_t)element.size,
                                  langtype_text, &wire,
                                  &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        return RUNTIMEC_STATUS_ERROR;
    }
    message[0] = '\0';
    const bool wrote = runtimec_kvspace_write_wire(
        frame->kvspace_handle, member_key, wire, (uint32_t)wire_size, message,
        (uint32_t)sizeof(message));
    const int set_status = wrote ? 0 : 1;
    free(wire);
    return set_status == 0 ? RUNTIMEC_STATUS_SUCCESS : RUNTIMEC_STATUS_ERROR;
}

static bool container_index_container(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, char *buffer,
    size_t capacity, binrwir_execution_exit_t *output_exit) {
    char message[256];
    const size_t key_length = strlen(buffer);

    if (key_length + 2U > capacity) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_CONTAINER_DIAG_UNPARSEABLE_LANGTYPE,
                           instruction->source_logical_program_counter);
        return false;
    }
    buffer[key_length] = '/';
    buffer[key_length + 1U] = '\0';
    message[0] = '\0';
    const int mkindex_status = kvspaceMkindex(frame->kvspace_handle, buffer,
                                              message, (uint32_t)sizeof(message));
    buffer[key_length] = '\0';
    if (mkindex_status != 0) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                           VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED,
                           instruction->source_logical_program_counter);
        return false;
    }
    return true;
}

bool runtimec_container_open_marker(binrwir_binvthread_frame_t *frame,
                                    const binrwir_instruction_view_t *instruction,
                                    char *buffer, size_t capacity,
                                    binrwir_execution_exit_t *output_exit) {
    char message[256];
    size_t key_length;

    if (!container_target_key(frame, instruction, buffer, capacity,
                              output_exit) ||
        vmbin_handler_write_new_value(frame, instruction, NULL, 0U,
                                      output_exit) != RUNTIMEC_STATUS_SUCCESS) {
        return false;
    }
    (void)message;
    (void)key_length;
    return container_index_container(frame, instruction, buffer, capacity,
                                     output_exit);
}

static void container_write_positional_members(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, const char *slot_key,
    binrwir_execution_exit_t *output_exit) {
    runtimec_error_t error;

    for (size_t ordinal = 0U; ordinal < instruction->read_arguments.count;
         ++ordinal) {
        char suffix[24];
        const int suffix_size = snprintf(suffix, sizeof(suffix), "[%zu]", ordinal);

        runtimec_error_reset(&error);
        if (container_argument_is_name(
                frame, &instruction->read_arguments.data[ordinal]) ||
            suffix_size <= 0 || (size_t)suffix_size >= sizeof(suffix) ||
            container_write_member(
                frame, slot_key, suffix, (size_t)suffix_size,
                &instruction->read_arguments.data[ordinal],
                &error) != RUNTIMEC_STATUS_SUCCESS) {
            vmbin_handler_fail(output_exit,
                               suffix_size <= 0
                                   ? RUNTIMEC_STATUS_INVALID
                                   : RUNTIMEC_STATUS_UNSUPPORTED,
                               RUNTIMEC_CONTAINER_DIAG_VALUE_WIDTH_MISMATCH,
                               instruction->source_logical_program_counter);
            return;
        }
    }
}

static void container_create_variadic(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    char slot_key[VMBIN_HANDLER_KEY_CAPACITY];

    if (runtimec_container_open_marker(frame, instruction, slot_key, sizeof(slot_key),
                              output_exit)) {
        container_write_positional_members(frame, instruction, slot_key,
                                           output_exit);
    }
}

static void container_map_new_build(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    runtimec_string_view_t result_langtype,
    binrwir_execution_exit_t *output_exit) {
    (void)result_langtype;
    
    container_create_variadic(frame, instruction, output_exit);
}

VMBIN_HANDLER_CREATE_VARIADIC(container_map_new_handler, container_map_new_build)

static bool container_literal_path(binrwir_binvthread_frame_t *frame,
                                   const binrwir_argument_t *argument,
                                   char *buffer, size_t capacity,
                                   size_t *output_size) {
    
    runtimec_bytes_buffer_t bytes = {0};
    runtimec_string_view_t declared;
    runtimec_error_t error;
    bool ok = false;

    bool owned = false;

    runtimec_bytes_buffer_reset(&bytes);
    runtimec_error_reset(&error);
    if (vmbin_value_access_read_body(frame, argument, &bytes, &error) ==
        RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY) {
        bytes.data = (uint8_t *)malloc(bytes.size != 0U ? bytes.size : 1U);
        if (bytes.data == NULL) {
            return false;
        }
        owned = true;
        bytes.capacity = bytes.size;
        bytes.size = 0U;
        runtimec_error_reset(&error);
        if (vmbin_value_access_read_body(frame, argument, &bytes, &error) !=
            RUNTIMEC_STATUS_SUCCESS) {
            free(bytes.data); 
            return false;
        }
    }
    if (!runtimec_bytes_buffer_is_valid(&bytes) ||
        vmbin_value_access_langtype(frame, argument, &declared, &error) !=
            RUNTIMEC_STATUS_SUCCESS) {
        if (owned) {
            free(bytes.data);
        }
        return false;
    }
    if (bytes.size != 0U) {
        bool utf32 = false;
        for (size_t ordinal = 1U; ordinal + 1U < declared.size; ++ordinal) {
            if (memcmp(declared.data + ordinal, "char/utf32", 10U) == 0) {
                utf32 = true;
                break;
            }
        }
        if (utf32) {
            const size_t characters = bytes.size / 4U;
            if (characters + 1U <= capacity) {
                for (size_t ordinal = 0U; ordinal < characters; ++ordinal) {
                    buffer[ordinal] = (char)bytes.data[ordinal * 4U];
                }
                buffer[characters] = '\0';
                *output_size = characters;
                ok = true;
            }
        } else if (bytes.size + 1U <= capacity) {
            memcpy(buffer, bytes.data, bytes.size);
            buffer[bytes.size] = '\0';
            *output_size = bytes.size;
            ok = true;
        }
    }
    if (owned) {
        free(bytes.data);
    }
    return ok;
}

static bool container_struct_clone(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, const char *slot_key,
    binrwir_execution_exit_t *output_exit) {
    char prototype_path[512];
    size_t prototype_size = 0U;
    char message[256];

    message[0] = '\0';
    if (!container_literal_path(frame, &instruction->read_arguments.data[0],
                                prototype_path, sizeof(prototype_path),
                                &prototype_size) ||
        prototype_size == 0U ||
        kvspaceCpdir(frame->kvspace_handle, prototype_path, slot_key, message,
                      (uint32_t)sizeof(message)) == 0) {
        return prototype_size != 0U;
    }
    char rollback[256];
    rollback[0] = '\0';
    (void)kvspaceDeltree(frame->kvspace_handle, slot_key, rollback,
                         (uint32_t)sizeof(rollback));
    vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                       RUNTIMEC_CONTAINER_DIAG_UNPARSEABLE_LANGTYPE,
                       instruction->source_logical_program_counter);
    return false;
}

static bool container_struct_fields(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, const char *slot_key,
    binrwir_execution_exit_t *output_exit) {
    for (size_t ordinal = 1U; ordinal + 1U < instruction->read_arguments.count;
         ordinal += 2U) {
        char field_name[128];
        size_t field_size = 0U;
        runtimec_error_t error;

        runtimec_error_reset(&error);
        if (!container_literal_path(
                frame, &instruction->read_arguments.data[ordinal], field_name,
                sizeof(field_name), &field_size) ||
            field_size == 0U ||
            container_write_member(
                frame, slot_key, field_name, field_size,
                &instruction->read_arguments.data[ordinal + 1U],
                &error) != RUNTIMEC_STATUS_SUCCESS) {
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                               RUNTIMEC_CONTAINER_DIAG_VALUE_WIDTH_MISMATCH,
                               instruction->source_logical_program_counter);
            return false;
        }
    }
    return true;
}

static void container_struct_create(binrwir_binvthread_frame_t *frame,
                                    const binrwir_instruction_view_t *instruction,
                                    binrwir_execution_exit_t *output_exit) {
    char slot_key[VMBIN_HANDLER_KEY_CAPACITY];

    if (instruction->read_arguments.count >= 1U &&
        container_target_key(frame, instruction, slot_key, sizeof(slot_key),
                             output_exit) &&
        container_struct_clone(frame, instruction, slot_key, output_exit) &&
        container_index_container(frame, instruction, slot_key, sizeof(slot_key),
                                  output_exit)) {
        runtimec_error_t error;

        (void)container_struct_fields(frame, instruction, slot_key, output_exit);
        
        runtimec_error_reset(&error);
        if (vmbin_value_access_bind_write_target(
                frame, &instruction->write_arguments.data[0],
                runtimec_key_view_make(slot_key, strlen(slot_key)),
                &error) != RUNTIMEC_STATUS_SUCCESS) {
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                               VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED,
                               instruction->source_logical_program_counter);
        }
    }
}

static void container_struct_new_build(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    runtimec_string_view_t result_langtype,
    binrwir_execution_exit_t *output_exit) {
    (void)result_langtype;
    container_struct_create(frame, instruction, output_exit);
}

VMBIN_HANDLER_CREATE_VARIADIC(container_struct_new_handler,
                              container_struct_new_build)

VMBIN_HANDLER_CREATE_ONE(container_copy_handler, container_copy_build)

static const binrwir_registry_entry_t runtimec_container_entry_table[] = {
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_MAP_NEW,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = container_map_new_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_STRUCT_NEW,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = container_struct_new_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_NEW,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = container_new_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_NUMEL,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = container_numel_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_AT,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = container_at_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_SET,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = container_set_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_FILL,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = container_fill_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_NEW_SLICE,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = container_newslice_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_NEW_RESHAPE,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = container_newreshape_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_NEW_REINTERPRET,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = container_newreinterpret_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_COPY,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = container_copy_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_DIMENSION,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = container_dim_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_LANGTYPE,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = container_langtype_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_BODY_LENGTH,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = container_bodylen_handler},
};

const binrwir_registry_entry_t *runtimec_container_handler_entries(
    size_t *output_count) {
    if (output_count == NULL) {
        return NULL;
    }
    *output_count = sizeof(runtimec_container_entry_table) /
                    sizeof(runtimec_container_entry_table[0]);
    return runtimec_container_entry_table;
}
