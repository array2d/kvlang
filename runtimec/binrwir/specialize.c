
#include "binrwir/specialize.h"

#include <string.h>

#include "binrwir/instruction.h"
#include "binrwir/operation_identifiers.h"

#define BINRWIR_SPECIALIZE_TUPLE_CAPACITY 256U
#define BINRWIR_SPECIALIZE_NAME_CAPACITY 32U

static runtimec_string_view_t binrwir_specialize_argument_type(
    const binrwir_binfunc_t *program, const binrwir_binfunc_view_t *view,
    const binrwir_argument_t *argument) {
    const runtimec_string_view_t empty = runtimec_string_view_make(NULL, 0U);
    runtimec_string_view_t name;
    runtimec_string_view_t langtype;

    switch (argument->reference_kind) {
    case BINRWIR_REFERENCE_LITERAL: {
        runtimec_bytes_view_t wire;

        if (binrwir_binfunc_get_literal(program, argument->payload, &wire,
                                        &langtype) == RUNTIMEC_STATUS_SUCCESS) {
            return langtype;
        }
        return empty;
    }
    case BINRWIR_REFERENCE_LOCAL:
    case BINRWIR_REFERENCE_STATIC:
        if (binrwir_binfunc_get_symbol(program, argument->slot, &name,
                                       &langtype) == RUNTIMEC_STATUS_SUCCESS) {
            return langtype;
        }
        return empty;
    case BINRWIR_REFERENCE_PARAMETER_READ:
        if (view->signature.read_parameter_types != NULL &&
            (size_t)argument->slot < view->signature.read_parameter_count) {
            return view->signature.read_parameter_types[argument->slot];
        }
        return empty;
    case BINRWIR_REFERENCE_PARAMETER_WRITE:
        if (view->signature.write_parameter_types != NULL &&
            (size_t)argument->slot < view->signature.write_parameter_count) {
            return view->signature.write_parameter_types[argument->slot];
        }
        return empty;
    default:
        return empty;
    }
}

static size_t binrwir_specialize_base_name(runtimec_string_view_t langtype,
                                           char *output, size_t capacity) {
    size_t begin = 0U;
    size_t end;

    if (!runtimec_string_view_is_valid(langtype) || langtype.size == 0U) {
        return 0U;
    }
    end = langtype.size;
    while (begin < end && langtype.data[begin] == '*') {
        begin += 1U;
    }
    if (begin < end && langtype.data[begin] == '[') {
        while (begin < end && langtype.data[begin] != ']') {
            begin += 1U;
        }
        if (begin < end) {
            begin += 1U;
        }
    }
    if (begin >= end || end - begin >= capacity) {
        return 0U;
    }
    memcpy(output, langtype.data + begin, end - begin);
    return end - begin;
}

static int binrwir_specialize_append(
    const binrwir_binfunc_t *program, const binrwir_binfunc_view_t *view,
    const binrwir_argument_t *argument, char *output, size_t capacity,
    size_t *used) {
    char base_name[BINRWIR_SPECIALIZE_NAME_CAPACITY];
    const runtimec_string_view_t type =
        binrwir_specialize_argument_type(program, view, argument);
    const size_t length = binrwir_specialize_base_name(
        type, base_name, sizeof(base_name));

    if (length == 0U) {
        return 0;
    }
    if (*used + (length + (*used ? 1U : 0U)) >= capacity) {
        return 0;
    }
    if (*used != 0U) {
        output[*used] = ',';
        *used += 1U;
    }
    memcpy(output + *used, base_name, length);
    *used += length;
    output[*used] = '\0';
    return 1;
}

static size_t binrwir_specialize_tuple(
    const binrwir_binfunc_t *program, const binrwir_binfunc_view_t *view,
    const binrwir_instruction_view_t *instruction, char *output,
    size_t capacity) {
    size_t used = 0U;
    size_t ordinal;

    output[0] = '\0';
    for (ordinal = 0U; ordinal < instruction->read_arguments.count;
         ++ordinal) {
        if (!binrwir_specialize_append(program, view,
                                       &instruction->read_arguments.data[ordinal],
                                       output, capacity, &used)) {
            return 0U;
        }
    }
    for (ordinal = 0U; ordinal < instruction->write_arguments.count;
         ++ordinal) {
        if (!binrwir_specialize_append(program, view,
                                       &instruction->write_arguments.data[ordinal],
                                       output, capacity, &used)) {
            return 0U;
        }
    }
    return used;
}

