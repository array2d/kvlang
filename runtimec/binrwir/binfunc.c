
#include "binrwir/binfunc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "binrwir/execution_exit.h"
#include "contract/kvspace_access.h"

#include <kvspace/kvspace_xvalue.h>

#define BINRWIR_BINFUNC_INITIAL_CAPACITY 4U

typedef struct binrwir_binfunc_instruction_record {
    uint32_t source_logical_program_counter;
    uint32_t read_argument_offset;
    uint32_t read_argument_count;
    uint32_t write_argument_count;
} binrwir_binfunc_instruction_record_t;

typedef struct binrwir_binfunc_parameter_arrays {
    uint32_t *argument_indices;
    runtimec_string_view_t *types;
    size_t count;
    size_t capacity;
} binrwir_binfunc_parameter_arrays_t;

typedef struct binrwir_binfunc_instruction_arrays {
    binrwir_instruction_t *heads;
    binrwir_binfunc_instruction_record_t *records;
    size_t count;
    size_t capacity;
} binrwir_binfunc_instruction_arrays_t;

typedef struct binrwir_binfunc_literal_record {
    runtimec_bytes_view_t wire_bytes;
    runtimec_string_view_t langtype;
} binrwir_binfunc_literal_record_t;

typedef struct binrwir_binfunc_symbol_record {
    runtimec_string_view_t name;
    runtimec_string_view_t langtype;
} binrwir_binfunc_symbol_record_t;

struct binrwir_binfunc {
    runtimec_allocator_t allocator;
    binrwir_binfunc_identity_t identity;
    binrwir_binfunc_instruction_arrays_t instructions;
    binrwir_argument_t *arguments;
    size_t argument_count;
    binrwir_binfunc_parameter_arrays_t read_parameters;
    binrwir_binfunc_parameter_arrays_t write_parameters;
    binrwir_binfunc_symbol_record_t *symbols;
    size_t symbol_count;
    binrwir_binfunc_literal_record_t *literals;
    size_t literal_count;
    binrwir_alias_rule_t *alias_rules;
    size_t alias_rule_count;
    char *pool_text;
    size_t pool_text_size;
    uint8_t *pool_bytes;
    size_t pool_bytes_size;
};

struct binrwir_binfunc_builder {
    runtimec_allocator_t allocator;
    binrwir_binfunc_identity_t identity;
    bool finalized;
    binrwir_binfunc_instruction_arrays_t instructions;
    binrwir_argument_t *arguments;
    size_t argument_count;
    size_t argument_capacity;
    binrwir_binfunc_parameter_arrays_t read_parameters;
    binrwir_binfunc_parameter_arrays_t write_parameters;
    binrwir_binfunc_symbol_record_t *symbols;
    size_t symbol_count;
    size_t symbol_capacity;
    binrwir_binfunc_literal_record_t *literals;
    size_t literal_count;
    size_t literal_capacity;
    binrwir_alias_rule_t *alias_rules;
    size_t alias_rule_count;
    size_t alias_rule_capacity;
};

static void binrwir_binfunc_identity_invalidate(
    binrwir_binfunc_identity_t *identity) {
    if (identity != NULL) {
        memset(identity, 0, sizeof(*identity));
        identity->schema_version = UINT32_MAX;
        identity->rwir_manifest_version = UINT32_MAX;
        identity->binrwir_binary_interface_generation = UINT32_MAX;
        identity->canonical_encoding_revision = UINT32_MAX;
    }
}

static int binrwir_binfunc_identity_is_usable(
    const binrwir_binfunc_identity_t *identity) {
    return identity->schema_version != UINT32_MAX &&
           identity->rwir_manifest_version != UINT32_MAX &&
           identity->binrwir_binary_interface_generation ==
               BINRWIR_BINARY_INTERFACE_GENERATION &&
           identity->canonical_encoding_revision != UINT32_MAX;
}

static int binrwir_binfunc_array_shape_is_invalid(const void *array,
                                                  size_t count) {
    if (count == 0U) {
        return array != NULL;
    }
    return array == NULL;
}

static size_t binrwir_binfunc_grown_capacity(size_t capacity,
                                             size_t required_count) {
    size_t grown_capacity =
        capacity == 0U ? BINRWIR_BINFUNC_INITIAL_CAPACITY : capacity;

    while (grown_capacity < required_count) {
        if (grown_capacity > SIZE_MAX / 2U) {
            return 0U;
        }
        grown_capacity *= 2U;
    }
    return grown_capacity;
}

static void *binrwir_binfunc_regrow(const runtimec_allocator_t *allocator,
                                    const void *array, size_t capacity,
                                    size_t grown_capacity, size_t element_size,
                                    size_t element_alignment) {
    void *grown;

    if (grown_capacity > SIZE_MAX / element_size) {
        return NULL;
    }
    grown = runtimec_allocator_allocate(
        allocator, element_size * grown_capacity, element_alignment);
    if (grown == NULL) {
        return NULL;
    }
    if (array != NULL) {
        memcpy(grown, array, element_size * capacity);
        (void)runtimec_allocator_deallocate(allocator, (void *)array,
                                            element_size * capacity,
                                            element_alignment);
    }
    return grown;
}

static void *binrwir_binfunc_allocate_exact(
    const runtimec_allocator_t *allocator, size_t count, size_t element_size,
    size_t element_alignment) {
    if (count == 0U || count > SIZE_MAX / element_size) {
        return NULL;
    }
    return runtimec_allocator_allocate(allocator, element_size * count,
                                       element_alignment);
}

static void binrwir_binfunc_release_array(const runtimec_allocator_t *allocator,
                                          void *array, size_t count,
                                          size_t element_size,
                                          size_t element_alignment) {
    if (array == NULL) {
        return;
    }
    (void)runtimec_allocator_deallocate(allocator, array,
                                        element_size * count,
                                        element_alignment);
}

static int binrwir_binfunc_symbol_compare(runtimec_string_view_t left,
                                          runtimec_string_view_t right) {
    size_t common = left.size < right.size ? left.size : right.size;
    int order = memcmp(left.data, right.data, common);

    if (order != 0) {
        return order;
    }
    if (left.size < right.size) {
        return -1;
    }
    if (left.size > right.size) {
        return 1;
    }
    return 0;
}

static void binrwir_binfunc_symbol_order(
    const binrwir_binfunc_symbol_record_t *symbols, size_t count,
    size_t *output_order) {
    size_t ordinal;

    for (ordinal = 0U; ordinal < count; ++ordinal) {
        size_t candidate = ordinal;

        output_order[ordinal] = ordinal;
        while (candidate > 0U &&
               binrwir_binfunc_symbol_compare(
                   symbols[output_order[candidate - 1U]].name,
                   symbols[output_order[candidate]].name) > 0) {
            size_t moved = output_order[candidate - 1U];

            output_order[candidate - 1U] = output_order[candidate];
            output_order[candidate] = moved;
            --candidate;
        }
    }
}

static int binrwir_binfunc_size_add(size_t *total, size_t size) {
    if (*total > SIZE_MAX - size) {
        return 0;
    }
    *total += size;
    return 1;
}

