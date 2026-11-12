
#include "temp_output.h"
#include "contract/diagnostic_band.h"

#include <float.h>
#include <stdio.h>
#include <string.h>

#include "contract/char_codec.h"
#include "contract/float_format.h"

#include "binrwir/execution_exit.h"
#include "binrwir/binvthread_frame.h"
#include "binrwir/instruction.h"
#include "binrwir/binfunc.h"
#include "contract/status.h"
#include "vmbin/value_access.h"

#define RUNTIMEC_TEMP_OUTPUT_SCRATCH_CAPACITY 64U

typedef enum runtimec_temp_output_diagnostic {
    RUNTIMEC_TEMP_OUTPUT_DIAG_VALUE_ACCESS = RUNTIMEC_DIAGNOSTIC_BAND_TEMP_OUTPUT + 1U,
    RUNTIMEC_TEMP_OUTPUT_DIAG_UNSUPPORTED_TYPE = RUNTIMEC_DIAGNOSTIC_BAND_TEMP_OUTPUT + 2U,
    RUNTIMEC_TEMP_OUTPUT_DIAG_MALFORMED_VALUE = RUNTIMEC_DIAGNOSTIC_BAND_TEMP_OUTPUT + 3U
} runtimec_temp_output_diagnostic_t;

_Static_assert(
    RUNTIMEC_DIAGNOSTIC_IN_BAND(RUNTIMEC_TEMP_OUTPUT_DIAG_VALUE_ACCESS,
                                RUNTIMEC_DIAGNOSTIC_BAND_TEMP_OUTPUT),
    "temp_output diagnostics must stay in its own band");

static runtimec_status_t runtimec_temp_output_fail(
    binrwir_execution_exit_t *output_exit, runtimec_status_t status,
    runtimec_temp_output_diagnostic_t detail, uint32_t source_counter) {
    *output_exit = (binrwir_execution_exit_t){0};
    output_exit->kind = BINRWIR_EXECUTION_EXIT_ERROR;
    output_exit->status = status;
    output_exit->detail_code = (uint32_t)detail;
    output_exit->logical_program_counter = source_counter;
    output_exit->resume_logical_program_counter = source_counter;
    return status;
}

