
#include "vmkv/encoder.h"

#include <stdio.h>
#include <string.h>

#include "contract/error.h"
#include "contract/pathspace_access.h"
#include "contract/view.h"
#include "vmkv/local_types.h"
#include "vmkv/parameter_type.h"
#include "vmkv/rwir_manifest.h"
#include "vmkv/vmkv_internal.h"

enum {
    VMKV_ENCODER_DIAG_INVALID_REQUEST = 20,
    VMKV_ENCODER_DIAG_HOST_CAPABILITY_MISSING = 21,
    
    VMKV_ENCODER_DIAG_INVALID_PARAMETER_TYPE = 23,
    VMKV_ENCODER_DIAG_UNION_PARAMETER = 24,
    VMKV_ENCODER_DIAG_UNMAPPED_PARAMETER_TYPE = 25,
    VMKV_ENCODER_DIAG_TYPE_SET_OUTSIDE_MASK = 26,
    VMKV_ENCODER_DIAG_CONSTRAINT_VIOLATION = 27,
    VMKV_ENCODER_DIAG_RESULT_TYPE_UNREPRESENTABLE = 28,
    VMKV_ENCODER_DIAG_DYNAMIC_KEY = 29,
    VMKV_ENCODER_DIAG_INVERTED_SPECIALIZATION = 30,
    VMKV_ENCODER_DIAG_ADDRESS_DERIVATION = 31,
    VMKV_ENCODER_DIAG_OUT_OF_MEMORY = 32,
    VMKV_ENCODER_DIAG_STALE_SNAPSHOT = 33,
    VMKV_ENCODER_DIAG_PROGRAM_REJECTED = 34,
    VMKV_ENCODER_DIAG_LOCAL_TYPE_UNKNOWN = 36
};

#define VMKV_ENCODER_CANONICAL_ENCODING_REVISION 1U
#define VMKV_ENCODER_PROGRAM_GENERATION 1U
#define VMKV_ENCODER_PROGRAM_COUNTER_CAPACITY 24U

struct vmkv_encoder_t {
    runtimec_allocator_t allocator;
    
    binrwir_argument_t *arguments;
    runtimec_string_view_t *argument_types;
    uint32_t *parameter_type_sets;
    size_t argument_capacity;
    
    binrwir_alias_rule_t *alias_rules;
    size_t alias_rule_count;
    size_t alias_rule_capacity;
};

typedef enum vmkv_encoder_operand_class {
    VMKV_ENCODER_OPERAND_CLASS_INVALID = 0,
    VMKV_ENCODER_OPERAND_CLASS_SIGNED_INTEGER = 1,
    VMKV_ENCODER_OPERAND_CLASS_UNSIGNED_INTEGER = 2,
    VMKV_ENCODER_OPERAND_CLASS_FLOAT = 3,
    VMKV_ENCODER_OPERAND_CLASS_BOOLEAN = 4,
    VMKV_ENCODER_OPERAND_CLASS_CHARACTER = 5,
    VMKV_ENCODER_OPERAND_CLASS_ARRAY = 6,
    VMKV_ENCODER_OPERAND_CLASS_STRING_KEY_MAP = 7,
    VMKV_ENCODER_OPERAND_CLASS_STRUCT_REFERENCE = 8,
    VMKV_ENCODER_OPERAND_CLASS_EXTENSION = 9,
    VMKV_ENCODER_OPERAND_CLASS_POINTER = 10,
    VMKV_ENCODER_OPERAND_CLASS_NONE = 11
} vmkv_encoder_operand_class_t;

static runtimec_status_t vmkv_encoder_fail(
    runtimec_error_t *error, runtimec_status_t status, int64_t code,
    const char *message, runtimec_program_counter_t logical_program_counter) {
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT, code,
                       runtimec_string_view_from_c_string(message),
                       logical_program_counter);
    return status;
}

static runtimec_program_counter_t vmkv_encoder_set_program_counter(
    vmkv_encoder_t *encoder, uint32_t line) {
    (void)encoder;
    return (runtimec_program_counter_t){
        RUNTIMEC_PROGRAM_COUNTER_STACK_IDENTIFIER_NONE, (int32_t)line};
}

static bool vmkv_encoder_request_is_valid(const vmkv_encode_request_t *request) {
    return request != NULL &&
           request->pathspace_access != NULL &&
           runtimec_string_view_is_valid(request->function_path) &&
           request->function_path.size != 0U &&
           runtimec_snapshot_is_valid(&request->expected_snapshot);
}

static void vmkv_encoder_release_buffers(vmkv_encoder_t *encoder) {
    if (encoder->arguments != NULL) {
        runtimec_allocator_deallocate(
            &encoder->allocator, encoder->arguments,
            encoder->argument_capacity * sizeof(*encoder->arguments),
            _Alignof(binrwir_argument_t));
        runtimec_allocator_deallocate(
            &encoder->allocator, encoder->argument_types,
            encoder->argument_capacity * sizeof(*encoder->argument_types),
            _Alignof(runtimec_string_view_t));
        runtimec_allocator_deallocate(
            &encoder->allocator, encoder->parameter_type_sets,
            encoder->argument_capacity * sizeof(*encoder->parameter_type_sets),
            _Alignof(uint32_t));
    }
    if (encoder->alias_rules != NULL) {
        runtimec_allocator_deallocate(
            &encoder->allocator, encoder->alias_rules,
            encoder->alias_rule_capacity * sizeof(*encoder->alias_rules),
            _Alignof(binrwir_alias_rule_t));
    }
    encoder->arguments = NULL;
    encoder->argument_types = NULL;
    encoder->parameter_type_sets = NULL;
    encoder->argument_capacity = 0U;
    encoder->alias_rules = NULL;
    encoder->alias_rule_count = 0U;
    encoder->alias_rule_capacity = 0U;
}

static runtimec_status_t vmkv_encoder_reserve(vmkv_encoder_t *encoder,
                                              size_t argument_count,
                                              runtimec_error_t *error) {
    if (argument_count <= encoder->argument_capacity) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    const size_t capacity = argument_count;
    binrwir_argument_t *arguments = runtimec_allocator_allocate(
        &encoder->allocator, capacity * sizeof(*arguments),
        _Alignof(binrwir_argument_t));
    runtimec_string_view_t *types = runtimec_allocator_allocate(
        &encoder->allocator, capacity * sizeof(*types),
        _Alignof(runtimec_string_view_t));
    uint32_t *type_sets = runtimec_allocator_allocate(
        &encoder->allocator, capacity * sizeof(*type_sets),
        _Alignof(uint32_t));
    if (arguments == NULL || types == NULL || type_sets == NULL) {
        runtimec_allocator_deallocate(&encoder->allocator, arguments,
                                      capacity * sizeof(*arguments),
                                      _Alignof(binrwir_argument_t));
        runtimec_allocator_deallocate(&encoder->allocator, types,
                                      capacity * sizeof(*types),
                                      _Alignof(runtimec_string_view_t));
        runtimec_allocator_deallocate(&encoder->allocator, type_sets,
                                      capacity * sizeof(*type_sets),
                                      _Alignof(uint32_t));
        return vmkv_encoder_fail(error, RUNTIMEC_STATUS_OUT_OF_MEMORY,
                                 VMKV_ENCODER_DIAG_OUT_OF_MEMORY,
                                 "vmkv encoder: allocation failed",
                                 RUNTIMEC_PROGRAM_COUNTER_NONE);
    }
    vmkv_encoder_release_buffers(encoder);
    encoder->arguments = arguments;
    encoder->argument_types = types;
    encoder->parameter_type_sets = type_sets;
    encoder->argument_capacity = capacity;
    return RUNTIMEC_STATUS_SUCCESS;
}

static vmkv_encoder_operand_class_t vmkv_encoder_operand_class(
    uint32_t type_set) {
    switch (type_set) {
        case VMKV_RWIR_TYPE_SET_INT8:
        case VMKV_RWIR_TYPE_SET_INT16:
        case VMKV_RWIR_TYPE_SET_INT32:
        case VMKV_RWIR_TYPE_SET_INT64:
            return VMKV_ENCODER_OPERAND_CLASS_SIGNED_INTEGER;
        case VMKV_RWIR_TYPE_SET_UINT8:
        case VMKV_RWIR_TYPE_SET_UINT16:
        case VMKV_RWIR_TYPE_SET_UINT32:
        case VMKV_RWIR_TYPE_SET_UINT64:
            return VMKV_ENCODER_OPERAND_CLASS_UNSIGNED_INTEGER;
        case VMKV_RWIR_TYPE_SET_FLOAT32:
        case VMKV_RWIR_TYPE_SET_FLOAT64:
            return VMKV_ENCODER_OPERAND_CLASS_FLOAT;
        case VMKV_RWIR_TYPE_SET_BOOLEAN:
            return VMKV_ENCODER_OPERAND_CLASS_BOOLEAN;
        case VMKV_RWIR_TYPE_SET_CHARACTER_UTF8:
        case VMKV_RWIR_TYPE_SET_CHARACTER_UTF32:
        case VMKV_RWIR_TYPE_SET_CHARACTER_ASCII:
            return VMKV_ENCODER_OPERAND_CLASS_CHARACTER;
        case VMKV_RWIR_TYPE_SET_ARRAY:
            return VMKV_ENCODER_OPERAND_CLASS_ARRAY;
        case VMKV_RWIR_TYPE_SET_STRING_KEY_MAP:
            return VMKV_ENCODER_OPERAND_CLASS_STRING_KEY_MAP;
        case VMKV_RWIR_TYPE_SET_STRUCT_REFERENCE:
            return VMKV_ENCODER_OPERAND_CLASS_STRUCT_REFERENCE;
        case VMKV_RWIR_TYPE_SET_EXTENSION:
            return VMKV_ENCODER_OPERAND_CLASS_EXTENSION;
        case VMKV_RWIR_TYPE_SET_POINTER:
            return VMKV_ENCODER_OPERAND_CLASS_POINTER;
        case VMKV_RWIR_TYPE_SET_NONE:
            return VMKV_ENCODER_OPERAND_CLASS_NONE;
        default:
            return VMKV_ENCODER_OPERAND_CLASS_INVALID;
    }
}