static int binrwir_binfunc_text_size(
    const binrwir_binfunc_parameter_arrays_t *read_parameters,
    const binrwir_binfunc_parameter_arrays_t *write_parameters,
    const binrwir_binfunc_symbol_record_t *symbols, size_t symbol_count,
    const binrwir_binfunc_literal_record_t *literals, size_t literal_count,
    size_t *output_size) {
    size_t ordinal;

    for (ordinal = 0U; ordinal < read_parameters->count; ++ordinal) {
        if (!binrwir_binfunc_size_add(
                output_size, read_parameters->types[ordinal].size)) {
            return 0;
        }
    }
    for (ordinal = 0U; ordinal < write_parameters->count; ++ordinal) {
        if (!binrwir_binfunc_size_add(
                output_size, write_parameters->types[ordinal].size)) {
            return 0;
        }
    }
    for (ordinal = 0U; ordinal < symbol_count; ++ordinal) {
        if (!binrwir_binfunc_size_add(output_size, symbols[ordinal].name.size) ||
            !binrwir_binfunc_size_add(output_size,
                                      symbols[ordinal].langtype.size)) {
            return 0;
        }
    }
    for (ordinal = 0U; ordinal < literal_count; ++ordinal) {
        if (!binrwir_binfunc_size_add(output_size,
                                      literals[ordinal].langtype.size)) {
            return 0;
        }
    }
    return 1;
}

#define BINRWIR_BINFUNC_POOL_ALIGNMENT 8U

static size_t binrwir_binfunc_pool_padded_size(size_t size) {
    return (size + BINRWIR_BINFUNC_POOL_ALIGNMENT - 1U) /
           BINRWIR_BINFUNC_POOL_ALIGNMENT * BINRWIR_BINFUNC_POOL_ALIGNMENT;
}

static int binrwir_binfunc_bytes_size(
    const binrwir_binfunc_literal_record_t *literals, size_t literal_count,
    size_t *output_size) {
    size_t ordinal;

    for (ordinal = 0U; ordinal < literal_count; ++ordinal) {
        
        if (!binrwir_binfunc_size_add(
                output_size,
                binrwir_binfunc_pool_padded_size(
                    literals[ordinal].wire_bytes.size))) {
            return 0;
        }
    }
    return 1;
}

static runtimec_string_view_t binrwir_binfunc_copy_text(
    binrwir_binfunc_t *program, runtimec_string_view_t text,
    size_t *text_offset) {
    char *copy = program->pool_text + *text_offset;

    if (text.size != 0U) {
        memcpy(copy, text.data, text.size);
    }
    *text_offset += text.size;
    return runtimec_string_view_make(copy, text.size);
}

static runtimec_bytes_view_t binrwir_binfunc_copy_bytes(
    binrwir_binfunc_t *program, runtimec_bytes_view_t bytes,
    size_t *bytes_offset) {
    uint8_t *copy = program->pool_bytes + *bytes_offset;

    if (bytes.size != 0U) {
        memcpy(copy, bytes.data, bytes.size);
    }
    *bytes_offset += binrwir_binfunc_pool_padded_size(bytes.size);
    return runtimec_bytes_view_make(copy, bytes.size);
}

static int binrwir_binfunc_argument_is_unbound(
    const binrwir_binfunc_t *program, const binrwir_argument_t *argument) {
    switch (argument->reference_kind) {
    case BINRWIR_REFERENCE_PARAMETER_READ:
        return argument->slot >= program->read_parameters.count ||
               program->read_parameters.argument_indices[argument->slot] !=
                   argument->argument_index;
    case BINRWIR_REFERENCE_PARAMETER_WRITE:
        return argument->slot >= program->write_parameters.count ||
               program->write_parameters.argument_indices[argument->slot] !=
                   argument->argument_index;
    case BINRWIR_REFERENCE_LOCAL:
    case BINRWIR_REFERENCE_FUNCTION:
    case BINRWIR_REFERENCE_STATIC:
        
        return (size_t)argument->slot >= program->symbol_count;
    case BINRWIR_REFERENCE_LITERAL:
        return argument->payload >= (uint64_t)program->literal_count;
    default:
        return 0;
    }
}

static int binrwir_binfunc_local_symbol_is_untyped(
    const binrwir_binfunc_t *program, const binrwir_argument_t *argument) {
    if (argument->reference_kind != BINRWIR_REFERENCE_LOCAL) {
        return 0;
    }
    return (size_t)argument->slot >= program->symbol_count ||
           program->symbols[argument->slot].langtype.size == 0U;
}

typedef struct binrwir_type_from_target_set {
    
    uint8_t *bits;
    size_t size;
} binrwir_type_from_target_set_t;

static bool binrwir_type_from_target_contains(
    const binrwir_type_from_target_set_t *set, uint16_t symbol_index) {
    if (set == NULL || set->bits == NULL || (size_t)symbol_index >= set->size) {
        return false;
    }
    return (set->bits[symbol_index / 8U] &
            (uint8_t)(1U << (symbol_index % 8U))) != 0U;
}

static void binrwir_type_from_target_insert(binrwir_type_from_target_set_t *set,
                                            uint16_t symbol_index) {
    if (set == NULL || set->bits == NULL || (size_t)symbol_index >= set->size) {
        return;
    }
    set->bits[symbol_index / 8U] |= (uint8_t)(1U << (symbol_index % 8U));
}

static void binrwir_type_from_target_collect(
    const binrwir_binfunc_t *program, binrwir_type_from_target_set_t *set) {
    for (size_t instruction_ordinal = 0U;
         instruction_ordinal < program->instructions.count;
         ++instruction_ordinal) {
        const binrwir_binfunc_instruction_record_t *record =
            &program->instructions.records[instruction_ordinal];
        const size_t read_offset = (size_t)record->read_argument_offset;
        const size_t read_count = (size_t)record->read_argument_count;
        const size_t write_count = (size_t)record->write_argument_count;

        bool has_static_read = false;
        for (size_t ordinal = 0U; ordinal < read_count; ++ordinal) {
            if (program->arguments[read_offset + ordinal].reference_kind ==
                BINRWIR_REFERENCE_STATIC) {
                has_static_read = true;
                break;
            }
        }
        if (!has_static_read) {
            continue;
        }
        for (size_t ordinal = 0U; ordinal < write_count; ++ordinal) {
            const binrwir_argument_t *argument =
                &program->arguments[read_offset + read_count + ordinal];

            if (argument->reference_kind != BINRWIR_REFERENCE_LOCAL ||
                (size_t)argument->slot >= program->symbol_count ||
                program->symbols[argument->slot].langtype.size != 0U) {
                continue;
            }
            binrwir_type_from_target_insert(set, argument->slot);
        }
    }
}

static int binrwir_binfunc_span_is_invalid(
    const binrwir_binfunc_t *program, const binrwir_argument_t *arguments,
    size_t count, bool is_write_span, bool instruction_has_static_read,
    const binrwir_type_from_target_set_t *type_from_target) {
    size_t argument_ordinal;

    for (argument_ordinal = 0U; argument_ordinal < count; ++argument_ordinal) {
        const binrwir_argument_t *argument = &arguments[argument_ordinal];
        bool exempt;

        if (binrwir_argument_validate(argument) != RUNTIMEC_STATUS_SUCCESS ||
            binrwir_binfunc_argument_is_unbound(program, argument)) {
            return 1;
        }
        if (!binrwir_binfunc_local_symbol_is_untyped(program, argument)) {
            continue;
        }
        exempt = is_write_span
                     ? instruction_has_static_read
                     : binrwir_type_from_target_contains(
                           type_from_target, argument->slot);
        if (!exempt) {
            return 1;
        }
    }
    return 0;
}