static int binrwir_specialize_applies(binrwir_operation_identifier_t base) {
    size_t low = 0U;
    size_t high = BINRWIR_SPECIALIZATION_TABLE_COUNT;

    while (low < high) {
        const size_t middle = low + (high - low) / 2U;

        if (binrwir_specializations[middle].base_operation_identifier < base) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    return low < BINRWIR_SPECIALIZATION_TABLE_COUNT &&
           binrwir_specializations[low].base_operation_identifier == base;
}

static int binrwir_specialize_lookup(binrwir_operation_identifier_t base,
                                     const char *tuple,
                                     binrwir_operation_identifier_t *output) {
    size_t ordinal;

    for (ordinal = 0U; ordinal < BINRWIR_SPECIALIZATION_TABLE_COUNT;
         ++ordinal) {
        const binrwir_specialization_t *entry = &binrwir_specializations[ordinal];

        if (entry->base_operation_identifier < base) {
            continue;
        }
        if (entry->base_operation_identifier > base) {
            return 0;
        }
        if (strcmp(entry->operand_types, tuple) != 0) {
            continue;
        }
        *output = entry->specialized_operation_identifier;
        return 1;
    }
    return 0;
}

static int binrwir_specialize_carry_symbols(
    const binrwir_binfunc_t *program, binrwir_binfunc_builder_t *builder,
    uint16_t *remap, size_t symbol_count) {
    size_t ordinal;

    for (ordinal = 0U; ordinal < symbol_count; ++ordinal) {
        runtimec_string_view_t name;
        runtimec_string_view_t langtype;
        uint16_t index = 0U;

        if (binrwir_binfunc_get_symbol(program, (uint16_t)ordinal, &name,
                                       &langtype) != RUNTIMEC_STATUS_SUCCESS ||
            binrwir_binfunc_builder_add_symbol(builder, name, langtype,
                                               &index) !=
                RUNTIMEC_STATUS_SUCCESS) {
            return 0;
        }
        remap[ordinal] = index;
    }
    return 1;
}

static int binrwir_specialize_carry_literals(
    const binrwir_binfunc_t *program, binrwir_binfunc_builder_t *builder,
    uint32_t *remap, size_t literal_count) {
    size_t ordinal;

    for (ordinal = 0U; ordinal < literal_count; ++ordinal) {
        runtimec_bytes_view_t wire;
        runtimec_string_view_t langtype;
        uint32_t index = BINRWIR_LITERAL_INDEX_NONE;

        if (binrwir_binfunc_get_literal(program, (uint32_t)ordinal, &wire,
                                        &langtype) != RUNTIMEC_STATUS_SUCCESS ||
            binrwir_binfunc_builder_add_literal(builder, wire, langtype,
                                                &index) !=
                RUNTIMEC_STATUS_SUCCESS) {
            return 0;
        }
        remap[ordinal] = index;
    }
    return 1;
}

static int binrwir_specialize_carry_parameters(
    const binrwir_binfunc_view_t *view, binrwir_binfunc_builder_t *builder) {
    size_t ordinal;

    for (ordinal = 0U; ordinal < view->signature.read_parameter_count;
         ++ordinal) {
        uint32_t index = 0U;

        if (binrwir_binfunc_builder_add_parameter(
                builder, BINRWIR_PARAMETER_DIRECTION_READ,
                view->signature.read_parameter_argument_indices[ordinal],
                view->signature.read_parameter_types[ordinal],
                &index) != RUNTIMEC_STATUS_SUCCESS) {
            return 0;
        }
    }
    for (ordinal = 0U; ordinal < view->signature.write_parameter_count;
         ++ordinal) {
        uint32_t index = 0U;

        if (binrwir_binfunc_builder_add_parameter(
                builder, BINRWIR_PARAMETER_DIRECTION_WRITE,
                view->signature.write_parameter_argument_indices[ordinal],
                view->signature.write_parameter_types[ordinal],
                &index) != RUNTIMEC_STATUS_SUCCESS) {
            return 0;
        }
    }
    for (ordinal = 0U; ordinal < view->signature.alias_rule_count; ++ordinal) {
        uint32_t index = 0U;

        if (binrwir_binfunc_builder_add_alias_rule(
                builder, &view->signature.alias_rules[ordinal],
                &index) != RUNTIMEC_STATUS_SUCCESS) {
            return 0;
        }
    }
    return 1;
}

static void binrwir_specialize_remap_argument(
    binrwir_argument_t *argument, const uint16_t *symbol_remap,
    size_t symbol_count) {
    switch (argument->reference_kind) {
    case BINRWIR_REFERENCE_LOCAL:
    case BINRWIR_REFERENCE_STATIC:
    case BINRWIR_REFERENCE_FUNCTION:
        if ((size_t)argument->slot < symbol_count) {
            argument->slot = symbol_remap[argument->slot];
        }
        break;
    default:
        break;
    }
}

typedef enum binrwir_specialize_diagnostic {
    
    BINRWIR_SPECIALIZE_DIAG_MISSING_OPERAND_TYPE = 1,
    BINRWIR_SPECIALIZE_DIAG_NO_SPECIALIZATION_ROW = 3,
    
    BINRWIR_SPECIALIZE_DIAG_OPERAND_TYPE_UNKNOWN = 4,
    BINRWIR_SPECIALIZE_DIAG_INSTRUCTION_UNREADABLE = 5,
    BINRWIR_SPECIALIZE_DIAG_ARGUMENT_COUNT_EXCEEDS_BUFFER = 6,
    BINRWIR_SPECIALIZE_DIAG_INSTRUCTION_REJECTED = 7,
    BINRWIR_SPECIALIZE_DIAG_COPY_ALLOCATION_FAILED = 8
} binrwir_specialize_diagnostic_t;

static void binrwir_specialize_release_copies(
    const runtimec_allocator_t *allocator, binrwir_argument_t *copies,
    size_t operand_count) {
    if (copies == NULL) {
        return;
    }
    (void)runtimec_allocator_deallocate(
        allocator, copies, sizeof(*copies) * operand_count,
        _Alignof(binrwir_argument_t));
}

static int binrwir_specialize_carry_instructions(
    const binrwir_binfunc_t *program, const binrwir_binfunc_view_t *view,
    binrwir_binfunc_builder_t *builder, const uint16_t *symbol_remap,
    size_t symbol_count, const runtimec_allocator_t *allocator,
    runtimec_error_t *error) {
    size_t ordinal;

    for (ordinal = 0U; ordinal < view->instruction_count; ++ordinal) {
        binrwir_instruction_view_t instruction;
        binrwir_instruction_build_request_t request;
        char tuple[BINRWIR_SPECIALIZE_TUPLE_CAPACITY];
        binrwir_operation_identifier_t specialized = 0U;
        uint32_t identifier = BINRWIR_IDENTIFIER_NONE;
        
        binrwir_argument_t *copies = NULL;
        
        size_t operand_count = 0U;
        binrwir_argument_span_t read_arguments;
        binrwir_argument_span_t write_arguments;

        if (binrwir_binfunc_get_instruction(program, (uint32_t)ordinal,
                                            &instruction) !=
            RUNTIMEC_STATUS_SUCCESS) {
            
            runtimec_error_set(
                error, RUNTIMEC_STATUS_INVALID,
                RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                (int64_t)BINRWIR_SPECIALIZE_DIAG_INSTRUCTION_UNREADABLE,
                runtimec_string_view_from_c_string(
                    "instruction is unreadable while carrying it into the "
                    "specialized program; the program view and the program "
                    "disagree (internal inconsistency)"),
                RUNTIMEC_PROGRAM_COUNTER_NONE);
            return 0;
        }
        
        if (!binrwir_specialize_applies(
                instruction.instruction->operation_identifier)) {
            specialized = instruction.instruction->operation_identifier;
        } else {
            const size_t tuple_size = binrwir_specialize_tuple(
                program, view, &instruction, tuple, sizeof(tuple));
        
        if (tuple_size == 0U) {
            
            runtimec_error_set(
                error, RUNTIMEC_STATUS_INVALID,
                RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                (int64_t)BINRWIR_SPECIALIZE_DIAG_OPERAND_TYPE_UNKNOWN,
                runtimec_string_view_from_c_string(
                    "operand type is unknown: an operand has no langtype or the "
                    "tuple exceeds its buffer; this is a producer (layout/vmkv) "
                    "problem, unrelated to the specialization table"),
                RUNTIMEC_PROGRAM_COUNTER_NONE);
            return 0;
        }
        if (!binrwir_specialize_lookup(
                instruction.instruction->operation_identifier, tuple,
                &specialized)) {
            
            static _Thread_local char message[512];
            static const char prefix[] =
                "no specialization row for operand types [";
            static const char middle[] = "] under base opid 0x";
            const size_t prefix_size = sizeof(prefix) - 1U;
            size_t used = 0U;
            size_t tuple_copied = tuple_size;

            memcpy(message, prefix, prefix_size);
            used = prefix_size;
            if (tuple_copied > sizeof(message) - used - 64U) {
                tuple_copied = sizeof(message) - used - 64U;
            }
            memcpy(message + used, tuple, tuple_copied);
            used += tuple_copied;
            memcpy(message + used, middle, sizeof(middle) - 1U);
            used += sizeof(middle) - 1U;
            {
                static const char digits[] = "0123456789abcdef";
                const unsigned base =
                    (unsigned)instruction.instruction->operation_identifier;
                int shift = 12;
                int wrote = 0;

                for (; shift >= 0; shift -= 4) {
                    message[used] = digits[(base >> shift) & 0xFU];
                    used += 1U;
                    wrote += 1;
                }
                (void)wrote;
            }
            {
                static const char tail[] =
                    "; add the row in specialization_table (generator/"
                    "catalog), not in the handler";
                size_t tail_size = sizeof(tail) - 1U;

                if (tail_size > sizeof(message) - used - 1U) {
                    tail_size = sizeof(message) - used - 1U;
                }
                memcpy(message + used, tail, tail_size);
                used += tail_size;
            }
            message[used] = '\0';
            runtimec_error_set(
                error, RUNTIMEC_STATUS_INVALID,
                RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                (int64_t)BINRWIR_SPECIALIZE_DIAG_NO_SPECIALIZATION_ROW,
                runtimec_string_view_make(message, used),
                RUNTIMEC_PROGRAM_COUNTER_NONE);
            return 0;
            }
        }
        
        {
            size_t copy_ordinal;

            operand_count = instruction.read_arguments.count +
                            instruction.write_arguments.count;

            if (operand_count != 0U) {
                copies = (binrwir_argument_t *)runtimec_allocator_allocate(
                    allocator, sizeof(*copies) * operand_count,
                    _Alignof(binrwir_argument_t));
                if (copies == NULL) {
                    runtimec_error_set(
                        error, RUNTIMEC_STATUS_OUT_OF_MEMORY,
                        RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                        (int64_t)BINRWIR_SPECIALIZE_DIAG_COPY_ALLOCATION_FAILED,
                        runtimec_string_view_from_c_string(
                            "cannot allocate the operand copies for one "
                            "instruction"),
                        RUNTIMEC_PROGRAM_COUNTER_NONE);
                    return 0;
                }
            }
            for (copy_ordinal = 0U;
                 copy_ordinal < instruction.read_arguments.count +
                                    instruction.write_arguments.count;
                 ++copy_ordinal) {
                const binrwir_argument_t *source =
                    copy_ordinal < instruction.read_arguments.count
                        ? &instruction.read_arguments.data[copy_ordinal]
                        : &instruction.write_arguments.data[
                              copy_ordinal - instruction.read_arguments.count];

                copies[copy_ordinal] = *source;
                binrwir_specialize_remap_argument(&copies[copy_ordinal],
                                                  symbol_remap, symbol_count);
            }
            read_arguments.data = copies;
            read_arguments.count = instruction.read_arguments.count;
            write_arguments.data =
                copies + instruction.read_arguments.count;
            write_arguments.count = instruction.write_arguments.count;
        }
        memset(&request, 0, sizeof(request));
        request.source_logical_program_counter =
            instruction.source_logical_program_counter;
        request.operation_identifier = specialized;
        request.read_arguments = read_arguments;
        request.write_arguments = write_arguments;
        if (binrwir_binfunc_builder_add_instruction(builder, &request,
                                                    &identifier) !=
            RUNTIMEC_STATUS_SUCCESS) {
            runtimec_error_set(
                error, RUNTIMEC_STATUS_INVALID,
                RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                (int64_t)BINRWIR_SPECIALIZE_DIAG_INSTRUCTION_REJECTED,
                runtimec_string_view_from_c_string(
                    "the specialized program rejected an instruction while "
                    "carrying it over; the instruction is malformed or its "
                    "arguments are inconsistent"),
                RUNTIMEC_PROGRAM_COUNTER_NONE);
            (void)binrwir_specialize_release_copies(allocator, copies,
                                                    operand_count);
            return 0;
        }
        
        (void)binrwir_specialize_release_copies(allocator, copies,
                                                operand_count);
    }
    return 1;
}

runtimec_status_t binrwir_binfunc_specialize(
    const binrwir_binfunc_t *program, const runtimec_allocator_t *allocator,
    binrwir_binfunc_t **output_program, runtimec_error_t *error) {
    binrwir_binfunc_view_t view;
    binrwir_binfunc_identity_t identity;
    binrwir_binfunc_builder_t *builder = NULL;
    binrwir_binfunc_t *specialized = NULL;
    size_t symbol_count = 0U;
    size_t literal_count = 0U;
    uint16_t *symbol_remap = NULL;
    uint32_t *literal_remap = NULL;
    runtimec_status_t status;

    runtimec_error_reset(error);
    if (output_program != NULL) {
        *output_program = NULL;
    }
    if (program == NULL || allocator == NULL || output_program == NULL ||
        !runtimec_allocator_is_valid(allocator) || error == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (binrwir_binfunc_validate(program) != RUNTIMEC_STATUS_SUCCESS) {
    }
    if (binrwir_binfunc_get_view(program, &view) != RUNTIMEC_STATUS_SUCCESS) {
    }
    if (binrwir_binfunc_get_identity(program, &identity) != RUNTIMEC_STATUS_SUCCESS) {
    }
    if (binrwir_binfunc_get_symbol_count(program, &symbol_count) != RUNTIMEC_STATUS_SUCCESS) {
    }
    if (binrwir_binfunc_get_literal_count(program, &literal_count) != RUNTIMEC_STATUS_SUCCESS) {
    }
    if (binrwir_binfunc_validate(program) != RUNTIMEC_STATUS_SUCCESS ||
        binrwir_binfunc_get_view(program, &view) != RUNTIMEC_STATUS_SUCCESS ||
        binrwir_binfunc_get_identity(program, &identity) !=
            RUNTIMEC_STATUS_SUCCESS ||
        binrwir_binfunc_get_symbol_count(program, &symbol_count) !=
            RUNTIMEC_STATUS_SUCCESS ||
        binrwir_binfunc_get_literal_count(program, &literal_count) !=
            RUNTIMEC_STATUS_SUCCESS) {
        return RUNTIMEC_STATUS_INVALID;
    }
    status = binrwir_binfunc_builder_create(allocator, &identity, &builder);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (symbol_count != 0U) {
        symbol_remap = (uint16_t *)runtimec_allocator_allocate(
            allocator, sizeof(*symbol_remap) * symbol_count,
            _Alignof(uint16_t));
        if (symbol_remap == NULL) {
            (void)binrwir_binfunc_builder_destroy(builder);
            return RUNTIMEC_STATUS_OUT_OF_MEMORY;
        }
    }
    if (literal_count != 0U) {
        literal_remap = (uint32_t *)runtimec_allocator_allocate(
            allocator, sizeof(*literal_remap) * literal_count,
            _Alignof(uint32_t));
        if (literal_remap == NULL) {
            (void)runtimec_allocator_deallocate(
                allocator, symbol_remap, sizeof(*symbol_remap) * symbol_count,
                _Alignof(uint16_t));
            (void)binrwir_binfunc_builder_destroy(builder);
            return RUNTIMEC_STATUS_OUT_OF_MEMORY;
        }
    }
    if (!binrwir_specialize_carry_parameters(&view, builder) ||
        !binrwir_specialize_carry_symbols(program, builder, symbol_remap,
                                          symbol_count) ||
        !binrwir_specialize_carry_literals(program, builder, literal_remap,
                                           literal_count) ||
        !binrwir_specialize_carry_instructions(program, &view, builder,
                                              symbol_remap, symbol_count,
                                              allocator, error)) {
        status = RUNTIMEC_STATUS_INVALID;
    } else {
        status = binrwir_binfunc_builder_finalize(builder, &specialized);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
        }
    }
    if (symbol_remap != NULL) {
        (void)runtimec_allocator_deallocate(
            allocator, symbol_remap, sizeof(*symbol_remap) * symbol_count,
            _Alignof(uint16_t));
    }
    if (literal_remap != NULL) {
        (void)runtimec_allocator_deallocate(
            allocator, literal_remap, sizeof(*literal_remap) * literal_count,
            _Alignof(uint32_t));
    }
    (void)binrwir_binfunc_builder_destroy(builder);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    *output_program = specialized;
    return RUNTIMEC_STATUS_SUCCESS;
}