static bool vmkv_encoder_operand_class_is_numeric(
    vmkv_encoder_operand_class_t operand_class) {
    return operand_class == VMKV_ENCODER_OPERAND_CLASS_SIGNED_INTEGER ||
           operand_class == VMKV_ENCODER_OPERAND_CLASS_UNSIGNED_INTEGER ||
           operand_class == VMKV_ENCODER_OPERAND_CLASS_FLOAT;
}

static bool vmkv_encoder_constraint_holds(
    const vmkv_rwir_capability_t *capability, const uint32_t *type_sets,
    size_t count) {
    bool saw_signed = false;
    bool saw_unsigned = false;
    if (capability->constraint == RUNTIMEC_RWIR_CONSTRAINT_NONE) {
        return true;
    }
    for (size_t ordinal = 0U; ordinal < count; ++ordinal) {
        const vmkv_encoder_operand_class_t operand_class =
            vmkv_encoder_operand_class(type_sets[ordinal]);
        if (operand_class == VMKV_ENCODER_OPERAND_CLASS_INVALID) {
            return false;
        }
        saw_signed = saw_signed ||
                     operand_class == VMKV_ENCODER_OPERAND_CLASS_SIGNED_INTEGER;
        saw_unsigned =
            saw_unsigned ||
            operand_class == VMKV_ENCODER_OPERAND_CLASS_UNSIGNED_INTEGER;
    }
    switch (capability->constraint) {
        case RUNTIMEC_RWIR_CONSTRAINT_SAME_CLASS:
            for (size_t ordinal = 1U; ordinal < count; ++ordinal) {
                if (vmkv_encoder_operand_class(type_sets[ordinal]) !=
                    vmkv_encoder_operand_class(type_sets[0])) {
                    return false;
                }
            }
            return true;
        case RUNTIMEC_RWIR_CONSTRAINT_NUMERIC_COMPATIBLE:
            for (size_t ordinal = 0U; ordinal < count; ++ordinal) {
                if (!vmkv_encoder_operand_class_is_numeric(
                        vmkv_encoder_operand_class(type_sets[ordinal]))) {
                    return false;
                }
            }
            return !(saw_signed && saw_unsigned);
        case RUNTIMEC_RWIR_CONSTRAINT_NUMERIC_COMPARE:
            for (size_t ordinal = 0U; ordinal < count; ++ordinal) {
                if (!vmkv_encoder_operand_class_is_numeric(
                        vmkv_encoder_operand_class(type_sets[ordinal]))) {
                    return false;
                }
            }
            return true;
        default:
            return false;
    }
}

static unsigned vmkv_encoder_integer_width(uint32_t type_set) {
    switch (type_set) {
        case VMKV_RWIR_TYPE_SET_INT8:
        case VMKV_RWIR_TYPE_SET_UINT8:
            return 1U;
        case VMKV_RWIR_TYPE_SET_INT16:
        case VMKV_RWIR_TYPE_SET_UINT16:
            return 2U;
        case VMKV_RWIR_TYPE_SET_INT32:
        case VMKV_RWIR_TYPE_SET_UINT32:
            return 4U;
        case VMKV_RWIR_TYPE_SET_INT64:
        case VMKV_RWIR_TYPE_SET_UINT64:
            return 8U;
        default:
            return 0U;
    }
}

static bool vmkv_encoder_widen_type_set(const uint32_t *type_sets, size_t count,
                                        bool integers_only,
                                        uint32_t *output_type_set) {
    bool saw_signed = false;
    bool saw_unsigned = false;
    bool saw_float32 = false;
    bool saw_float64 = false;
    unsigned widest_width = 0U;
    uint32_t widest_type_set = VMKV_RWIR_TYPE_SET_NONE;
    if (count == 0U) {
        return false;
    }
    for (size_t ordinal = 0U; ordinal < count; ++ordinal) {
        const uint32_t type_set = type_sets[ordinal];
        const vmkv_encoder_operand_class_t operand_class =
            vmkv_encoder_operand_class(type_set);
        if (operand_class == VMKV_ENCODER_OPERAND_CLASS_SIGNED_INTEGER ||
            operand_class == VMKV_ENCODER_OPERAND_CLASS_UNSIGNED_INTEGER) {
            saw_signed = saw_signed ||
                         operand_class ==
                             VMKV_ENCODER_OPERAND_CLASS_SIGNED_INTEGER;
            saw_unsigned = saw_unsigned ||
                           operand_class ==
                               VMKV_ENCODER_OPERAND_CLASS_UNSIGNED_INTEGER;
            const unsigned width = vmkv_encoder_integer_width(type_set);
            if (width > widest_width) {
                widest_width = width;
                widest_type_set = type_set;
            }
        } else if (operand_class == VMKV_ENCODER_OPERAND_CLASS_FLOAT) {
            if (integers_only) {
                return false;
            }
            saw_float64 = saw_float64 || type_set == VMKV_RWIR_TYPE_SET_FLOAT64;
            saw_float32 = saw_float32 || type_set == VMKV_RWIR_TYPE_SET_FLOAT32;
        } else {
            return false;
        }
    }
    if (saw_float64) {
        *output_type_set = VMKV_RWIR_TYPE_SET_FLOAT64;
        return true;
    }
    if (saw_float32) {
        
        *output_type_set = (saw_signed || saw_unsigned)
                               ? VMKV_RWIR_TYPE_SET_FLOAT64
                               : VMKV_RWIR_TYPE_SET_FLOAT32;
        return true;
    }
    if (saw_signed && saw_unsigned) {
        return false;
    }
    if (widest_type_set == VMKV_RWIR_TYPE_SET_NONE) {
        return false;
    }
    *output_type_set = widest_type_set;
    return true;
}

static bool vmkv_encoder_result_rule_computes(
    runtimec_rwir_result_rule_t result_rule) {
    switch (result_rule) {
        case RUNTIMEC_RWIR_RESULT_SAME_FIRST:
        case RUNTIMEC_RWIR_RESULT_SAME_SECOND:
        case RUNTIMEC_RWIR_RESULT_WIDEN:
        case RUNTIMEC_RWIR_RESULT_WIDEN_INTEGER:
        case RUNTIMEC_RWIR_RESULT_BOOLEAN:
        case RUNTIMEC_RWIR_RESULT_FLOAT64:
        case RUNTIMEC_RWIR_RESULT_CHARACTER_UTF8:
            return true;
        default:
            return false;
    }
}

static bool vmkv_encoder_parse_parameter_reference(runtimec_key_view_t key,
                                                   uint16_t *output_ordinal,
                                                   bool *output_write) {
    *output_ordinal = 0U;
    *output_write = false;
    
    const size_t prefix = key.size >= 5U && key.data != NULL &&
                                  memcmp(key.data, "*[0,", 4U) == 0
                              ? 4U
                              : (key.size >= 4U && key.data != NULL &&
                                         memcmp(key.data, "[0,", 3U) == 0
                                     ? 3U
                                     : 0U);
    if (prefix == 0U || key.size < prefix + 2U) {
        return false;
    }
    size_t cursor = prefix;
    bool negative = false;
    if (key.data[cursor] == '-' || key.data[cursor] == '+') {
        negative = key.data[cursor] == '-';
        cursor += 1U;
    }
    const size_t digit_start = cursor;
    uint32_t ordinal = 0U;
    while (cursor < key.size && key.data[cursor] != ']') {
        if (key.data[cursor] < '0' || key.data[cursor] > '9') {
            return false;
        }
        ordinal = ordinal * 10U + (uint32_t)(key.data[cursor] - '0');
        if (ordinal == 0U || ordinal > UINT16_MAX) {
            return false;
        }
        cursor += 1U;
    }
    if (cursor == digit_start || cursor + 1U != key.size) {
        return false;
    }
    *output_ordinal = (uint16_t)ordinal;
    *output_write = !negative;
    return true;
}

static bool vmkv_encoder_derive_value_body_address(
    runtimec_xvalue_address_t xvalue_address,
    runtimec_value_body_address_t *output_body_address) {
    if (output_body_address == NULL) {
        return false;
    }
    *output_body_address = runtimec_xvalue_body_address(xvalue_address);
    return *output_body_address != RUNTIMEC_VALUE_BODY_ADDRESS_NONE;
}