static int binrwir_binfunc_instructions_are_invalid(
    const binrwir_binfunc_t *program) {
    
    binrwir_type_from_target_set_t type_from_target = {NULL, 0U};
    size_t instruction_ordinal;
    int invalid = 0;

    if (program->symbol_count != 0U) {
        const size_t size = (program->symbol_count + 7U) / 8U;
        uint8_t *bits = (uint8_t *)runtimec_allocator_allocate(
            &program->allocator, size, _Alignof(uint8_t));
        if (bits == NULL) {
            return 1;
        }
        memset(bits, 0, size);
        type_from_target.bits = bits;
        type_from_target.size = program->symbol_count;
        binrwir_type_from_target_collect(program, &type_from_target);
    }

    for (instruction_ordinal = 0U;
         instruction_ordinal < program->instructions.count;
         ++instruction_ordinal) {
        const binrwir_binfunc_instruction_record_t *record =
            &program->instructions.records[instruction_ordinal];
        size_t read_offset = (size_t)record->read_argument_offset;
        size_t read_count = (size_t)record->read_argument_count;
        size_t write_count = (size_t)record->write_argument_count;
        size_t span_count = read_count + write_count;

        if (program->instructions.heads[instruction_ordinal]
                    .operation_identifier ==
                BINRWIR_OPERATION_IDENTIFIER_NONE ||
            record->source_logical_program_counter ==
                BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE ||
            span_count > program->argument_count ||
            read_offset > program->argument_count - span_count) {
            return 1;
        }
        
        bool instruction_has_static_read = false;
        for (size_t ordinal = 0U; ordinal < read_count; ++ordinal) {
            if (program->arguments[read_offset + ordinal].reference_kind ==
                BINRWIR_REFERENCE_STATIC) {
                instruction_has_static_read = true;
                break;
            }
        }
        {
            const int read_bad = binrwir_binfunc_span_is_invalid(
                program, program->arguments + read_offset, read_count, false,
                instruction_has_static_read, &type_from_target);
            const int write_bad = binrwir_binfunc_span_is_invalid(
                program, program->arguments + read_offset + read_count,
                write_count, true, instruction_has_static_read,
                &type_from_target);
            if (read_bad || write_bad) {
                invalid = 1;
                break;
            }
        }
    }
    if (type_from_target.bits != NULL) {
        (void)runtimec_allocator_deallocate(
            &program->allocator, type_from_target.bits,
            type_from_target.size == 0U ? 1U : type_from_target.size,
            _Alignof(uint8_t));
    }
    return invalid;
}

static int binrwir_binfunc_pools_are_invalid(
    const binrwir_binfunc_t *program) {
    size_t ordinal;

    if (program->symbol_count > (size_t)UINT16_MAX ||
        binrwir_binfunc_array_shape_is_invalid(program->symbols,
                                               program->symbol_count) ||
        binrwir_binfunc_array_shape_is_invalid(program->literals,
                                               program->literal_count) ||
        binrwir_binfunc_array_shape_is_invalid(program->pool_text,
                                               program->pool_text_size) ||
        binrwir_binfunc_array_shape_is_invalid(program->pool_bytes,
                                               program->pool_bytes_size) ||
        program->literal_count > UINT32_MAX) {
        return 1;
    }
    for (ordinal = 0U; ordinal < program->symbol_count; ++ordinal) {
        const binrwir_binfunc_symbol_record_t *symbol =
            &program->symbols[ordinal];

        if (symbol->name.size == 0U ||
            (symbol->langtype.size != 0U &&
             binrwir_signature_validate_parameter_type(symbol->langtype) !=
                 RUNTIMEC_STATUS_SUCCESS)) {
            return 1;
        }
        if (ordinal != 0U &&
            binrwir_binfunc_symbol_compare(program->symbols[ordinal - 1U].name,
                                           symbol->name) >= 0) {
            return 1;
        }
    }
    for (ordinal = 0U; ordinal < program->literal_count; ++ordinal) {
        const binrwir_binfunc_literal_record_t *literal =
            &program->literals[ordinal];

        if (literal->wire_bytes.size == 0U) {
            return 1;
        }
        if (literal->langtype.size == 0U) {
            if (!binrwir_literal_is_none_value(literal->wire_bytes)) {
                return 1;
            }
        } else if (binrwir_signature_validate_parameter_type(
                       literal->langtype) != RUNTIMEC_STATUS_SUCCESS) {
            return 1;
        }
    }
    return 0;
}

static binrwir_signature_view_t binrwir_binfunc_signature_view(
    const binrwir_binfunc_t *program) {
    binrwir_signature_view_t signature = {
        .read_parameter_argument_indices =
            program->read_parameters.argument_indices,
        .read_parameter_count = program->read_parameters.count,
        .write_parameter_argument_indices =
            program->write_parameters.argument_indices,
        .write_parameter_count = program->write_parameters.count,
        .read_parameter_types = program->read_parameters.types,
        .write_parameter_types = program->write_parameters.types,
        .alias_rules = program->alias_rules,
        .alias_rule_count = program->alias_rule_count,
    };

    return signature;
}