static runtimec_string_view_t runtimec_temp_output_langtype(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument) {
    runtimec_string_view_t langtype = runtimec_string_view_make(NULL, 0U);
    runtimec_error_t error;

    runtimec_error_reset(&error);
    if (vmbin_value_access_langtype(frame, argument, &langtype, &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_string_view_make(NULL, 0U);
    }
    return langtype;
}

static int runtimec_temp_output_type_is(runtimec_string_view_t langtype,
                                    const char *text) {
    const size_t size = strlen(text);
    size_t offset = 0U;

    if (langtype.data != NULL && langtype.size != 0U && langtype.data[0] == '[') {
        while (offset < langtype.size && langtype.data[offset] != ']') {
            ++offset;
        }
        if (offset < langtype.size) {
            ++offset; 
        }
    }
    return langtype.size - offset == size &&
           (size == 0U ||
            memcmp(langtype.data + offset, text, size) == 0);
}

static int runtimec_temp_output_render_float(double value, int is_float32,
                                              char *scratch, size_t capacity,
                                              size_t *output_length) {
    return runtimec_float_format_shortest(
               value,
               is_float32 != 0 ? RUNTIMEC_FLOAT_DOMAIN_FLOAT32
                               : RUNTIMEC_FLOAT_DOMAIN_FLOAT64,
               scratch, capacity, output_length)
           ? 1
           : 0;
}

typedef enum runtimec_temp_output_render_result {
    RUNTIMEC_TEMP_OUTPUT_RENDER_OK = 0,
    
    RUNTIMEC_TEMP_OUTPUT_RENDER_UNSUPPORTED_TYPE,
    
    RUNTIMEC_TEMP_OUTPUT_RENDER_MALFORMED,
} runtimec_temp_output_render_result_t;

typedef struct runtimec_temp_output_integer {
    size_t width;
    int is_signed;
} runtimec_temp_output_integer_t;

static int runtimec_temp_output_integer_of(runtimec_string_view_t langtype,
                                            runtimec_temp_output_integer_t *output) {
    static const struct {
        const char *name;
        size_t width;
        int is_signed;
    } table[] = {
        {"int8", 1U, 1},   {"int16", 2U, 1},  {"int32", 4U, 1},
        {"int64", 8U, 1},  {"uint8", 1U, 0},  {"uint16", 2U, 0},
        {"uint32", 4U, 0}, {"uint64", 8U, 0},
    };
    for (size_t ordinal = 0U; ordinal < sizeof(table) / sizeof(table[0]);
         ++ordinal) {
        if (runtimec_temp_output_type_is(langtype, table[ordinal].name)) {
            output->width = table[ordinal].width;
            output->is_signed = table[ordinal].is_signed;
            return 1;
        }
    }
    return 0;
}

static uint64_t runtimec_temp_output_load_unsigned(const uint8_t *body,
                                                    size_t width) {
    uint64_t value = 0U;

    for (size_t index = 0U; index < width; ++index) {
        value |= (uint64_t)body[index] << (8U * index);
    }
    return value;
}

static int runtimec_temp_output_render_integer(
    runtimec_string_view_t langtype, const uint8_t *body, size_t size,
    char *scratch, size_t capacity, size_t *output_length) {
    runtimec_temp_output_integer_t integer;
    int written;

    if (!runtimec_temp_output_integer_of(langtype, &integer) ||
        size != integer.width) {
        return 0;
    }
    const uint64_t raw = runtimec_temp_output_load_unsigned(body, integer.width);
    if (integer.is_signed != 0) {
        
        const unsigned shift = (unsigned)(64U - 8U * integer.width);
        const int64_t value = (int64_t)(raw << shift) >> shift;

        written = snprintf(scratch, capacity, "%lld", (long long)value);
    } else {
        written = snprintf(scratch, capacity, "%llu",
                           (unsigned long long)raw);
    }
    if (written <= 0 || (size_t)written >= capacity) {
        return 0;
    }
    *output_length = (size_t)written;
    return 1;
}

static int runtimec_temp_output_write_direct(const uint8_t *body, size_t size,
                                              FILE *stream) {
    if (size != 0U) {
        fwrite(body, 1U, size, stream);
    }
    return 1;
}

static int runtimec_temp_output_write_utf32(const uint8_t *body, size_t size,
                                             char *scratch, size_t capacity,
                                             FILE *stream) {
    size_t offset = 0U;

    if (size % 4U != 0U) {
        return 0;
    }
    while (offset < size) {
        
        size_t chunk = size - offset;
        size_t produced = 0U;

        const size_t maximum = (capacity > 1U ? capacity - 1U : 0U) / 4U * 4U;
        if (chunk > maximum) {
            chunk = maximum;
        }
        if (chunk == 0U) {
            return 0;
        }
        if (!runtimec_char_utf32_to_utf8(body + offset, chunk, scratch, capacity,
                                         &produced)) {
            return 0;
        }
        if (produced != 0U) {
            fwrite(scratch, 1U, produced, stream);
        }
        offset += chunk;
    }
    return 1;
}

static runtimec_temp_output_render_result_t runtimec_temp_output_render(
    runtimec_string_view_t langtype, const uint8_t *body, size_t size,
    char *scratch, size_t capacity, FILE *stream) {
    if (runtimec_temp_output_type_is(langtype, "char/utf8")) {
        if (!runtimec_temp_output_write_direct(body, size, stream)) {
            return RUNTIMEC_TEMP_OUTPUT_RENDER_MALFORMED;
        }
        return RUNTIMEC_TEMP_OUTPUT_RENDER_OK;
    }
    if (runtimec_temp_output_type_is(langtype, "char/utf32")) {
        if (!runtimec_temp_output_write_utf32(body, size, scratch, capacity,
                                               stream)) {
            return RUNTIMEC_TEMP_OUTPUT_RENDER_MALFORMED;
        }
        return RUNTIMEC_TEMP_OUTPUT_RENDER_OK;
    }
    if (runtimec_temp_output_type_is(langtype, "bool")) {
        static const char true_text[] = "true";
        static const char false_text[] = "false";
        const char *text;
        size_t length;

        if (size != 1U) {
            return RUNTIMEC_TEMP_OUTPUT_RENDER_MALFORMED;
        }
        text = body[0] != 0U ? true_text : false_text;
        length = body[0] != 0U ? sizeof(true_text) - 1U : sizeof(false_text) - 1U;
        if (length >= capacity) {
            return RUNTIMEC_TEMP_OUTPUT_RENDER_MALFORMED;
        }
        memcpy(scratch, text, length);
        fwrite(scratch, 1U, length, stream);
        return RUNTIMEC_TEMP_OUTPUT_RENDER_OK;
    }
    
    if (runtimec_temp_output_type_is(langtype, "float64")) {
        double value = 0.0;
        size_t length = 0U;

        if (size != sizeof(value)) {
            return RUNTIMEC_TEMP_OUTPUT_RENDER_MALFORMED;
        }
        memcpy(&value, body, sizeof(value));
        if (!runtimec_temp_output_render_float(value, 0, scratch, capacity,
                                                &length)) {
            return RUNTIMEC_TEMP_OUTPUT_RENDER_MALFORMED;
        }
        fwrite(scratch, 1U, length, stream);
        return RUNTIMEC_TEMP_OUTPUT_RENDER_OK;
    }
    if (runtimec_temp_output_type_is(langtype, "float32")) {
        float narrow = 0.0f;
        size_t length = 0U;

        if (size != sizeof(narrow)) {
            return RUNTIMEC_TEMP_OUTPUT_RENDER_MALFORMED;
        }
        memcpy(&narrow, body, sizeof(narrow));
        
        if (!runtimec_temp_output_render_float((double)narrow, 1, scratch,
                                                capacity, &length)) {
            return RUNTIMEC_TEMP_OUTPUT_RENDER_MALFORMED;
        }
        fwrite(scratch, 1U, length, stream);
        return RUNTIMEC_TEMP_OUTPUT_RENDER_OK;
    }
    {
        size_t length = 0U;

        if (runtimec_temp_output_integer_of(langtype, &(runtimec_temp_output_integer_t){0, 0})) {
            if (!runtimec_temp_output_render_integer(langtype, body, size,
                                                      scratch, capacity,
                                                      &length)) {
                return RUNTIMEC_TEMP_OUTPUT_RENDER_MALFORMED;
            }
            fwrite(scratch, 1U, length, stream);
            return RUNTIMEC_TEMP_OUTPUT_RENDER_OK;
        }
    }
    return RUNTIMEC_TEMP_OUTPUT_RENDER_UNSUPPORTED_TYPE;
}

runtimec_status_t runtimec_temp_output_run(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, const char *separator,
    int use_stderr, int trailing_newline,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_counter = instruction->source_logical_program_counter;
    FILE *stream = use_stderr != 0 ? stderr : stdout;
    runtimec_error_t error;

    runtimec_error_reset(&error);
    for (size_t ordinal = 0U; ordinal < instruction->read_arguments.count;
         ++ordinal) {
        const binrwir_argument_t *argument =
            &instruction->read_arguments.data[ordinal];
        const runtimec_string_view_t langtype =
            runtimec_temp_output_langtype(frame, argument);
        
        vmbin_value_location_t location;
        char scratch[RUNTIMEC_TEMP_OUTPUT_SCRATCH_CAPACITY];

        if (langtype.size == 0U) {
            
            return runtimec_temp_output_fail(
                output_exit, RUNTIMEC_STATUS_UNSUPPORTED,
                RUNTIMEC_TEMP_OUTPUT_DIAG_UNSUPPORTED_TYPE, source_counter);
        }
        const runtimec_status_t status =
            vmbin_value_access_locate_body(frame, argument, &location, &error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            
            return runtimec_temp_output_fail(
                output_exit, status, RUNTIMEC_TEMP_OUTPUT_DIAG_VALUE_ACCESS,
                source_counter);
        }
        if (ordinal != 0U && separator[0] != '\0') {
            fputs(separator, stream);
        }
        
        {
            uint8_t form = VMBIN_VALUE_ACCESS_FORM_VALUE;

            if (vmbin_value_access_form(frame, argument, &form, &error) ==
                    RUNTIMEC_STATUS_SUCCESS &&
                form == VMBIN_VALUE_ACCESS_FORM_POINTER &&
                argument->dereference == 0U) {
                if (location.size != 0U && location.read_bytes != NULL) {
                    fwrite(location.read_bytes, 1U, location.size, stream);
                }
                continue;
            }
        }
        switch (runtimec_temp_output_render(langtype, location.read_bytes,
                                             location.size, scratch,
                                             sizeof(scratch), stream)) {
            case RUNTIMEC_TEMP_OUTPUT_RENDER_OK:
                break;
            case RUNTIMEC_TEMP_OUTPUT_RENDER_UNSUPPORTED_TYPE:
                return runtimec_temp_output_fail(
                    output_exit, RUNTIMEC_STATUS_UNSUPPORTED,
                    RUNTIMEC_TEMP_OUTPUT_DIAG_UNSUPPORTED_TYPE, source_counter);
            default:
                
                return runtimec_temp_output_fail(
                    output_exit, RUNTIMEC_STATUS_ERROR,
                    RUNTIMEC_TEMP_OUTPUT_DIAG_MALFORMED_VALUE, source_counter);
        }
    }
    if (trailing_newline != 0) {
        fputc('\n', stream);
    }
    fflush(stream);
    *output_exit = (binrwir_execution_exit_t){0};
    return RUNTIMEC_STATUS_SUCCESS;
}

#ifdef RUNTIMEC_TEMP_OUTPUT

static runtimec_status_t runtimec_temp_print_handler(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return runtimec_temp_output_run(frame, instruction, "", 0, 0,
                                     output_exit);
}

static runtimec_status_t runtimec_temp_cerr_handler(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return runtimec_temp_output_run(frame, instruction, " ", 1, 1,
                                     output_exit);
}

static const binrwir_registry_entry_t runtimec_temp_output_entry_table[] = {
    {.operation_identifier = RUNTIMEC_TEMP_PRINT_OPERATION_IDENTIFIER,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_temp_print_handler},
    {.operation_identifier = RUNTIMEC_TEMP_CERR_OPERATION_IDENTIFIER,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_temp_cerr_handler},
};

const binrwir_registry_entry_t *runtimec_temp_output_entries(
    size_t *output_count) {
    if (output_count != NULL) {
        *output_count = sizeof(runtimec_temp_output_entry_table) /
                        sizeof(runtimec_temp_output_entry_table[0]);
    }
    return runtimec_temp_output_entry_table;
}

#endif 