runtimec_status_t vmkv_encoder_create(runtimec_allocator_t allocator,
                                      vmkv_encoder_t **output_encoder,
                                      runtimec_error_t *error) {
    if (output_encoder != NULL) {
        *output_encoder = NULL;
    }
    runtimec_error_reset(error);
    if (!runtimec_allocator_is_valid(&allocator) || output_encoder == NULL ||
        error == NULL) {
        return vmkv_encoder_fail(error, RUNTIMEC_STATUS_INVALID,
                                 VMKV_ENCODER_DIAG_INVALID_REQUEST,
                                 "vmkv encoder: invalid create request",
                                 RUNTIMEC_PROGRAM_COUNTER_NONE);
    }
    vmkv_encoder_t *encoder = runtimec_allocator_allocate(
        &allocator, sizeof(*encoder), _Alignof(vmkv_encoder_t));
    if (encoder == NULL) {
        return vmkv_encoder_fail(error, RUNTIMEC_STATUS_OUT_OF_MEMORY,
                                 VMKV_ENCODER_DIAG_OUT_OF_MEMORY,
                                 "vmkv encoder: allocation failed",
                                 RUNTIMEC_PROGRAM_COUNTER_NONE);
    }
    memset(encoder, 0, sizeof(*encoder));
    encoder->allocator = allocator;
    *output_encoder = encoder;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmkv_encoder_destroy(vmkv_encoder_t *encoder) {
    if (encoder == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    vmkv_encoder_release_buffers(encoder);
    const runtimec_allocator_t allocator = encoder->allocator;
    memset(encoder, 0, sizeof(*encoder));
    runtimec_allocator_deallocate(&allocator, encoder, sizeof(*encoder),
                                  _Alignof(vmkv_encoder_t));
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmkv_encoder_get_canonical_encoding_revision(
    const vmkv_encoder_t *encoder,
    uint32_t *output_canonical_encoding_revision) {
    if (output_canonical_encoding_revision != NULL) {
        *output_canonical_encoding_revision = UINT32_MAX;
    }
    if (encoder == NULL || output_canonical_encoding_revision == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    *output_canonical_encoding_revision =
        VMKV_ENCODER_CANONICAL_ENCODING_REVISION;
    return RUNTIMEC_STATUS_SUCCESS;
}

static bool vmkv_encoder_strip_library_prefix(runtimec_string_view_t *name) {
    static const char prefix[] = "/lib/";
    const size_t prefix_size = sizeof(prefix) - 1U;
    if (name->size < prefix_size || name->data == NULL ||
        memcmp(name->data, prefix, prefix_size) != 0) {
        return false;
    }
    name->data += prefix_size;
    name->size -= prefix_size;
    return true;
}

static runtimec_status_t vmkv_encoder_register_symbol(
    binrwir_binfunc_builder_t *builder, runtimec_string_view_t name,
    runtimec_string_view_t langtype, uint16_t *output_symbol_index,
    runtimec_program_counter_t logical_program_counter, runtimec_error_t *error) {
    const runtimec_status_t status = binrwir_binfunc_builder_add_symbol(
        builder, name, langtype, output_symbol_index);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    return vmkv_encoder_fail(error, status,
                             VMKV_ENCODER_DIAG_PROGRAM_REJECTED,
                             "vmkv encoder: program builder rejected a symbol",
                             logical_program_counter);
}

static runtimec_status_t vmkv_encode_static_argument_into(
    const vmkv_encode_request_t *request, binrwir_binfunc_builder_t *builder,
    const vmkv_rwir_argument_view_t *source_argument,
    uint32_t argument_index, binrwir_argument_t *output_argument,
    runtimec_error_t *error) {
    if (output_argument != NULL) {
        memset(output_argument, 0, sizeof(*output_argument));
    }
    
    if (!vmkv_encoder_request_is_valid(request) || output_argument == NULL ||
        builder == NULL || argument_index == BINRWIR_ARGUMENT_INDEX_NONE ||
        source_argument == NULL ||
        !runtimec_string_view_is_valid(source_argument->parameter_type_name) ||
        !runtimec_key_view_is_valid(source_argument->key) ||
        source_argument->key.size == 0U) {
        return vmkv_encoder_fail(error, RUNTIMEC_STATUS_INVALID,
                                 VMKV_ENCODER_DIAG_INVALID_REQUEST,
                                 "vmkv encoder: invalid static argument request",
                                 RUNTIMEC_PROGRAM_COUNTER_NONE);
    }
    if (source_argument->key_addressability == VMKV_KEY_ADDRESSABILITY_DYNAMIC) {
        return vmkv_encoder_fail(error, RUNTIMEC_STATUS_UNSUPPORTED,
                                 VMKV_ENCODER_DIAG_DYNAMIC_KEY,
                                 "vmkv encoder: dynamic key is unsupported",
                                 RUNTIMEC_PROGRAM_COUNTER_NONE);
    }
    runtimec_xvalue_address_t xvalue_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
    runtimec_status_t status =
        runtimec_pathspace_access_resolve_static_xvalue_address(
            request->pathspace_access, &request->expected_snapshot,
            source_argument->key, &xvalue_address, error);
    
    const bool unresolved = status == RUNTIMEC_STATUS_UNSUPPORTED;
    if (status != RUNTIMEC_STATUS_SUCCESS && !unresolved) {
        return status;
    }
    runtimec_value_body_address_t body_address =
        RUNTIMEC_VALUE_BODY_ADDRESS_NONE;
    if (!unresolved &&
        !vmkv_encoder_derive_value_body_address(xvalue_address,
                                                &body_address)) {
        return vmkv_encoder_fail(error, RUNTIMEC_STATUS_ERROR,
                                 VMKV_ENCODER_DIAG_ADDRESS_DERIVATION,
                                 "vmkv encoder: xvalue base is not addressable",
                                 RUNTIMEC_PROGRAM_COUNTER_NONE);
    }
    uint16_t symbol_index = BINRWIR_SYMBOL_INDEX_NONE;
    status = vmkv_encoder_register_symbol(
        builder, runtimec_string_view_make(source_argument->key.data,
                                          source_argument->key.size),
        source_argument->parameter_type_name, &symbol_index,
        RUNTIMEC_PROGRAM_COUNTER_NONE, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    output_argument->argument_index = argument_index;
    output_argument->reference_kind = (uint8_t)BINRWIR_REFERENCE_STATIC;
    output_argument->dereference = 0U;
    output_argument->slot = symbol_index;
    output_argument->payload = body_address;
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t vmkv_encoder_take_operand_argument_index(
    vmkv_encode_function_t *function, uint32_t *output,
    runtimec_program_counter_t logical_program_counter, runtimec_error_t *error) {
    if (function->next_operand_argument_index ==
        BINRWIR_ARGUMENT_INDEX_NONE - 1U) {
        return vmkv_encoder_fail(
            error, RUNTIMEC_STATUS_ERROR, VMKV_ENCODER_DIAG_OUT_OF_MEMORY,
            "vmkv encoder: operand argument index space exhausted",
            logical_program_counter);
    }
    *output = function->next_operand_argument_index;
    function->next_operand_argument_index += 1U;
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t vmkv_encoder_dynamic_member_fail(
    runtimec_error_t *error, runtimec_key_view_t key,
    runtimec_program_counter_t logical_program_counter) {
    static _Thread_local char message[512];
    static const char prefix[] =
        "vmkv encoder: dynamic member key '";
    static const char suffix[] =
        "' is not representable in BinRWIR; the key must be evaluated at run "
        "time, which no reference kind models yet";
    const size_t prefix_size = sizeof(prefix) - 1U;
    const size_t suffix_size = sizeof(suffix) - 1U;
    const size_t key_size =
        key.size < sizeof(message) - prefix_size - suffix_size - 1U
            ? key.size
            : sizeof(message) - prefix_size - suffix_size - 1U;

    memcpy(message, prefix, prefix_size);
    memcpy(message + prefix_size, key.data, key_size);
    memcpy(message + prefix_size + key_size, suffix, suffix_size);
    message[prefix_size + key_size + suffix_size] = '\0';
    return vmkv_encoder_fail(error, RUNTIMEC_STATUS_UNSUPPORTED,
                             VMKV_ENCODER_DIAG_DYNAMIC_KEY, message,
                             logical_program_counter);
}

static bool vmkv_encoder_langtype_is(runtimec_string_view_t langtype,
                                     const char *name) {
    if (langtype.data == NULL) {
        return false;
    }
    size_t start = 0U;
    while (start < langtype.size && langtype.data[start] == '[') {
        while (start < langtype.size && langtype.data[start] != ']') {
            ++start;
        }
        if (start < langtype.size) {
            ++start;
        }
    }
    const size_t name_size = strlen(name);
    if (langtype.size - start != name_size) {
        return false;
    }
    return memcmp(langtype.data + start, name, name_size) == 0;
}

static bool vmkv_encoder_literal_is_character_text(
    runtimec_string_view_t langtype) {
    if (langtype.data == NULL || langtype.size < 4U) {
        return false;
    }
    size_t start = 0U;
    
    while (start < langtype.size && langtype.data[start] == '[') {
        while (start < langtype.size && langtype.data[start] != ']') {
            ++start;
        }
        if (start < langtype.size) {
            ++start; 
        }
    }
    if (langtype.size - start < 4U) {
        return false;
    }
    return memcmp(langtype.data + start, "char", 4U) == 0;
}

static bool vmkv_encoder_parameter_role_is_key(runtimec_string_view_t roles,
                                               size_t ordinal) {
    if (roles.data == NULL || roles.size == 0U) {
        return false;
    }
    size_t position = 0U;
    size_t cursor = 0U;
    while (cursor <= roles.size) {
        const size_t start = cursor;
        while (cursor < roles.size && roles.data[cursor] != ',') {
            ++cursor;
        }
        if (position == ordinal) {
            const size_t length = cursor - start;
            
            return length == 3U && roles.data[start] == 'k' &&
                   roles.data[start + 1U] == 'e' && roles.data[start + 2U] == 'y';
        }
        ++position;
        if (cursor >= roles.size) {
            break;
        }
        ++cursor; 
    }
    return false;
}

static runtimec_status_t vmkv_encoder_map_argument(
    const vmkv_encode_request_t *request, binrwir_binfunc_builder_t *builder,
    const vmkv_local_types_t *local_types, vmkv_encode_function_t *function,
    const vmkv_rwir_argument_view_t *source_argument, bool function_target,
    bool parameter_is_key, binrwir_argument_t *output,
    runtimec_program_counter_t logical_program_counter,
    runtimec_error_t *error) {
    *output = (binrwir_argument_t){0};
    
    vmkv_rwir_argument_view_t dereferenced_view = *source_argument;
    bool has_dereference = false;
    if (dereferenced_view.key.size >= 2U &&
        dereferenced_view.key.data != NULL &&
        dereferenced_view.key.data[0] == '*') {
        if (dereferenced_view.key.data[1] == '*') {
            return vmkv_encoder_fail(
                error, RUNTIMEC_STATUS_UNSUPPORTED,
                VMKV_ENCODER_DIAG_DYNAMIC_KEY,
                "vmkv encoder: multi-level pointer following is not "
                "representable",
                logical_program_counter);
        }
        dereferenced_view.key = runtimec_key_view_make(
            dereferenced_view.key.data + 1U,
            dereferenced_view.key.size - 1U);
        has_dereference = true;
    }
    source_argument = &dereferenced_view;
    uint32_t argument_index = 0U;
    runtimec_status_t status = vmkv_encoder_take_operand_argument_index(
        function, &argument_index, logical_program_counter, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    
    if (parameter_is_key && source_argument->literal.bytes.data != NULL &&
        vmkv_encoder_literal_is_character_text(
            source_argument->literal.langtype)) {

        char key_text[1024];
        const uint64_t wire_size = (uint64_t)source_argument->literal.bytes.size;
        if (wire_size < PATHSPACE_XVALUE_HEAD_LEN) {
            return vmkv_encoder_fail(
                error, RUNTIMEC_STATUS_INVALID, VMKV_ENCODER_DIAG_INVALID_REQUEST,
                "vmkv encoder: key-position literal wire is truncated",
                logical_program_counter);
        }
        const pathspaceXvalueHead *literal_head =
            (const pathspaceXvalueHead *)(const void *)source_argument->literal.bytes.data;
        const uint8_t *text_bytes =
            source_argument->literal.bytes.data + PATHSPACE_XVALUE_HEAD_LEN;
        const size_t text_size = (size_t)pathspaceXvalueHeadLen(literal_head);
        if (text_size == 0U || text_size >= sizeof(key_text) ||
            PATHSPACE_XVALUE_HEAD_LEN + text_size > wire_size) {
            return vmkv_encoder_fail(
                error, RUNTIMEC_STATUS_INVALID, VMKV_ENCODER_DIAG_INVALID_REQUEST,
                "vmkv encoder: key-position literal is empty or too long",
                logical_program_counter);
        }
        
        size_t written = 0U;
        if (vmkv_encoder_langtype_is(source_argument->literal.langtype,
                                     "char/utf32")) {
            if (text_size % 4U != 0U) {
                return vmkv_encoder_fail(
                    error, RUNTIMEC_STATUS_INVALID,
                    VMKV_ENCODER_DIAG_INVALID_REQUEST,
                    "vmkv encoder: key literal is not whole utf32 code points",
                    logical_program_counter);
            }
            for (size_t at = 0U; at < text_size; at += 4U) {
                const uint32_t code_point =
                    (uint32_t)text_bytes[at] |
                    ((uint32_t)text_bytes[at + 1U] << 8U) |
                    ((uint32_t)text_bytes[at + 2U] << 16U) |
                    ((uint32_t)text_bytes[at + 3U] << 24U);
                
                if (code_point == 0U || code_point > 0x7FU) {
                    return vmkv_encoder_fail(
                        error, RUNTIMEC_STATUS_UNSUPPORTED,
                        VMKV_ENCODER_DIAG_DYNAMIC_KEY,
                        "vmkv encoder: non-ASCII key literal is not representable",
                        logical_program_counter);
                }
                key_text[written] = (char)code_point;
                ++written;
            }
        } else {
            memcpy(key_text, text_bytes, text_size);
            written = text_size;
        }
        
        uint16_t existing_symbol = BINRWIR_SYMBOL_INDEX_NONE;
        if (binrwir_binfunc_builder_find_symbol(
                builder, runtimec_string_view_make(key_text, written),
                &existing_symbol) == RUNTIMEC_STATUS_SUCCESS) {
            output->argument_index = argument_index;
            output->reference_kind = (uint8_t)BINRWIR_REFERENCE_STATIC;
            output->dereference = has_dereference ? 1U : 0U;
            output->slot = existing_symbol;
            output->payload = RUNTIMEC_VALUE_BODY_ADDRESS_NONE;
            return RUNTIMEC_STATUS_SUCCESS;
        }
        return vmkv_encode_static_argument_into(
            request, builder,
            &(vmkv_rwir_argument_view_t){
                .key = runtimec_key_view_make(key_text, written),
                .key_addressability = VMKV_KEY_ADDRESSABILITY_STATIC,
                .parameter_type_name = source_argument->parameter_type_name,
            },
            argument_index, output, error);
    }
    if (source_argument->literal.bytes.data != NULL) {
        uint32_t literal_index = BINRWIR_LITERAL_INDEX_NONE;
        status = binrwir_binfunc_builder_add_literal(
            builder, source_argument->literal.bytes,
            source_argument->literal.langtype, &literal_index);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return vmkv_encoder_fail(
                error, status, VMKV_ENCODER_DIAG_PROGRAM_REJECTED,
                "vmkv encoder: program builder rejected a literal",
                logical_program_counter);
        }
        output->argument_index = argument_index;
        output->reference_kind = (uint8_t)BINRWIR_REFERENCE_LITERAL;
        output->payload = literal_index;
        return RUNTIMEC_STATUS_SUCCESS;
    }
    if (source_argument->key_addressability == VMKV_KEY_ADDRESSABILITY_STATIC) {
        status = vmkv_encode_static_argument_into(
            request, builder, source_argument, argument_index, output, error);
        if (status == RUNTIMEC_STATUS_SUCCESS && has_dereference) {
            output->dereference = 1U;
        }
        return status;
    }
    uint16_t parameter_ordinal = 0U;
    bool writes_parameter = false;
    
    if (source_argument->key_addressability == VMKV_KEY_ADDRESSABILITY_DYNAMIC &&
        vmkv_encoder_parse_parameter_reference(
            source_argument->key, &parameter_ordinal, &writes_parameter) &&
        (writes_parameter
             ? parameter_ordinal <= function->write_parameter_count
             : parameter_ordinal <= function->read_parameter_count)) {
        
        function->next_operand_argument_index -= 1U;
        output->slot = (uint16_t)(parameter_ordinal - 1U);
        output->argument_index =
            writes_parameter
                ? function->read_parameter_count + parameter_ordinal - 1U
                : parameter_ordinal - 1U;
        output->reference_kind =
            (uint8_t)(writes_parameter ? BINRWIR_REFERENCE_PARAMETER_WRITE
                                       : BINRWIR_REFERENCE_PARAMETER_READ);
        
        output->dereference = 0U;
        return RUNTIMEC_STATUS_SUCCESS;
    }
    
    if (source_argument->key.size != 0U && source_argument->key.data[0] == '[') {
        return vmkv_encoder_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, VMKV_ENCODER_DIAG_DYNAMIC_KEY,
            "vmkv encoder: framed slot expression is not representable",
            logical_program_counter);
    }
    
    if (vmkv_rwir_key_is_dynamic_member(source_argument->key)) {
        return vmkv_encoder_dynamic_member_fail(
            error, source_argument->key, logical_program_counter);
    }
    
    const runtimec_string_view_t name = runtimec_string_view_make(
        source_argument->key.data, source_argument->key.size);
    runtimec_string_view_t symbol_type =
        source_argument->parameter_type_name;
    
    if (!function_target && !vmkv_rwir_key_is_member(source_argument->key)) {
        if (local_types == NULL ||
            !vmkv_local_types_find(local_types, name, &symbol_type)) {
            return vmkv_encoder_fail(
                error, RUNTIMEC_STATUS_INVALID,
                VMKV_ENCODER_DIAG_LOCAL_TYPE_UNKNOWN,
                "vmkv encoder: local symbol has no unique write type",
                logical_program_counter);
        }
    }
    uint16_t symbol_index = BINRWIR_SYMBOL_INDEX_NONE;
    status = vmkv_encoder_register_symbol(builder, name, symbol_type,
                                          &symbol_index,
                                          logical_program_counter, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    output->argument_index = argument_index;
    output->reference_kind = (uint8_t)(function_target
                                           ? BINRWIR_REFERENCE_FUNCTION
                                           : BINRWIR_REFERENCE_LOCAL);
    output->slot = symbol_index;
    output->payload = 0U;
    if (has_dereference) {
        output->dereference = 1U;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmkv_encode_static_argument(
    const vmkv_encode_request_t *request, binrwir_binfunc_builder_t *builder,
    const vmkv_rwir_argument_view_t *source_argument,
    uint32_t argument_index, binrwir_argument_t *output_argument,
    runtimec_error_t *error) {
    return vmkv_encode_static_argument_into(request, builder, source_argument,
                                            argument_index, output_argument,
                                            error);
}

typedef struct vmkv_encoder_operation {
    const vmkv_rwir_capability_t *capability;
    const vmkv_rwir_instruction_view_t *source;
    runtimec_program_counter_t logical_program_counter;
} vmkv_encoder_operation_t;

static bool vmkv_encoder_requires_classification(
    const vmkv_rwir_capability_t *capability) {
    switch (capability->constraint) {
        case RUNTIMEC_RWIR_CONSTRAINT_SAME_CLASS:
        case RUNTIMEC_RWIR_CONSTRAINT_NUMERIC_COMPATIBLE:
        case RUNTIMEC_RWIR_CONSTRAINT_NUMERIC_COMPARE:
            return true;
        default:
            break;
    }
    switch (capability->result_rule) {
        case RUNTIMEC_RWIR_RESULT_SAME_FIRST:
        case RUNTIMEC_RWIR_RESULT_SAME_SECOND:
        case RUNTIMEC_RWIR_RESULT_WIDEN:
        case RUNTIMEC_RWIR_RESULT_WIDEN_INTEGER:
        case RUNTIMEC_RWIR_RESULT_BOOLEAN:
        case RUNTIMEC_RWIR_RESULT_FLOAT64:
        case RUNTIMEC_RWIR_RESULT_CHARACTER_UTF8:
        case RUNTIMEC_RWIR_RESULT_TARGET_TYPE:
            return true;
        default:
            return false;
    }
}

static runtimec_status_t vmkv_encoder_classify_read_arguments(
    vmkv_encoder_t *encoder, vmkv_encoder_operation_t *operation,
    bool function_target, runtimec_error_t *error) {
    const vmkv_rwir_instruction_view_t *source = operation->source;
    (void)function_target;
    const bool needs_classification =
        vmkv_encoder_requires_classification(operation->capability);
    for (size_t ordinal = 0U; ordinal < source->read_argument_count; ++ordinal) {
        const runtimec_string_view_t declared =
            source->read_arguments[ordinal].parameter_type_name;
        uint32_t type_set = VMKV_RWIR_TYPE_SET_NONE;
        const vmkv_parameter_type_result_t result =
            vmkv_parameter_type_classify(declared, &type_set);
        
        if (result == VMKV_PARAMETER_TYPE_RESULT_NONE &&
            vmkv_encoder_requires_classification(operation->capability) &&
            operation->capability->read_parameter_type_set ==
                VMKV_RWIR_TYPE_SET_ANY_SINGLE) {
            encoder->parameter_type_sets[ordinal] = VMKV_RWIR_TYPE_SET_NONE;
            continue;
        }
        
        if (result == VMKV_PARAMETER_TYPE_RESULT_UNION) {
            return vmkv_encoder_fail(
                error, RUNTIMEC_STATUS_UNSUPPORTED,
                VMKV_ENCODER_DIAG_UNION_PARAMETER,
                "vmkv encoder: parameter has no concrete langtype",
                operation->logical_program_counter);
        }
        
        if (result == VMKV_PARAMETER_TYPE_RESULT_NONE &&
            source->read_argument_count == 1U &&
            source->read_arguments[ordinal].key_addressability ==
                VMKV_KEY_ADDRESSABILITY_STATIC) {
            encoder->parameter_type_sets[ordinal] = VMKV_RWIR_TYPE_SET_NONE;
            continue;
        }
        if (!needs_classification) {
            
            if (result == VMKV_PARAMETER_TYPE_RESULT_NONE) {
                return vmkv_encoder_fail(
                    error, RUNTIMEC_STATUS_ERROR,
                    VMKV_ENCODER_DIAG_INVALID_PARAMETER_TYPE,
                    "vmkv encoder: parameter has no langtype at all",
                    operation->logical_program_counter);
            }
            encoder->parameter_type_sets[ordinal] = VMKV_RWIR_TYPE_SET_NONE;
            continue;
        }
        if (result == VMKV_PARAMETER_TYPE_RESULT_UNMAPPED) {
            return vmkv_encoder_fail(
                error, RUNTIMEC_STATUS_UNSUPPORTED,
                VMKV_ENCODER_DIAG_UNMAPPED_PARAMETER_TYPE,
                "vmkv encoder: parameter type is outside the category set and "
                "this operation needs it to compute a result",
                operation->logical_program_counter);
        }
        if (result != VMKV_PARAMETER_TYPE_RESULT_SINGLE) {
            return vmkv_encoder_fail(
                error, RUNTIMEC_STATUS_ERROR,
                VMKV_ENCODER_DIAG_INVALID_PARAMETER_TYPE,
                "vmkv encoder: parameter type is not a single concrete langtype",
                operation->logical_program_counter);
        }
        encoder->parameter_type_sets[ordinal] = type_set;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t vmkv_encoder_resolve_write_types(
    const vmkv_encoder_t *encoder, vmkv_encoder_operation_t *operation,
    runtimec_string_view_t *targets, runtimec_error_t *error) {
    const vmkv_rwir_instruction_view_t *source = operation->source;
    const runtimec_rwir_result_rule_t result_rule =
        operation->capability->result_rule;
    runtimec_string_view_t computed_type = runtimec_string_view_make(NULL, 0U);
    if (vmkv_encoder_result_rule_computes(result_rule)) {
        if (source->write_argument_count != 1U) {
            return vmkv_encoder_fail(
                error, RUNTIMEC_STATUS_ERROR,
                VMKV_ENCODER_DIAG_RESULT_TYPE_UNREPRESENTABLE,
                "vmkv encoder: computed result requires one write parameter",
                operation->logical_program_counter);
        }
        switch (result_rule) {
            case RUNTIMEC_RWIR_RESULT_SAME_FIRST:
                if (source->read_argument_count < 1U) {
                    return vmkv_encoder_fail(
                        error, RUNTIMEC_STATUS_ERROR,
                        VMKV_ENCODER_DIAG_RESULT_TYPE_UNREPRESENTABLE,
                        "vmkv encoder: result rule needs a first read parameter",
                        operation->logical_program_counter);
                }
                computed_type = source->read_arguments[0].parameter_type_name;
                break;
            case RUNTIMEC_RWIR_RESULT_SAME_SECOND:
                if (source->read_argument_count < 2U) {
                    return vmkv_encoder_fail(
                        error, RUNTIMEC_STATUS_ERROR,
                        VMKV_ENCODER_DIAG_RESULT_TYPE_UNREPRESENTABLE,
                        "vmkv encoder: result rule needs a second read parameter",
                        operation->logical_program_counter);
                }
                computed_type = source->read_arguments[1].parameter_type_name;
                break;
            case RUNTIMEC_RWIR_RESULT_WIDEN:
            case RUNTIMEC_RWIR_RESULT_WIDEN_INTEGER: {
                uint32_t result_type_set = VMKV_RWIR_TYPE_SET_NONE;
                if (!vmkv_encoder_widen_type_set(
                        encoder->parameter_type_sets,
                        source->read_argument_count,
                        result_rule == RUNTIMEC_RWIR_RESULT_WIDEN_INTEGER,
                        &result_type_set)) {
                    return vmkv_encoder_fail(
                        error, RUNTIMEC_STATUS_ERROR,
                        VMKV_ENCODER_DIAG_RESULT_TYPE_UNREPRESENTABLE,
                        "vmkv encoder: widened result type is not decidable",
                        operation->logical_program_counter);
                }
                computed_type = vmkv_parameter_type_name(result_type_set);
                break;
            }
            case RUNTIMEC_RWIR_RESULT_BOOLEAN:
                computed_type = runtimec_string_view_from_c_string("bool");
                break;
            case RUNTIMEC_RWIR_RESULT_FLOAT64:
                computed_type = runtimec_string_view_from_c_string("float64");
                break;
            case RUNTIMEC_RWIR_RESULT_CHARACTER_UTF8:
                computed_type = runtimec_string_view_from_c_string("char/utf8");
                break;
            default:
                return vmkv_encoder_fail(
                    error, RUNTIMEC_STATUS_ERROR,
                    VMKV_ENCODER_DIAG_RESULT_TYPE_UNREPRESENTABLE,
                    "vmkv encoder: result rule is not computable",
                    operation->logical_program_counter);
        }
        if (!runtimec_string_view_is_valid(computed_type) ||
            computed_type.size == 0U) {
            return vmkv_encoder_fail(
                error, RUNTIMEC_STATUS_ERROR,
                VMKV_ENCODER_DIAG_RESULT_TYPE_UNREPRESENTABLE,
                "vmkv encoder: result type is not decidable",
                operation->logical_program_counter);
        }
        for (size_t ordinal = 0U; ordinal < source->write_argument_count;
             ++ordinal) {
            targets[ordinal] = computed_type;
        }
        return RUNTIMEC_STATUS_SUCCESS;
    }
    
    for (size_t ordinal = 0U; ordinal < source->write_argument_count;
         ++ordinal) {
        const runtimec_string_view_t declared =
            source->write_arguments[ordinal].parameter_type_name;
        
        if (declared.size == 0U || declared.data == NULL ||
            memchr(declared.data, '|', declared.size) != NULL) {
            return vmkv_encoder_fail(
                error, RUNTIMEC_STATUS_UNSUPPORTED,
                VMKV_ENCODER_DIAG_INVALID_PARAMETER_TYPE,
                "vmkv encoder: write parameter has no concrete langtype; "
                "layout must declare one (missing type is a rule violation, "
                "not an exemptible case)",
                operation->logical_program_counter);
        }
        targets[ordinal] = declared;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static int vmkv_encoder_numeric_reserved_identifier(
    runtimec_string_view_t name,
    binrwir_operation_identifier_t *output_identifier) {
    *output_identifier = 0U;
    if (name.data == NULL || name.size == 0U || name.size > 5U) {
        return 0;
    }
    uint32_t value = 0U;
    for (size_t at = 0U; at < name.size; ++at) {
        
        if (name.data[at] < '0' || name.data[at] > '9') {
            return 0;
        }
        value = value * 10U + (uint32_t)(name.data[at] - '0');
        if (value > UINT16_MAX) {
            return 0;
        }
    }
    
    if (value < BINRWIR_OPERATION_IDENTIFIER_RESERVED_BASE) {
        return 0;
    }
    *output_identifier = (binrwir_operation_identifier_t)value;
    return 1;
}

static runtimec_status_t vmkv_encoder_encode_temporary_output(
    vmkv_encoder_t *encoder, const vmkv_encode_request_t *request,
    binrwir_binfunc_builder_t *builder, const vmkv_local_types_t *local_types,
    vmkv_encode_function_t *function,
    const vmkv_rwir_instruction_view_t *source_instruction,
    binrwir_operation_identifier_t operation_identifier,
    runtimec_program_counter_t logical_program_counter,
    vmkv_canonical_instruction_t *output_instruction,
    runtimec_error_t *error) {
    const size_t read_count = source_instruction->read_argument_count;
    runtimec_status_t status;

    if (read_count != 0U && source_instruction->read_arguments == NULL) {
        return vmkv_encoder_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMKV_ENCODER_DIAG_INVALID_REQUEST,
            "vmkv encoder: read argument span is null", logical_program_counter);
    }
    status = vmkv_encoder_reserve(encoder, read_count == 0U ? 1U : read_count,
                                  error);
    (void)builder;
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    for (size_t ordinal = 0U; ordinal < read_count; ++ordinal) {
        const vmkv_rwir_argument_view_t *source_argument =
            &source_instruction->read_arguments[ordinal];
        binrwir_argument_t *target = &encoder->arguments[ordinal];

        if (source_argument->literal.bytes.data != NULL) {
            uint32_t literal_index = BINRWIR_LITERAL_INDEX_NONE;

            status = binrwir_binfunc_builder_add_literal(
                builder, source_argument->literal.bytes,
                source_argument->literal.langtype, &literal_index);
            if (status != RUNTIMEC_STATUS_SUCCESS) {
                return vmkv_encoder_fail(
                    error, status, VMKV_ENCODER_DIAG_PROGRAM_REJECTED,
                    "vmkv encoder: program builder rejected a literal",
                    logical_program_counter);
            }
            uint32_t argument_index = 0U;
            status = vmkv_encoder_take_operand_argument_index(
                function, &argument_index, logical_program_counter, error);
            if (status != RUNTIMEC_STATUS_SUCCESS) {
                return status;
            }
            *target = (binrwir_argument_t){0};
            target->argument_index = argument_index;
            target->reference_kind = (uint8_t)BINRWIR_REFERENCE_LITERAL;
            target->payload = literal_index;
            continue;
        }
        
        status = vmkv_encoder_map_argument(
            request, builder, local_types, function, source_argument, false,
            false, target, logical_program_counter, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
    }
    output_instruction->operation_identifier = operation_identifier;
    output_instruction->logical_program_counter =
        source_instruction->logical_program_counter;
    output_instruction->read_arguments =
        (binrwir_argument_span_t){encoder->arguments, read_count};
    output_instruction->write_arguments = (binrwir_argument_span_t){0};
    output_instruction->read_parameter_types = NULL;
    output_instruction->write_parameter_types = NULL;
    output_instruction->aliasing_declared = false;
    return RUNTIMEC_STATUS_SUCCESS;
}

static bool vmkv_encoder_view_equals(runtimec_string_view_t view,
                                     const char *text) {
    const size_t size = strlen(text);

    return view.size == size && view.data != NULL &&
           memcmp(view.data, text, size) == 0;
}

static bool vmkv_encoder_operand_is_none(
    const vmkv_rwir_argument_view_t *argument) {
    if (argument == NULL) {
        return false;
    }
    
    if (argument->parameter_type_name.size != 0U ||
        argument->literal.langtype.size != 0U) {
        return false;
    }
    if (argument->literal.bytes.size == 0U) {
        return true; 
    }
    if (argument->literal.bytes.size < PATHSPACE_XVALUE_HEAD_LEN ||
        argument->literal.bytes.data == NULL) {
        return false;
    }
    {
        pathspaceXvalueHead head;

        memcpy(&head, argument->literal.bytes.data, sizeof head);
        return pathspaceXvalueHeadLen(&head) == 0U &&
               pathspaceXvalueHeadAttrsLen(&head) == 0U;
    }
}

static bool vmkv_encoder_type_is_reference(runtimec_string_view_t type_name) {
    if (type_name.size == 0U || type_name.data == NULL) {
        return false;
    }
    return type_name.data[0] == '*' || type_name.data[0] == '/';
}

static size_t vmkv_encoder_none_operand_side(
    const vmkv_encoder_t *encoder,
    const vmkv_rwir_instruction_view_t *source_instruction) {
    (void)encoder;
    if (vmkv_encoder_operand_is_none(&source_instruction->read_arguments[0])) {
        return 0U;
    }
    if (vmkv_encoder_operand_is_none(&source_instruction->read_arguments[1])) {
        return 1U;
    }
    return 2U;
}

static bool vmkv_encoder_side_rejects_pointer(
    uint32_t type_set, const vmkv_rwir_argument_view_t *arguments,
    size_t argument_count) {
    size_t ordinal;

    if ((type_set & VMKV_RWIR_TYPE_SET_POINTER) == 0U) {
        return false;
    }
    if (type_set == VMKV_RWIR_TYPE_SET_POINTER) {
        return false;
    }
    if (arguments == NULL || argument_count == 0U) {
        return true;
    }
    for (ordinal = 0U; ordinal < argument_count; ++ordinal) {
        const runtimec_string_view_t declared =
            arguments[ordinal].parameter_type_name;
        if (declared.data != NULL && declared.size != 0U &&
            declared.data[0] == '*') {
            return true;
        }
    }
    return false;
}

runtimec_status_t vmkv_encode_instruction(
    vmkv_encoder_t *encoder, const vmkv_encode_request_t *request,
    binrwir_binfunc_builder_t *builder,
    const vmkv_local_types_t *local_types, vmkv_encode_function_t *function,
    const vmkv_rwir_instruction_view_t *source_instruction,
    vmkv_canonical_instruction_t *output_instruction,
    runtimec_error_t *error) {
    if (output_instruction != NULL) {
        *output_instruction = (vmkv_canonical_instruction_t){0};
    }
    runtimec_error_reset(error);
    if (encoder == NULL || output_instruction == NULL || builder == NULL ||
        function == NULL || source_instruction == NULL || error == NULL ||
        !vmkv_encoder_request_is_valid(request)) {
        return vmkv_encoder_fail(error, RUNTIMEC_STATUS_INVALID,
                                 VMKV_ENCODER_DIAG_INVALID_REQUEST,
                                 "vmkv encoder: invalid encode request",
                                 RUNTIMEC_PROGRAM_COUNTER_NONE);
    }
    
    if (function->next_operand_argument_index <
        function->read_parameter_count + function->write_parameter_count) {
        return vmkv_encoder_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMKV_ENCODER_DIAG_INVALID_REQUEST,
            "vmkv encoder: operand argument index cursor starts below the "
            "parameter range",
            RUNTIMEC_PROGRAM_COUNTER_NONE);
    }
    const runtimec_program_counter_t logical_program_counter =
        vmkv_encoder_set_program_counter(
            encoder, source_instruction->logical_program_counter);
    
    const bool is_rwfunc_call = vmkv_rwir_manifest_is_rwfunc_call(
        source_instruction->operation_langtype);
    
    runtimec_string_view_t operation_name = source_instruction->operation_name;
    
    vmkv_rwir_instruction_view_t normalized_view;
    const vmkv_rwir_instruction_view_t *instruction_view = source_instruction;
    if (!is_rwfunc_call && source_instruction->read_argument_count == 2U &&
        (vmkv_encoder_view_equals(operation_name, "==") ||
         vmkv_encoder_view_equals(operation_name, "!="))) {
        const size_t none_side = vmkv_encoder_none_operand_side(
            encoder, source_instruction);
        if (none_side < 2U) {
            const size_t other = none_side == 0U ? 1U : 0U;
            const runtimec_string_view_t other_type =
                source_instruction->read_arguments[other].parameter_type_name;
            if (!vmkv_encoder_type_is_reference(other_type)) {
                operation_name = vmkv_encoder_view_equals(operation_name, "!=")
                                     ? runtimec_string_view_from_c_string("nonnull")
                                     : runtimec_string_view_from_c_string("nonnull·not");
                
                normalized_view = *source_instruction;
                normalized_view.read_arguments =
                    &source_instruction->read_arguments[other];
                normalized_view.read_argument_count = 1U;
                instruction_view = &normalized_view;
            }
        }
    }
    const vmkv_rwir_capability_t *capability =
        is_rwfunc_call
            ? vmkv_rwir_manifest_capability(RUNTIMEC_RWIR_CALL)
            : vmkv_rwir_manifest_find_operation(operation_name);
    if (capability == NULL) {
        
        binrwir_operation_identifier_t temporary_identifier = 0U;
        if (vmkv_encoder_numeric_reserved_identifier(
                instruction_view->operation_name, &temporary_identifier)) {
            runtimec_status_t temp_status =
                vmkv_encoder_encode_temporary_output(
                    encoder, request, builder, local_types, function,
                    source_instruction, temporary_identifier,
                    logical_program_counter, output_instruction, error);
            if (temp_status != RUNTIMEC_STATUS_SUCCESS) {
                return temp_status;
            }
            return RUNTIMEC_STATUS_SUCCESS;
        }
        
        output_instruction->operation_identifier =
            BINRWIR_OPERATION_IDENTIFIER_FOREIGN;
        output_instruction->logical_program_counter =
            instruction_view->logical_program_counter;
        output_instruction->read_arguments = (binrwir_argument_span_t){0};
        output_instruction->write_arguments = (binrwir_argument_span_t){0};
        output_instruction->read_parameter_types = NULL;
        output_instruction->write_parameter_types = NULL;
        output_instruction->aliasing_declared = false;
        return RUNTIMEC_STATUS_SUCCESS;
    }
    
    if ((capability->read_parameter_type_set &
         VMKV_RWIR_TYPE_SET_EXTENSION) != 0U ||
        (capability->write_parameter_type_set &
         VMKV_RWIR_TYPE_SET_EXTENSION) != 0U) {
        return vmkv_encoder_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED,
            VMKV_ENCODER_DIAG_UNMAPPED_PARAMETER_TYPE,
            "vmkv encoder: extension parameters are not representable from "
            "RWIR text yet",
            logical_program_counter);
    }
    if (vmkv_encoder_side_rejects_pointer(
            capability->read_parameter_type_set,
            instruction_view->read_arguments,
            instruction_view->read_argument_count) ||
        vmkv_encoder_side_rejects_pointer(
            capability->write_parameter_type_set,
            instruction_view->write_arguments,
            instruction_view->write_argument_count)) {
        return vmkv_encoder_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED,
            VMKV_ENCODER_DIAG_UNMAPPED_PARAMETER_TYPE,
            "vmkv encoder: a pointer operand has no representable type text",
            logical_program_counter);
    }
    const size_t read_count = instruction_view->read_argument_count;
    const size_t write_count = instruction_view->write_argument_count;
    
    const size_t target_arguments = is_rwfunc_call ? 1U : 0U;
    const size_t read_region_end = target_arguments + read_count;
    const size_t write_region_begin = read_region_end;
    const size_t total = read_region_end + write_count;
    const bool function_target = is_rwfunc_call;
    runtimec_status_t status = vmkv_encoder_reserve(
        encoder, total == 0U ? 1U : total, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    vmkv_encoder_operation_t operation = {
        .capability = capability,
        .source = source_instruction,
        .logical_program_counter = logical_program_counter,
    };
    status = vmkv_encoder_classify_read_arguments(encoder, &operation,
                                                  function_target, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }

    const vmkv_rwir_capability_t *selected = NULL;
    const vmkv_rwir_specialization_result_t specialization =
        vmkv_rwir_manifest_find_specialization(
            instruction_view->operation_name, encoder->parameter_type_sets,
            read_count, &selected);
    if (specialization == VMKV_RWIR_SPECIALIZATION_RESULT_INVERTED) {
        return vmkv_encoder_fail(
            error, RUNTIMEC_STATUS_ERROR,
            VMKV_ENCODER_DIAG_INVERTED_SPECIALIZATION,
            "vmkv encoder: inverted specialization has no BinRWIR representation",
            logical_program_counter);
    }
    if (specialization == VMKV_RWIR_SPECIALIZATION_RESULT_SELECTED &&
        selected != NULL) {
        operation.capability = selected;
    }
    const vmkv_rwir_capability_t *selected_capability = operation.capability;

    if (selected_capability->read_parameter_type_set ==
        VMKV_RWIR_TYPE_SET_ANY_SINGLE) {
        
    } else if (selected_capability->read_parameter_type_set !=
        VMKV_RWIR_TYPE_SET_NONE) {
        for (size_t ordinal = 0U; ordinal < read_count; ++ordinal) {
            if ((encoder->parameter_type_sets[ordinal] &
                 selected_capability->read_parameter_type_set) == 0U) {
                return vmkv_encoder_fail(
                    error, RUNTIMEC_STATUS_ERROR,
                    VMKV_ENCODER_DIAG_TYPE_SET_OUTSIDE_MASK,
                    "vmkv encoder: read parameter type is outside the capability mask",
                    logical_program_counter);
            }
        }
        if (!vmkv_encoder_constraint_holds(selected_capability,
                                          encoder->parameter_type_sets,
                                          read_count)) {
            return vmkv_encoder_fail(
                error, RUNTIMEC_STATUS_ERROR,
                VMKV_ENCODER_DIAG_CONSTRAINT_VIOLATION,
                "vmkv encoder: operand constraint is violated",
                logical_program_counter);
        }
    }

    runtimec_string_view_t *write_types =
        encoder->argument_types + write_region_begin;
    status = vmkv_encoder_resolve_write_types(encoder, &operation, write_types,
                                              error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (selected_capability->write_parameter_type_set !=
        VMKV_RWIR_TYPE_SET_NONE) {
        for (size_t ordinal = 0U; ordinal < write_count; ++ordinal) {
            uint32_t type_set = VMKV_RWIR_TYPE_SET_NONE;
            if (vmkv_parameter_type_classify(write_types[ordinal], &type_set) !=
                    VMKV_PARAMETER_TYPE_RESULT_SINGLE ||
                (type_set & selected_capability->write_parameter_type_set) ==
                    0U) {
                return vmkv_encoder_fail(
                    error, RUNTIMEC_STATUS_ERROR,
                    VMKV_ENCODER_DIAG_TYPE_SET_OUTSIDE_MASK,
                    "vmkv encoder: result type is outside the capability mask",
                    logical_program_counter);
            }
        }
    }

    if (is_rwfunc_call) {
        
        runtimec_string_view_t target_name = instruction_view->operation_name;
        if (vmkv_encoder_strip_library_prefix(&target_name) &&
            target_name.size == 0U) {
            return vmkv_encoder_fail(
                error, RUNTIMEC_STATUS_ERROR,
                VMKV_ENCODER_DIAG_INVALID_REQUEST,
                "vmkv encoder: rwfunc call target has an empty function name",
                logical_program_counter);
        }
        uint16_t symbol_index = BINRWIR_SYMBOL_INDEX_NONE;
        status = vmkv_encoder_register_symbol(
            builder, target_name, instruction_view->operation_langtype,
            &symbol_index, logical_program_counter, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
        uint32_t target_index = 0U;
        status = vmkv_encoder_take_operand_argument_index(
            function, &target_index, logical_program_counter, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
        encoder->arguments[0] = (binrwir_argument_t){
            .argument_index = target_index,
            .reference_kind = (uint8_t)BINRWIR_REFERENCE_FUNCTION,
            .slot = symbol_index,
            .payload = 0U,
        };
        
        encoder->argument_types[0] = runtimec_string_view_make(NULL, 0U);
    }
    for (size_t ordinal = 0U; ordinal < read_count; ++ordinal) {
        status = vmkv_encoder_map_argument(
            request, builder, local_types, function,
            &instruction_view->read_arguments[ordinal], false,
            vmkv_encoder_parameter_role_is_key(
                selected_capability->read_parameter_roles, ordinal),
            &encoder->arguments[target_arguments + ordinal],
            logical_program_counter, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
        encoder->argument_types[target_arguments + ordinal] =
            instruction_view->read_arguments[ordinal].parameter_type_name;
    }
    for (size_t ordinal = 0U; ordinal < write_count; ++ordinal) {
        status = vmkv_encoder_map_argument(
            request, builder, local_types, function,
            &instruction_view->write_arguments[ordinal], false,
            vmkv_encoder_parameter_role_is_key(
                selected_capability->write_parameter_roles, ordinal),
            &encoder->arguments[write_region_begin + ordinal],
            logical_program_counter, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
    }

    output_instruction->operation_identifier =
        selected_capability->operation_identifier;
    output_instruction->logical_program_counter =
        instruction_view->logical_program_counter;
    output_instruction->read_arguments.data = encoder->arguments;
    
    output_instruction->read_arguments.count = read_region_end;
    output_instruction->write_arguments.data =
        encoder->arguments + write_region_begin;
    output_instruction->write_arguments.count = write_count;
    output_instruction->read_parameter_types = encoder->argument_types;
    output_instruction->write_parameter_types = encoder->argument_types + read_count;
    output_instruction->aliasing_declared =
        selected_capability->alias_policy != RUNTIMEC_RWIR_ALIAS_FORBIDDEN;
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t vmkv_encoder_record_alias_rules(
    vmkv_encoder_t *encoder,
    const vmkv_canonical_instruction_t *canonical, runtimec_error_t *error) {
    if (!canonical->aliasing_declared) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    for (size_t read_ordinal = 0U;
         read_ordinal < canonical->read_arguments.count; ++read_ordinal) {
        const binrwir_argument_t *read_argument =
            &canonical->read_arguments.data[read_ordinal];
        if (read_argument->reference_kind != BINRWIR_REFERENCE_PARAMETER_READ) {
            continue;
        }
        for (size_t write_ordinal = 0U;
             write_ordinal < canonical->write_arguments.count; ++write_ordinal) {
            const binrwir_argument_t *write_argument =
                &canonical->write_arguments.data[write_ordinal];
            if (write_argument->reference_kind !=
                BINRWIR_REFERENCE_PARAMETER_WRITE) {
                continue;
            }
            const binrwir_alias_rule_t rule = {
                .read_parameter_index = read_argument->slot,
                .write_parameter_index = write_argument->slot,
                .policy = (uint8_t)BINRWIR_ALIAS_MAY_ALIAS,
                .flags = 0U,
                .reserved_16_bits = 0U,
            };
            bool duplicate = false;
            for (size_t ordinal = 0U; ordinal < encoder->alias_rule_count;
                 ++ordinal) {
                const binrwir_alias_rule_t *existing =
                    &encoder->alias_rules[ordinal];
                if (existing->read_parameter_index ==
                        rule.read_parameter_index &&
                    existing->write_parameter_index ==
                        rule.write_parameter_index) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                continue;
            }
            if (encoder->alias_rule_count == encoder->alias_rule_capacity) {
                const size_t capacity = encoder->alias_rule_capacity == 0U
                                            ? 4U
                                            : encoder->alias_rule_capacity * 2U;
                binrwir_alias_rule_t *grown = runtimec_allocator_allocate(
                    &encoder->allocator, capacity * sizeof(*grown),
                    _Alignof(binrwir_alias_rule_t));
                if (grown == NULL) {
                    return vmkv_encoder_fail(
                        error, RUNTIMEC_STATUS_OUT_OF_MEMORY,
                        VMKV_ENCODER_DIAG_OUT_OF_MEMORY,
                        "vmkv encoder: alias rule allocation failed",
                        RUNTIMEC_PROGRAM_COUNTER_NONE);
                }
                if (encoder->alias_rules != NULL) {
                    memcpy(grown, encoder->alias_rules,
                           encoder->alias_rule_count * sizeof(*grown));
                    runtimec_allocator_deallocate(
                        &encoder->allocator, encoder->alias_rules,
                        encoder->alias_rule_capacity * sizeof(*grown),
                        _Alignof(binrwir_alias_rule_t));
                }
                encoder->alias_rules = grown;
                encoder->alias_rule_capacity = capacity;
            }
            encoder->alias_rules[encoder->alias_rule_count++] = rule;
        }
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static bool vmkv_encoder_source_revision_equals(
    const runtimec_source_revision_t *left,
    const runtimec_source_revision_t *right) {
    return left->instance_identifier == right->instance_identifier &&
           left->generation == right->generation &&
           left->schema_version == right->schema_version &&
           left->instruction_set_version == right->instruction_set_version;
}

static runtimec_status_t vmkv_encoder_register_parameters(
    binrwir_binfunc_builder_t *builder,
    const vmkv_function_source_t *source, runtimec_error_t *error) {
    for (uint32_t ordinal = 0U; ordinal < source->read_parameter_count;
         ++ordinal) {
        const vmkv_param_definition_t *definition =
            &source->read_parameters[ordinal];
        uint32_t type_set = VMKV_RWIR_TYPE_SET_NONE;
        const runtimec_string_view_t declared = runtimec_string_view_make(
            definition->type_name, definition->type_name_size);
        const vmkv_parameter_type_result_t result =
            vmkv_parameter_type_classify(declared, &type_set);
        if (result != VMKV_PARAMETER_TYPE_RESULT_SINGLE) {
            return vmkv_encoder_fail(
                error, RUNTIMEC_STATUS_UNSUPPORTED,
                result == VMKV_PARAMETER_TYPE_RESULT_UNION
                    ? VMKV_ENCODER_DIAG_UNION_PARAMETER
                    : VMKV_ENCODER_DIAG_INVALID_PARAMETER_TYPE,
                "vmkv encoder: read parameter declaration has no concrete langtype",
                RUNTIMEC_PROGRAM_COUNTER_NONE);
        }
        uint32_t parameter_index = BINRWIR_ARGUMENT_INDEX_NONE;
        const runtimec_status_t status = binrwir_binfunc_builder_add_parameter(
            builder, BINRWIR_PARAMETER_DIRECTION_READ, ordinal, declared,
            &parameter_index);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
    }
    for (uint32_t ordinal = 0U; ordinal < source->write_parameter_count;
         ++ordinal) {
        const vmkv_param_definition_t *definition =
            &source->write_parameters[ordinal];
        uint32_t type_set = VMKV_RWIR_TYPE_SET_NONE;
        const runtimec_string_view_t declared = runtimec_string_view_make(
            definition->type_name, definition->type_name_size);
        const vmkv_parameter_type_result_t result =
            vmkv_parameter_type_classify(declared, &type_set);
        if (result != VMKV_PARAMETER_TYPE_RESULT_SINGLE) {
            return vmkv_encoder_fail(
                error, RUNTIMEC_STATUS_UNSUPPORTED,
                result == VMKV_PARAMETER_TYPE_RESULT_UNION
                    ? VMKV_ENCODER_DIAG_UNION_PARAMETER
                    : VMKV_ENCODER_DIAG_INVALID_PARAMETER_TYPE,
                "vmkv encoder: write parameter declaration has no concrete langtype",
                RUNTIMEC_PROGRAM_COUNTER_NONE);
        }
        uint32_t parameter_index = BINRWIR_ARGUMENT_INDEX_NONE;
        const runtimec_status_t status = binrwir_binfunc_builder_add_parameter(
            builder, BINRWIR_PARAMETER_DIRECTION_WRITE,
            source->read_parameter_count + ordinal, declared,
            &parameter_index);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmkv_encode(vmkv_encoder_t *encoder,
                              const vmkv_encode_request_t *request,
                              binrwir_binfunc_t **output_program,
                              runtimec_error_t *error) {
    if (output_program != NULL) {
        *output_program = NULL;
    }
    runtimec_error_reset(error);
    if (encoder == NULL || output_program == NULL || error == NULL ||
        !vmkv_encoder_request_is_valid(request)) {
        return vmkv_encoder_fail(error, RUNTIMEC_STATUS_INVALID,
                                 VMKV_ENCODER_DIAG_INVALID_REQUEST,
                                 "vmkv encoder: invalid program request",
                                 RUNTIMEC_PROGRAM_COUNTER_NONE);
    }
    vmkv_function_source_t *function = NULL;
    runtimec_snapshot_t snapshot;
    runtimec_snapshot_reset(&snapshot);
    runtimec_status_t status = vmkv_source_open_function(
        request->pathspace_access, request->function_path, &function, &snapshot,
        error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (!vmkv_encoder_source_revision_equals(
            &snapshot.source_revision,
            &request->expected_snapshot.source_revision)) {
        vmkv_source_close_function(function);
        return vmkv_encoder_fail(
            error, RUNTIMEC_STATUS_INVALID, VMKV_ENCODER_DIAG_STALE_SNAPSHOT,
            "vmkv encoder: function source revision differs from the request",
            RUNTIMEC_PROGRAM_COUNTER_NONE);
    }
    vmkv_encode_function_t function_shape = {
        .read_parameter_count = function->read_parameter_count,
        .write_parameter_count = function->write_parameter_count,
        .next_operand_argument_index = function->read_parameter_count +
                                       function->write_parameter_count,
    };
    const binrwir_binfunc_identity_t identity = {
        .snapshot_generation = snapshot.source_revision.generation,
        .program_generation = VMKV_ENCODER_PROGRAM_GENERATION,
        .schema_version = snapshot.source_revision.schema_version,
        .rwir_manifest_version = VMKV_RWIR_MANIFEST_VERSION,
        .binrwir_binary_interface_generation = BINRWIR_BINARY_INTERFACE_GENERATION,
        .canonical_encoding_revision =
            VMKV_ENCODER_CANONICAL_ENCODING_REVISION,
    };
    binrwir_binfunc_builder_t *builder = NULL;
    status = binrwir_binfunc_builder_create(&encoder->allocator, &identity,
                                            &builder);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        vmkv_source_close_function(function);
        return vmkv_encoder_fail(
            error, status, VMKV_ENCODER_DIAG_PROGRAM_REJECTED,
            "vmkv encoder: program builder rejected the identity",
            RUNTIMEC_PROGRAM_COUNTER_NONE);
    }
    encoder->alias_rule_count = 0U;
    status = vmkv_encoder_register_parameters(builder, function, error);
    
    vmkv_local_types_t *local_types = NULL;
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = vmkv_local_types_collect(function, encoder->allocator,
                                          &local_types, error);
    }
    vmkv_rwir_reader_t *reader = NULL;
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = vmkv_rwir_reader_create(function, &reader, error);
    }
    vmkv_rwir_read_result_t read_result = VMKV_RWIR_READ_RESULT_INVALID;
    vmkv_rwir_instruction_view_t instruction;
    
    while (status == RUNTIMEC_STATUS_SUCCESS) {
        status = vmkv_rwir_reader_next(reader, &read_result, &instruction, error);
        if (status != RUNTIMEC_STATUS_SUCCESS ||
            read_result != VMKV_RWIR_READ_RESULT_INSTRUCTION) {
            break;
        }
        vmkv_canonical_instruction_t canonical;
        status = vmkv_encode_instruction(encoder, request, builder, local_types,
                                         &function_shape, &instruction,
                                         &canonical, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            break;
        }
        status = vmkv_encoder_record_alias_rules(encoder, &canonical, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            break;
        }
        const binrwir_instruction_build_request_t build_request = {
            .source_logical_program_counter = canonical.logical_program_counter,
            .operation_identifier = canonical.operation_identifier,
            .read_arguments = canonical.read_arguments,
            .write_arguments = canonical.write_arguments,
        };
        uint32_t instruction_identifier = BINRWIR_IDENTIFIER_NONE;
        status = binrwir_binfunc_builder_add_instruction(
            builder, &build_request, &instruction_identifier);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            status = vmkv_encoder_fail(
                error, status, VMKV_ENCODER_DIAG_PROGRAM_REJECTED,
                "vmkv encoder: program builder rejected an instruction",
                RUNTIMEC_PROGRAM_COUNTER_NONE);
        }
    }
    if (reader != NULL) {
        vmkv_rwir_reader_destroy(reader);
    }
    (void)vmkv_local_types_destroy(local_types, encoder->allocator);
    for (size_t ordinal = 0U;
         status == RUNTIMEC_STATUS_SUCCESS &&
         ordinal < encoder->alias_rule_count;
         ++ordinal) {
        uint32_t alias_rule_identifier = BINRWIR_IDENTIFIER_NONE;
        status = binrwir_binfunc_builder_add_alias_rule(
            builder, &encoder->alias_rules[ordinal], &alias_rule_identifier);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            status = vmkv_encoder_fail(
                error, status, VMKV_ENCODER_DIAG_PROGRAM_REJECTED,
                "vmkv encoder: program builder rejected an alias rule",
                RUNTIMEC_PROGRAM_COUNTER_NONE);
        }
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_binfunc_builder_finalize(builder, output_program);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            status = vmkv_encoder_fail(
                error, status, VMKV_ENCODER_DIAG_PROGRAM_REJECTED,
                "vmkv encoder: program builder rejected the program",
                RUNTIMEC_PROGRAM_COUNTER_NONE);
        }
    }
    binrwir_binfunc_builder_destroy(builder);
    vmkv_source_close_function(function);
    return status;
}