runtimec_status_t binrwir_binfunc_get_identity(
    const binrwir_binfunc_t *program,
    binrwir_binfunc_identity_t *output_identity) {
    binrwir_binfunc_identity_invalidate(output_identity);
    if (program == NULL || output_identity == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    *output_identity = program->identity;
    return RUNTIMEC_STATUS_SUCCESS;
}

static bool binrwir_binfunc_empty_parameter_type_is_legal(void *context,
                                                          int is_write,
                                                          size_t ordinal) {
    const binrwir_binfunc_t *program = (const binrwir_binfunc_t *)context;
    const uint32_t *indices =
        is_write != 0 ? program->write_parameters.argument_indices
                      : program->read_parameters.argument_indices;
    const size_t count = is_write != 0 ? program->write_parameters.count
                                       : program->read_parameters.count;
    const binrwir_argument_t *argument;
    const binrwir_binfunc_literal_record_t *literal;

    if (indices == NULL || ordinal >= count) {
        return false;
    }
    if (indices[ordinal] >= program->argument_count) {
        return false;
    }
    argument = &program->arguments[indices[ordinal]];
    if (argument->reference_kind != BINRWIR_REFERENCE_LITERAL ||
        argument->payload >= program->literal_count) {
        return false;
    }
    literal = &program->literals[argument->payload];
    return binrwir_literal_is_none_value(literal->wire_bytes);
}

runtimec_status_t binrwir_binfunc_validate(const binrwir_binfunc_t *program) {
    binrwir_signature_view_t signature;
    size_t text_size = 0U;
    size_t bytes_size = 0U;

    if (program == NULL || !runtimec_allocator_is_valid(&program->allocator) ||
        !binrwir_binfunc_identity_is_usable(&program->identity) ||
        program->instructions.count == 0U) {
        return RUNTIMEC_STATUS_INVALID;
    }
    signature = binrwir_binfunc_signature_view(program);
    if (signature.read_parameter_count > UINT32_MAX ||
        signature.write_parameter_count > UINT32_MAX ||
        program->instructions.count > UINT32_MAX ||
        program->argument_count > UINT32_MAX ||
        signature.alias_rule_count > UINT32_MAX ||
        binrwir_binfunc_array_shape_is_invalid(
            program->instructions.heads, program->instructions.count) ||
        binrwir_binfunc_array_shape_is_invalid(
            program->instructions.records, program->instructions.count) ||
        binrwir_binfunc_array_shape_is_invalid(program->arguments,
                                               program->argument_count) ||
        binrwir_binfunc_array_shape_is_invalid(signature.alias_rules,
                                               signature.alias_rule_count) ||
        binrwir_binfunc_pools_are_invalid(program) ||
        binrwir_signature_validate_permitting_empty_types(
            &signature, (void *)(uintptr_t)program,
            binrwir_binfunc_empty_parameter_type_is_legal) !=
            RUNTIMEC_STATUS_SUCCESS) {
        return RUNTIMEC_STATUS_INVALID;
    }
    
    if (!binrwir_binfunc_text_size(&program->read_parameters,
                                   &program->write_parameters,
                                   program->symbols, program->symbol_count,
                                   program->literals, program->literal_count,
                                   &text_size) ||
        !binrwir_binfunc_bytes_size(program->literals, program->literal_count,
                                    &bytes_size) ||
        text_size != program->pool_text_size ||
        bytes_size != program->pool_bytes_size) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (binrwir_binfunc_instructions_are_invalid(program)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_binfunc_get_view(
    const binrwir_binfunc_t *program, binrwir_binfunc_view_t *output_view) {
    if (output_view != NULL) {
        memset(output_view, 0, sizeof(*output_view));
        binrwir_binfunc_identity_invalidate(&output_view->identity);
    }
    if (program == NULL || output_view == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    output_view->identity = program->identity;
    output_view->signature = binrwir_binfunc_signature_view(program);
    output_view->instruction_count = program->instructions.count;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_binfunc_get_instruction(
    const binrwir_binfunc_t *program, uint32_t instruction_identifier,
    binrwir_instruction_view_t *output_instruction) {
    const binrwir_binfunc_instruction_record_t *record;
    binrwir_argument_span_t read_arguments;
    binrwir_argument_span_t write_arguments;

    if (output_instruction != NULL) {
        memset(output_instruction, 0, sizeof(*output_instruction));
    }
    if (program == NULL || output_instruction == NULL ||
        instruction_identifier == BINRWIR_IDENTIFIER_NONE ||
        (size_t)instruction_identifier >= program->instructions.count) {
        return RUNTIMEC_STATUS_INVALID;
    }
    record = &program->instructions.records[instruction_identifier];
    read_arguments.data = program->arguments + record->read_argument_offset;
    read_arguments.count = (size_t)record->read_argument_count;
    write_arguments.data = read_arguments.data + record->read_argument_count;
    write_arguments.count = (size_t)record->write_argument_count;
    return binrwir_instruction_get_view(
        &program->instructions.heads[instruction_identifier],
        record->source_logical_program_counter, read_arguments,
        write_arguments, output_instruction);
}

runtimec_status_t binrwir_binfunc_find_instruction(
    const binrwir_binfunc_t *program, uint32_t source_logical_program_counter,
    uint32_t *output_instruction_identifier) {
    size_t instruction_ordinal;

    if (output_instruction_identifier != NULL) {
        *output_instruction_identifier = BINRWIR_IDENTIFIER_NONE;
    }
    if (program == NULL || output_instruction_identifier == NULL ||
        source_logical_program_counter ==
            BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE) {
        return RUNTIMEC_STATUS_INVALID;
    }
    for (instruction_ordinal = 0U;
         instruction_ordinal < program->instructions.count;
         ++instruction_ordinal) {
        if (program->instructions.records[instruction_ordinal]
                .source_logical_program_counter ==
            source_logical_program_counter) {
            *output_instruction_identifier = (uint32_t)instruction_ordinal;
            return RUNTIMEC_STATUS_SUCCESS;
        }
    }
    return RUNTIMEC_STATUS_INVALID;
}

runtimec_status_t binrwir_binfunc_get_symbol_count(
    const binrwir_binfunc_t *program, size_t *output_symbol_count) {
    if (output_symbol_count != NULL) {
        *output_symbol_count = 0U;
    }
    if (program == NULL || output_symbol_count == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    *output_symbol_count = program->symbol_count;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_binfunc_get_symbol(
    const binrwir_binfunc_t *program, uint16_t symbol_index,
    runtimec_string_view_t *output_name,
    runtimec_string_view_t *output_langtype) {
    if (output_name != NULL) {
        *output_name = runtimec_string_view_make(NULL, 0U);
    }
    if (output_langtype != NULL) {
        *output_langtype = runtimec_string_view_make(NULL, 0U);
    }
    if (program == NULL || output_name == NULL || output_langtype == NULL ||
        (size_t)symbol_index >= program->symbol_count) {
        return RUNTIMEC_STATUS_INVALID;
    }
    *output_name = program->symbols[symbol_index].name;
    *output_langtype = program->symbols[symbol_index].langtype;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_binfunc_get_literal_count(
    const binrwir_binfunc_t *program, size_t *output_literal_count) {
    if (output_literal_count != NULL) {
        *output_literal_count = 0U;
    }
    if (program == NULL || output_literal_count == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    *output_literal_count = program->literal_count;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_binfunc_get_literal(
    const binrwir_binfunc_t *program, uint32_t literal_index,
    runtimec_bytes_view_t *output_wire_bytes,
    runtimec_string_view_t *output_langtype) {
    if (output_wire_bytes != NULL) {
        *output_wire_bytes = runtimec_bytes_view_make(NULL, 0U);
    }
    if (output_langtype != NULL) {
        *output_langtype = runtimec_string_view_make(NULL, 0U);
    }
    if (program == NULL || output_wire_bytes == NULL ||
        output_langtype == NULL ||
        (size_t)literal_index >= program->literal_count) {
        return RUNTIMEC_STATUS_INVALID;
    }
    *output_wire_bytes = program->literals[literal_index].wire_bytes;
    *output_langtype = program->literals[literal_index].langtype;
    return RUNTIMEC_STATUS_SUCCESS;
}

static void binrwir_binfunc_release(binrwir_binfunc_t *program) {
    const runtimec_allocator_t *allocator = &program->allocator;

    binrwir_binfunc_release_array(allocator, program->instructions.heads,
                                  program->instructions.capacity,
                                  sizeof(binrwir_instruction_t),
                                  _Alignof(binrwir_instruction_t));
    binrwir_binfunc_release_array(
        allocator, program->instructions.records,
        program->instructions.capacity,
        sizeof(binrwir_binfunc_instruction_record_t),
        _Alignof(binrwir_binfunc_instruction_record_t));
    binrwir_binfunc_release_array(allocator, program->arguments,
                                  program->argument_count,
                                  sizeof(binrwir_argument_t),
                                  _Alignof(binrwir_argument_t));
    binrwir_binfunc_release_array(allocator,
                                  program->read_parameters.argument_indices,
                                  program->read_parameters.capacity,
                                  sizeof(uint32_t), _Alignof(uint32_t));
    binrwir_binfunc_release_array(allocator, program->read_parameters.types,
                                  program->read_parameters.capacity,
                                  sizeof(runtimec_string_view_t),
                                  _Alignof(runtimec_string_view_t));
    binrwir_binfunc_release_array(allocator,
                                  program->write_parameters.argument_indices,
                                  program->write_parameters.capacity,
                                  sizeof(uint32_t), _Alignof(uint32_t));
    binrwir_binfunc_release_array(allocator, program->write_parameters.types,
                                  program->write_parameters.capacity,
                                  sizeof(runtimec_string_view_t),
                                  _Alignof(runtimec_string_view_t));
    binrwir_binfunc_release_array(allocator, program->symbols,
                                  program->symbol_count,
                                  sizeof(binrwir_binfunc_symbol_record_t),
                                  _Alignof(binrwir_binfunc_symbol_record_t));
    binrwir_binfunc_release_array(allocator, program->literals,
                                  program->literal_count,
                                  sizeof(binrwir_binfunc_literal_record_t),
                                  _Alignof(binrwir_binfunc_literal_record_t));
    binrwir_binfunc_release_array(allocator, program->alias_rules,
                                  program->alias_rule_count,
                                  sizeof(binrwir_alias_rule_t),
                                  _Alignof(binrwir_alias_rule_t));
    binrwir_binfunc_release_array(allocator, program->pool_text,
                                  program->pool_text_size, sizeof(char),
                                  _Alignof(char));
    binrwir_binfunc_release_array(allocator, program->pool_bytes,
                                  program->pool_bytes_size, sizeof(uint8_t),
                                  _Alignof(uint64_t));
    memset(program, 0, sizeof(*program));
}

runtimec_status_t binrwir_binfunc_destroy(binrwir_binfunc_t *program) {
    runtimec_allocator_t allocator;

    if (program == NULL ||
        !runtimec_allocator_is_valid(&program->allocator)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    allocator = program->allocator;
    binrwir_binfunc_release(program);
    (void)runtimec_allocator_deallocate(&allocator, program, sizeof(*program),
                                        _Alignof(binrwir_binfunc_t));
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t binrwir_binfunc_allocate_storage(
    binrwir_binfunc_t *program, const binrwir_binfunc_builder_t *builder) {
    const runtimec_allocator_t *allocator = &program->allocator;
    size_t text_size = 0U;
    size_t bytes_size = 0U;

    if (!binrwir_binfunc_text_size(&builder->read_parameters,
                                   &builder->write_parameters,
                                   builder->symbols, builder->symbol_count,
                                   builder->literals, builder->literal_count,
                                   &text_size) ||
        !binrwir_binfunc_bytes_size(builder->literals, builder->literal_count,
                                    &bytes_size)) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    program->instructions.count = builder->instructions.count;
    program->instructions.capacity = builder->instructions.count;
    program->argument_count = builder->argument_count;
    program->read_parameters.count = builder->read_parameters.count;
    program->read_parameters.capacity = builder->read_parameters.count;
    program->write_parameters.count = builder->write_parameters.count;
    program->write_parameters.capacity = builder->write_parameters.count;
    program->symbol_count = builder->symbol_count;
    program->literal_count = builder->literal_count;
    program->alias_rule_count = builder->alias_rule_count;
    program->pool_text_size = text_size;
    program->pool_bytes_size = bytes_size;
    program->instructions.heads = (binrwir_instruction_t *)
        binrwir_binfunc_allocate_exact(allocator, program->instructions.count,
                                       sizeof(binrwir_instruction_t),
                                       _Alignof(binrwir_instruction_t));
    if (program->instructions.count != 0U &&
        program->instructions.heads == NULL) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    program->instructions.records = (binrwir_binfunc_instruction_record_t *)
        binrwir_binfunc_allocate_exact(
            allocator, program->instructions.count,
            sizeof(binrwir_binfunc_instruction_record_t),
            _Alignof(binrwir_binfunc_instruction_record_t));
    if (program->instructions.count != 0U &&
        program->instructions.records == NULL) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    program->arguments = (binrwir_argument_t *)binrwir_binfunc_allocate_exact(
        allocator, program->argument_count, sizeof(binrwir_argument_t),
        _Alignof(binrwir_argument_t));
    if (program->argument_count != 0U && program->arguments == NULL) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    program->read_parameters.argument_indices = (uint32_t *)
        binrwir_binfunc_allocate_exact(allocator,
                                       program->read_parameters.count,
                                       sizeof(uint32_t), _Alignof(uint32_t));
    program->write_parameters.argument_indices = (uint32_t *)
        binrwir_binfunc_allocate_exact(allocator,
                                       program->write_parameters.count,
                                       sizeof(uint32_t), _Alignof(uint32_t));
    program->read_parameters.types = (runtimec_string_view_t *)
        binrwir_binfunc_allocate_exact(allocator,
                                       program->read_parameters.count,
                                       sizeof(runtimec_string_view_t),
                                       _Alignof(runtimec_string_view_t));
    program->write_parameters.types = (runtimec_string_view_t *)
        binrwir_binfunc_allocate_exact(allocator,
                                       program->write_parameters.count,
                                       sizeof(runtimec_string_view_t),
                                       _Alignof(runtimec_string_view_t));
    if ((program->read_parameters.count != 0U &&
         (program->read_parameters.argument_indices == NULL ||
          program->read_parameters.types == NULL)) ||
        (program->write_parameters.count != 0U &&
         (program->write_parameters.argument_indices == NULL ||
          program->write_parameters.types == NULL))) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    program->symbols = (binrwir_binfunc_symbol_record_t *)
        binrwir_binfunc_allocate_exact(allocator, program->symbol_count,
                                       sizeof(binrwir_binfunc_symbol_record_t),
                                       _Alignof(
                                           binrwir_binfunc_symbol_record_t));
    if (program->symbol_count != 0U && program->symbols == NULL) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    program->literals = (binrwir_binfunc_literal_record_t *)
        binrwir_binfunc_allocate_exact(allocator, program->literal_count,
                                       sizeof(binrwir_binfunc_literal_record_t),
                                       _Alignof(
                                           binrwir_binfunc_literal_record_t));
    if (program->literal_count != 0U && program->literals == NULL) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    program->alias_rules = (binrwir_alias_rule_t *)
        binrwir_binfunc_allocate_exact(allocator, program->alias_rule_count,
                                       sizeof(binrwir_alias_rule_t),
                                       _Alignof(binrwir_alias_rule_t));
    if (program->alias_rule_count != 0U && program->alias_rules == NULL) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    program->pool_text = (char *)binrwir_binfunc_allocate_exact(
        allocator, program->pool_text_size, sizeof(char), _Alignof(char));
    
    program->pool_bytes = (uint8_t *)binrwir_binfunc_allocate_exact(
        allocator, program->pool_bytes_size, sizeof(uint8_t),
        _Alignof(uint64_t));
    if ((program->pool_text_size != 0U && program->pool_text == NULL) ||
        (program->pool_bytes_size != 0U && program->pool_bytes == NULL)) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static void binrwir_binfunc_copy_pools(binrwir_binfunc_t *program,
                                       const binrwir_binfunc_builder_t *builder,
                                       size_t *symbol_order,
                                       size_t *symbol_remap) {
    size_t text_offset = 0U;
    size_t bytes_offset = 0U;
    size_t ordinal;

    for (ordinal = 0U; ordinal < builder->read_parameters.count; ++ordinal) {
        program->read_parameters.types[ordinal] = binrwir_binfunc_copy_text(
            program, builder->read_parameters.types[ordinal], &text_offset);
    }
    for (ordinal = 0U; ordinal < builder->write_parameters.count; ++ordinal) {
        program->write_parameters.types[ordinal] = binrwir_binfunc_copy_text(
            program, builder->write_parameters.types[ordinal], &text_offset);
    }
    binrwir_binfunc_symbol_order(builder->symbols, builder->symbol_count,
                                 symbol_order);
    for (ordinal = 0U; ordinal < builder->symbol_count; ++ordinal) {
        size_t source_index = symbol_order[ordinal];

        program->symbols[ordinal].name = binrwir_binfunc_copy_text(
            program, builder->symbols[source_index].name, &text_offset);
        program->symbols[ordinal].langtype = binrwir_binfunc_copy_text(
            program, builder->symbols[source_index].langtype, &text_offset);
        symbol_remap[source_index] = ordinal;
    }
    for (ordinal = 0U; ordinal < builder->literal_count; ++ordinal) {
        program->literals[ordinal].langtype = binrwir_binfunc_copy_text(
            program, builder->literals[ordinal].langtype, &text_offset);
    }
    for (ordinal = 0U; ordinal < builder->literal_count; ++ordinal) {
        program->literals[ordinal].wire_bytes = binrwir_binfunc_copy_bytes(
            program, builder->literals[ordinal].wire_bytes, &bytes_offset);
    }
    for (ordinal = 0U; ordinal < program->argument_count; ++ordinal) {
        binrwir_argument_t *argument = &program->arguments[ordinal];

        if ((argument->reference_kind == BINRWIR_REFERENCE_LOCAL ||
             argument->reference_kind == BINRWIR_REFERENCE_FUNCTION ||
             argument->reference_kind == BINRWIR_REFERENCE_STATIC) &&
            (size_t)argument->slot < program->symbol_count) {
            argument->slot = (uint16_t)symbol_remap[argument->slot];
        }
    }
}

static int binrwir_binfunc_builder_is_writable(
    const binrwir_binfunc_builder_t *builder) {
    return builder != NULL && !builder->finalized &&
           runtimec_allocator_is_valid(&builder->allocator);
}

static int binrwir_binfunc_builder_reserve_parameters(
    binrwir_binfunc_builder_t *builder,
    binrwir_binfunc_parameter_arrays_t *arrays, size_t required_count) {
    size_t grown_capacity;
    void *grown;

    if (arrays->capacity >= required_count) {
        return 1;
    }
    grown_capacity = binrwir_binfunc_grown_capacity(arrays->capacity,
                                                    required_count);
    if (grown_capacity == 0U) {
        return 0;
    }
    grown = binrwir_binfunc_regrow(&builder->allocator,
                                   arrays->argument_indices, arrays->capacity,
                                   grown_capacity, sizeof(uint32_t),
                                   _Alignof(uint32_t));
    if (grown == NULL) {
        return 0;
    }
    arrays->argument_indices = (uint32_t *)grown;
    grown = binrwir_binfunc_regrow(&builder->allocator, arrays->types,
                                   arrays->capacity, grown_capacity,
                                   sizeof(runtimec_string_view_t),
                                   _Alignof(runtimec_string_view_t));
    if (grown == NULL) {
        return 0;
    }
    arrays->types = (runtimec_string_view_t *)grown;
    arrays->capacity = grown_capacity;
    return 1;
}

static int binrwir_binfunc_builder_reserve_instructions(
    binrwir_binfunc_builder_t *builder, size_t required_count) {
    size_t grown_capacity;
    void *grown;

    if (builder->instructions.capacity >= required_count) {
        return 1;
    }
    grown_capacity = binrwir_binfunc_grown_capacity(
        builder->instructions.capacity, required_count);
    if (grown_capacity == 0U) {
        return 0;
    }
    grown = binrwir_binfunc_regrow(
        &builder->allocator, builder->instructions.heads,
        builder->instructions.capacity, grown_capacity,
        sizeof(binrwir_instruction_t), _Alignof(binrwir_instruction_t));
    if (grown == NULL) {
        return 0;
    }
    builder->instructions.heads = (binrwir_instruction_t *)grown;
    grown = binrwir_binfunc_regrow(
        &builder->allocator, builder->instructions.records,
        builder->instructions.capacity, grown_capacity,
        sizeof(binrwir_binfunc_instruction_record_t),
        _Alignof(binrwir_binfunc_instruction_record_t));
    if (grown == NULL) {
        return 0;
    }
    builder->instructions.records =
        (binrwir_binfunc_instruction_record_t *)grown;
    builder->instructions.capacity = grown_capacity;
    return 1;
}

static int binrwir_binfunc_builder_reserve_arguments(
    binrwir_binfunc_builder_t *builder, size_t required_count) {
    size_t grown_capacity;
    void *grown;

    if (builder->argument_capacity >= required_count) {
        return 1;
    }
    grown_capacity = binrwir_binfunc_grown_capacity(builder->argument_capacity,
                                                    required_count);
    if (grown_capacity == 0U) {
        return 0;
    }
    grown = binrwir_binfunc_regrow(
        &builder->allocator, builder->arguments, builder->argument_capacity,
        grown_capacity, sizeof(binrwir_argument_t),
        _Alignof(binrwir_argument_t));
    if (grown == NULL) {
        return 0;
    }
    builder->arguments = (binrwir_argument_t *)grown;
    builder->argument_capacity = grown_capacity;
    return 1;
}

static int binrwir_binfunc_builder_reserve_alias_rules(
    binrwir_binfunc_builder_t *builder, size_t required_count) {
    size_t grown_capacity;
    void *grown;

    if (builder->alias_rule_capacity >= required_count) {
        return 1;
    }
    grown_capacity = binrwir_binfunc_grown_capacity(
        builder->alias_rule_capacity, required_count);
    if (grown_capacity == 0U) {
        return 0;
    }
    grown = binrwir_binfunc_regrow(
        &builder->allocator, builder->alias_rules,
        builder->alias_rule_capacity, grown_capacity,
        sizeof(binrwir_alias_rule_t), _Alignof(binrwir_alias_rule_t));
    if (grown == NULL) {
        return 0;
    }
    builder->alias_rules = (binrwir_alias_rule_t *)grown;
    builder->alias_rule_capacity = grown_capacity;
    return 1;
}

static int binrwir_binfunc_builder_reserve_symbols(
    binrwir_binfunc_builder_t *builder, size_t required_count) {
    size_t grown_capacity;
    void *grown;

    if (builder->symbol_capacity >= required_count) {
        return 1;
    }
    grown_capacity = binrwir_binfunc_grown_capacity(builder->symbol_capacity,
                                                    required_count);
    if (grown_capacity == 0U) {
        return 0;
    }
    grown = binrwir_binfunc_regrow(
        &builder->allocator, builder->symbols, builder->symbol_capacity,
        grown_capacity, sizeof(binrwir_binfunc_symbol_record_t),
        _Alignof(binrwir_binfunc_symbol_record_t));
    if (grown == NULL) {
        return 0;
    }
    builder->symbols = (binrwir_binfunc_symbol_record_t *)grown;
    builder->symbol_capacity = grown_capacity;
    return 1;
}

static int binrwir_binfunc_builder_reserve_literals(
    binrwir_binfunc_builder_t *builder, size_t required_count) {
    size_t grown_capacity;
    void *grown;

    if (builder->literal_capacity >= required_count) {
        return 1;
    }
    grown_capacity = binrwir_binfunc_grown_capacity(builder->literal_capacity,
                                                    required_count);
    if (grown_capacity == 0U) {
        return 0;
    }
    grown = binrwir_binfunc_regrow(
        &builder->allocator, builder->literals, builder->literal_capacity,
        grown_capacity, sizeof(binrwir_binfunc_literal_record_t),
        _Alignof(binrwir_binfunc_literal_record_t));
    if (grown == NULL) {
        return 0;
    }
    builder->literals = (binrwir_binfunc_literal_record_t *)grown;
    builder->literal_capacity = grown_capacity;
    return 1;
}

runtimec_status_t binrwir_binfunc_builder_create(
    const runtimec_allocator_t *allocator,
    const binrwir_binfunc_identity_t *identity,
    binrwir_binfunc_builder_t **output_builder) {
    binrwir_binfunc_builder_t *builder;

    if (output_builder != NULL) {
        *output_builder = NULL;
    }
    if (output_builder == NULL || identity == NULL ||
        !runtimec_allocator_is_valid(allocator) ||
        !binrwir_binfunc_identity_is_usable(identity)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    builder = (binrwir_binfunc_builder_t *)runtimec_allocator_allocate(
        allocator, sizeof(*builder), _Alignof(binrwir_binfunc_builder_t));
    if (builder == NULL) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    memset(builder, 0, sizeof(*builder));
    builder->allocator = *allocator;
    builder->identity = *identity;
    *output_builder = builder;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_binfunc_builder_reset(
    binrwir_binfunc_builder_t *builder) {
    if (builder == NULL ||
        !runtimec_allocator_is_valid(&builder->allocator)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    builder->finalized = false;
    builder->instructions.count = 0U;
    builder->argument_count = 0U;
    builder->read_parameters.count = 0U;
    builder->write_parameters.count = 0U;
    builder->symbol_count = 0U;
    builder->literal_count = 0U;
    builder->alias_rule_count = 0U;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_binfunc_builder_destroy(
    binrwir_binfunc_builder_t *builder) {
    runtimec_allocator_t allocator;

    if (builder == NULL ||
        !runtimec_allocator_is_valid(&builder->allocator)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    allocator = builder->allocator;
    binrwir_binfunc_release_array(
        &allocator, builder->instructions.heads,
        builder->instructions.capacity, sizeof(binrwir_instruction_t),
        _Alignof(binrwir_instruction_t));
    binrwir_binfunc_release_array(
        &allocator, builder->instructions.records,
        builder->instructions.capacity,
        sizeof(binrwir_binfunc_instruction_record_t),
        _Alignof(binrwir_binfunc_instruction_record_t));
    binrwir_binfunc_release_array(&allocator, builder->arguments,
                                  builder->argument_capacity,
                                  sizeof(binrwir_argument_t),
                                  _Alignof(binrwir_argument_t));
    binrwir_binfunc_release_array(
        &allocator, builder->read_parameters.argument_indices,
        builder->read_parameters.capacity, sizeof(uint32_t),
        _Alignof(uint32_t));
    binrwir_binfunc_release_array(
        &allocator, builder->read_parameters.types,
        builder->read_parameters.capacity, sizeof(runtimec_string_view_t),
        _Alignof(runtimec_string_view_t));
    binrwir_binfunc_release_array(
        &allocator, builder->write_parameters.argument_indices,
        builder->write_parameters.capacity, sizeof(uint32_t),
        _Alignof(uint32_t));
    binrwir_binfunc_release_array(
        &allocator, builder->write_parameters.types,
        builder->write_parameters.capacity, sizeof(runtimec_string_view_t),
        _Alignof(runtimec_string_view_t));
    binrwir_binfunc_release_array(&allocator, builder->symbols,
                                  builder->symbol_capacity,
                                  sizeof(binrwir_binfunc_symbol_record_t),
                                  _Alignof(binrwir_binfunc_symbol_record_t));
    binrwir_binfunc_release_array(&allocator, builder->literals,
                                  builder->literal_capacity,
                                  sizeof(binrwir_binfunc_literal_record_t),
                                  _Alignof(binrwir_binfunc_literal_record_t));
    binrwir_binfunc_release_array(&allocator, builder->alias_rules,
                                  builder->alias_rule_capacity,
                                  sizeof(binrwir_alias_rule_t),
                                  _Alignof(binrwir_alias_rule_t));
    (void)runtimec_allocator_deallocate(&allocator, builder,
                                        sizeof(*builder),
                                        _Alignof(binrwir_binfunc_builder_t));
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_binfunc_builder_add_parameter(
    binrwir_binfunc_builder_t *builder, binrwir_parameter_direction_t direction,
    uint32_t argument_index, runtimec_string_view_t parameter_type,
    uint32_t *output_parameter_index) {
    binrwir_binfunc_parameter_arrays_t *arrays = NULL;

    if (output_parameter_index != NULL) {
        *output_parameter_index = BINRWIR_ARGUMENT_INDEX_NONE;
    }
    if (!binrwir_binfunc_builder_is_writable(builder) ||
        argument_index == BINRWIR_ARGUMENT_INDEX_NONE ||
        output_parameter_index == NULL ||
        binrwir_signature_validate_parameter_type(parameter_type) !=
            RUNTIMEC_STATUS_SUCCESS) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (direction == BINRWIR_PARAMETER_DIRECTION_READ) {
        arrays = &builder->read_parameters;
    } else if (direction == BINRWIR_PARAMETER_DIRECTION_WRITE) {
        arrays = &builder->write_parameters;
    }
    if (arrays == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (arrays->count >= UINT32_MAX) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    if (!binrwir_binfunc_builder_reserve_parameters(builder, arrays,
                                                    arrays->count + 1U)) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    arrays->argument_indices[arrays->count] = argument_index;
    arrays->types[arrays->count] = parameter_type;
    *output_parameter_index = (uint32_t)arrays->count;
    arrays->count += 1U;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_binfunc_builder_add_alias_rule(
    binrwir_binfunc_builder_t *builder, const binrwir_alias_rule_t *rule,
    uint32_t *output_alias_rule_identifier) {
    if (output_alias_rule_identifier != NULL) {
        *output_alias_rule_identifier = BINRWIR_IDENTIFIER_NONE;
    }
    if (!binrwir_binfunc_builder_is_writable(builder) || rule == NULL ||
        output_alias_rule_identifier == NULL ||
        binrwir_signature_validate_alias_rule(rule) !=
            RUNTIMEC_STATUS_SUCCESS) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (builder->alias_rule_count >= UINT32_MAX) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    if (!binrwir_binfunc_builder_reserve_alias_rules(
            builder, builder->alias_rule_count + 1U)) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    builder->alias_rules[builder->alias_rule_count] = *rule;
    *output_alias_rule_identifier = (uint32_t)builder->alias_rule_count;
    builder->alias_rule_count += 1U;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_binfunc_builder_find_symbol(
    const binrwir_binfunc_builder_t *builder, runtimec_string_view_t name,
    uint16_t *output_symbol_index) {
    if (output_symbol_index != NULL) {
        *output_symbol_index = BINRWIR_SYMBOL_INDEX_NONE;
    }
    if (builder == NULL || output_symbol_index == NULL || name.size == 0U ||
        name.data == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    for (size_t ordinal = 0U; ordinal < builder->symbol_count; ++ordinal) {
        if (builder->symbols[ordinal].name.size == name.size &&
            memcmp(builder->symbols[ordinal].name.data, name.data,
                   name.size) == 0) {
            *output_symbol_index = (uint16_t)ordinal;
            return RUNTIMEC_STATUS_SUCCESS;
        }
    }
    return RUNTIMEC_STATUS_INVALID;
}

runtimec_status_t binrwir_binfunc_builder_add_symbol(
    binrwir_binfunc_builder_t *builder, runtimec_string_view_t name,
    runtimec_string_view_t langtype, uint16_t *output_symbol_index) {
    size_t ordinal;

    if (output_symbol_index != NULL) {
        *output_symbol_index = BINRWIR_SYMBOL_INDEX_NONE;
    }
    if (!binrwir_binfunc_builder_is_writable(builder) ||
        output_symbol_index == NULL || name.size == 0U || name.data == NULL ||
        !runtimec_string_view_is_valid(langtype) ||
        (langtype.size != 0U &&
         binrwir_signature_validate_parameter_type(langtype) !=
             RUNTIMEC_STATUS_SUCCESS)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    for (ordinal = 0U; ordinal < builder->symbol_count; ++ordinal) {
        if (builder->symbols[ordinal].name.size != name.size ||
            memcmp(builder->symbols[ordinal].name.data, name.data, name.size) !=
                0) {
            continue;
        }
        
        if (builder->symbols[ordinal].langtype.size != langtype.size ||
            memcmp(builder->symbols[ordinal].langtype.data, langtype.data,
                   langtype.size) != 0) {
            return RUNTIMEC_STATUS_INVALID;
        }
        *output_symbol_index = (uint16_t)ordinal;
        return RUNTIMEC_STATUS_SUCCESS;
    }
    if (builder->symbol_count >= (size_t)UINT16_MAX) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    if (!binrwir_binfunc_builder_reserve_symbols(builder,
                                                 builder->symbol_count + 1U)) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    builder->symbols[builder->symbol_count].name = name;
    builder->symbols[builder->symbol_count].langtype = langtype;
    *output_symbol_index = (uint16_t)builder->symbol_count;
    builder->symbol_count += 1U;
    return RUNTIMEC_STATUS_SUCCESS;
}

bool binrwir_literal_is_none_value(runtimec_bytes_view_t wire_bytes) {
    const kvspaceXvalueHead *head;
    uint64_t total;

    if (wire_bytes.size < KVSPACE_XVALUE_HEAD_LEN || wire_bytes.data == NULL) {
        return false;
    }
    head = (const kvspaceXvalueHead *)(const void *)wire_bytes.data;
    
    total = kvspaceXvalueHeadTotalLen(head);
    
    return total != 0U && total == (uint64_t)wire_bytes.size &&
           kvspaceXvalueHeadAttrsLen(head) == 0U &&
           runtimec_xvalue_body_from_wire(wire_bytes).size == 0U;
}

runtimec_status_t binrwir_binfunc_builder_add_literal(
    binrwir_binfunc_builder_t *builder, runtimec_bytes_view_t wire_bytes,
    runtimec_string_view_t langtype, uint32_t *output_literal_index) {
    if (output_literal_index != NULL) {
        *output_literal_index = BINRWIR_LITERAL_INDEX_NONE;
    }
    
    if (!binrwir_binfunc_builder_is_writable(builder) ||
        output_literal_index == NULL || wire_bytes.size == 0U ||
        wire_bytes.data == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (langtype.size == 0U) {
        if (!binrwir_literal_is_none_value(wire_bytes)) {
            return RUNTIMEC_STATUS_INVALID;
        }
    } else if (binrwir_signature_validate_parameter_type(langtype) !=
               RUNTIMEC_STATUS_SUCCESS) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (builder->literal_count >= UINT32_MAX) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    if (!binrwir_binfunc_builder_reserve_literals(
            builder, builder->literal_count + 1U)) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    builder->literals[builder->literal_count].wire_bytes = wire_bytes;
    builder->literals[builder->literal_count].langtype = langtype;
    *output_literal_index = (uint32_t)builder->literal_count;
    builder->literal_count += 1U;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_binfunc_builder_add_instruction(
    binrwir_binfunc_builder_t *builder,
    const binrwir_instruction_build_request_t *request,
    uint32_t *output_instruction_identifier) {
    size_t read_count;
    size_t write_count;
    size_t span_count;
    binrwir_binfunc_instruction_record_t *record;

    if (output_instruction_identifier != NULL) {
        *output_instruction_identifier = BINRWIR_IDENTIFIER_NONE;
    }
    if (!binrwir_binfunc_builder_is_writable(builder) || request == NULL ||
        output_instruction_identifier == NULL ||
        binrwir_instruction_validate_build_request(request) !=
            RUNTIMEC_STATUS_SUCCESS) {
        return RUNTIMEC_STATUS_INVALID;
    }
    read_count = request->read_arguments.count;
    write_count = request->write_arguments.count;
    span_count = read_count + write_count;
    if (builder->instructions.count >= UINT32_MAX ||
        span_count > UINT32_MAX ||
        builder->argument_count > UINT32_MAX - span_count) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    if (!binrwir_binfunc_builder_reserve_instructions(
            builder, builder->instructions.count + 1U) ||
        !binrwir_binfunc_builder_reserve_arguments(
            builder, builder->argument_count + span_count)) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    if (read_count != 0U) {
        memcpy(builder->arguments + builder->argument_count,
               request->read_arguments.data,
               sizeof(binrwir_argument_t) * read_count);
    }
    if (write_count != 0U) {
        memcpy(builder->arguments + builder->argument_count + read_count,
               request->write_arguments.data,
               sizeof(binrwir_argument_t) * write_count);
    }
    record = &builder->instructions.records[builder->instructions.count];
    record->source_logical_program_counter =
        request->source_logical_program_counter;
    record->read_argument_offset = (uint32_t)builder->argument_count;
    record->read_argument_count = (uint32_t)read_count;
    record->write_argument_count = (uint32_t)write_count;
    builder->instructions.heads[builder->instructions.count]
        .operation_identifier = request->operation_identifier;
    *output_instruction_identifier = (uint32_t)builder->instructions.count;
    builder->instructions.count += 1U;
    builder->argument_count += span_count;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_binfunc_builder_finalize(
    binrwir_binfunc_builder_t *builder, binrwir_binfunc_t **output_program) {
    binrwir_binfunc_t *program;
    size_t *symbol_order = NULL;
    size_t *symbol_remap = NULL;
    runtimec_status_t status;

    if (output_program != NULL) {
        *output_program = NULL;
    }
    if (!binrwir_binfunc_builder_is_writable(builder) ||
        output_program == NULL || builder->instructions.count == 0U) {
        return RUNTIMEC_STATUS_INVALID;
    }
    program = (binrwir_binfunc_t *)runtimec_allocator_allocate(
        &builder->allocator, sizeof(*program), _Alignof(binrwir_binfunc_t));
    if (program == NULL) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    memset(program, 0, sizeof(*program));
    program->allocator = builder->allocator;
    program->identity = builder->identity;
    status = binrwir_binfunc_allocate_storage(program, builder);
    if (status == RUNTIMEC_STATUS_SUCCESS && builder->symbol_count != 0U) {
        symbol_order = (size_t *)binrwir_binfunc_allocate_exact(
            &builder->allocator, builder->symbol_count * 2U, sizeof(size_t),
            _Alignof(size_t));
        if (symbol_order == NULL) {
            status = RUNTIMEC_STATUS_OUT_OF_MEMORY;
        } else {
            symbol_remap = symbol_order + builder->symbol_count;
        }
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        memcpy(program->instructions.heads, builder->instructions.heads,
               sizeof(binrwir_instruction_t) * builder->instructions.count);
        memcpy(program->instructions.records, builder->instructions.records,
               sizeof(binrwir_binfunc_instruction_record_t) *
                   builder->instructions.count);
        if (program->argument_count != 0U) {
            memcpy(program->arguments, builder->arguments,
                   sizeof(binrwir_argument_t) * program->argument_count);
        }
        if (program->read_parameters.count != 0U) {
            memcpy(program->read_parameters.argument_indices,
                   builder->read_parameters.argument_indices,
                   sizeof(uint32_t) * program->read_parameters.count);
        }
        if (program->write_parameters.count != 0U) {
            memcpy(program->write_parameters.argument_indices,
                   builder->write_parameters.argument_indices,
                   sizeof(uint32_t) * program->write_parameters.count);
        }
        if (program->alias_rule_count != 0U) {
            memcpy(program->alias_rules, builder->alias_rules,
                   sizeof(binrwir_alias_rule_t) * program->alias_rule_count);
        }
        binrwir_binfunc_copy_pools(program, builder, symbol_order,
                                   symbol_remap);
        status = binrwir_binfunc_validate(program);
    }
    if (symbol_order != NULL) {
        (void)runtimec_allocator_deallocate(
            &builder->allocator, symbol_order,
            sizeof(size_t) * builder->symbol_count * 2U, _Alignof(size_t));
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        binrwir_binfunc_release(program);
        (void)runtimec_allocator_deallocate(
            &builder->allocator, program, sizeof(*program),
            _Alignof(binrwir_binfunc_t));
        return status;
    }
    builder->finalized = true;
    *output_program = program;
    return RUNTIMEC_STATUS_SUCCESS;
}

bool binrwir_symbol_name_is_member(runtimec_string_view_t name) {
    
    static const char separator[] = "\xC2\xB7";
    if (name.data == NULL || name.size < 2U) {
        return false;
    }
    for (size_t offset = 0U; offset + 2U <= name.size; ++offset) {
        if ((uint8_t)name.data[offset] == (uint8_t)separator[0] &&
            (uint8_t)name.data[offset + 1U] == (uint8_t)separator[1]) {
            return true;
        }
    }
    return false;
}
