/*
 * 组件：vmkv/encoder 的实现
 * 作用：明文 RWIR → canonical 指令 → canonical BinRWIR。别名表定 opid，能力行
 *       掩码定参数类型，结果规则定写参类型，参数引用形态定 reference kind。
 * 约束：只做等价编码。不优化、不融合、不生成执行计划或机器码，不执行，不缓存
 *       V.body；任何不确定处失败封闭，不用默认提升或占位地址换取成功。
 */

#include "vmkv/encoder.h"

#include <string.h>

#include "contract/error.h"
#include "contract/host_services.h"
#include "contract/view.h"
#include "vmkv/parameter_type.h"
#include "vmkv/rwir_manifest.h"
#include "vmkv/vmkv_internal.h"

/* 诊断码：与 vmkv/source 的 1–11 连续，编码阶段从 20 起。 */
enum {
    VMKV_ENCODER_DIAG_INVALID_REQUEST = 20,
    VMKV_ENCODER_DIAG_HOST_CAPABILITY_MISSING = 21,
    VMKV_ENCODER_DIAG_UNKNOWN_OPERATION = 22,
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
    VMKV_ENCODER_DIAG_PROGRAM_REJECTED = 34
};

#define VMKV_ENCODER_CANONICAL_ENCODING_REVISION 1U
#define VMKV_ENCODER_PROGRAM_GENERATION 1U
#define VMKV_ENCODER_PROGRAM_COUNTER_CAPACITY 24U

struct vmkv_encoder_t {
    runtimec_allocator_t allocator;
    /* 三个并行数组，index 0..read_count-1 是读参，其后是写参。 */
    binrwir_argument_t *arguments;
    runtimec_string_view_t *argument_types;
    uint32_t *parameter_type_sets;
    size_t argument_capacity;
    /* 全函数去重后的别名规则对（只保留首次出现）。 */
    binrwir_alias_rule_t *alias_rules;
    size_t alias_rule_count;
    size_t alias_rule_capacity;
    char program_counter_text[VMKV_ENCODER_PROGRAM_COUNTER_CAPACITY];
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
    const char *message, runtimec_string_view_t logical_program_counter) {
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT, code,
                       runtimec_string_view_from_c_string(message),
                       logical_program_counter);
    return status;
}

static runtimec_string_view_t vmkv_encoder_set_program_counter(
    vmkv_encoder_t *encoder, uint32_t line) {
    char digits[16];
    size_t digit_count = 0U;
    size_t used = 0U;
    uint32_t value = line;
    do {
        digits[digit_count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U);
    encoder->program_counter_text[used++] = '[';
    while (digit_count != 0U) {
        encoder->program_counter_text[used++] = digits[--digit_count];
    }
    encoder->program_counter_text[used++] = ',';
    encoder->program_counter_text[used++] = '0';
    encoder->program_counter_text[used++] = ']';
    return runtimec_string_view_make(encoder->program_counter_text, used);
}

static bool vmkv_encoder_request_is_valid(const vmkv_encode_request_t *request) {
    return request != NULL &&
           runtimec_host_services_is_valid(request->host_services) &&
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
                                 runtimec_string_view_make(NULL, 0U));
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

/* 能力行的跨操作数约束；未知类别一律视为不成立。 */
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

/*
 * 结果宽度：与操作数顺序无关（矩阵 mixed-precision-promotion-order-independent）。
 * 同为整数取最宽、混合符号不成立；同为浮点取最宽；整数与浮点混合取 float64。
 */
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
        /* 全浮点保留 float32；与整数混合按矩阵取 float64。 */
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

/* 能力行是否自己算写参类型；NONE/TARGET_TYPE/IDENTITY 由写参自身的槽给出。 */
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

/* `*[0,±k]` → 1 起的形参序号与方向；不是该形态返回 false。 */
static bool vmkv_encoder_parse_parameter_reference(runtimec_key_view_t key,
                                                   uint16_t *output_ordinal,
                                                   bool *output_write) {
    *output_ordinal = 0U;
    *output_write = false;
    if (key.size < 6U || key.data[0] != '*' ||
        memcmp(key.data, "*[0,", 4U) != 0) {
        return false;
    }
    size_t cursor = 4U;
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
    *output_body_address = RUNTIMEC_VALUE_BODY_ADDRESS_NONE;
    if (xvalue_address == RUNTIMEC_XVALUE_ADDRESS_NONE ||
        xvalue_address > UINT64_MAX - RUNTIMEC_XVALUE_BODY_OFFSET_BYTES) {
        return false;
    }
    *output_body_address = xvalue_address + RUNTIMEC_XVALUE_BODY_OFFSET_BYTES;
    return true;
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
                                 runtimec_string_view_make(NULL, 0U));
    }
    vmkv_encoder_t *encoder = runtimec_allocator_allocate(
        &allocator, sizeof(*encoder), _Alignof(vmkv_encoder_t));
    if (encoder == NULL) {
        return vmkv_encoder_fail(error, RUNTIMEC_STATUS_OUT_OF_MEMORY,
                                 VMKV_ENCODER_DIAG_OUT_OF_MEMORY,
                                 "vmkv encoder: allocation failed",
                                 runtimec_string_view_make(NULL, 0U));
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

/* 静态参数：绝对路径 → host 解引用 XValue 基址 → 唯一推导 body 地址。 */
runtimec_status_t vmkv_encode_static_argument(
    const vmkv_encode_request_t *request,
    const vmkv_rwir_argument_view_t *source_argument,
    uint32_t argument_index, binrwir_argument_t *output_argument,
    runtimec_error_t *error) {
    if (output_argument != NULL) {
        memset(output_argument, 0, sizeof(*output_argument));
    }
    if (!vmkv_encoder_request_is_valid(request) || output_argument == NULL ||
        argument_index == BINRWIR_ARGUMENT_INDEX_NONE ||
        source_argument == NULL ||
        !runtimec_string_view_is_valid(source_argument->parameter_type_name) ||
        source_argument->parameter_type_name.size == 0U ||
        !runtimec_key_view_is_valid(source_argument->key) ||
        source_argument->key.size == 0U) {
        return vmkv_encoder_fail(error, RUNTIMEC_STATUS_INVALID,
                                 VMKV_ENCODER_DIAG_INVALID_REQUEST,
                                 "vmkv encoder: invalid static argument request",
                                 runtimec_string_view_make(NULL, 0U));
    }
    if (source_argument->key_addressability == VMKV_KEY_ADDRESSABILITY_DYNAMIC) {
        return vmkv_encoder_fail(error, RUNTIMEC_STATUS_UNSUPPORTED,
                                 VMKV_ENCODER_DIAG_DYNAMIC_KEY,
                                 "vmkv encoder: dynamic key is unsupported",
                                 runtimec_string_view_make(NULL, 0U));
    }
    if (request->host_services->resolve_static_xvalue_address == NULL) {
        return vmkv_encoder_fail(
            error, RUNTIMEC_STATUS_NOT_IMPLEMENTED,
            VMKV_ENCODER_DIAG_HOST_CAPABILITY_MISSING,
            "vmkv encoder: host cannot resolve static xvalue addresses",
            runtimec_string_view_make(NULL, 0U));
    }

    runtimec_xvalue_address_t xvalue_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
    runtimec_status_t status =
        request->host_services->resolve_static_xvalue_address(
            request->host_services->context, &request->expected_snapshot,
            source_argument->key, &xvalue_address, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    runtimec_value_body_address_t body_address =
        RUNTIMEC_VALUE_BODY_ADDRESS_NONE;
    if (!vmkv_encoder_derive_value_body_address(xvalue_address,
                                                &body_address)) {
        return vmkv_encoder_fail(error, RUNTIMEC_STATUS_ERROR,
                                 VMKV_ENCODER_DIAG_ADDRESS_DERIVATION,
                                 "vmkv encoder: xvalue base is not addressable",
                                 runtimec_string_view_make(NULL, 0U));
    }
    output_argument->argument_index = argument_index;
    output_argument->reference_kind = (uint8_t)BINRWIR_REFERENCE_STATIC;
    output_argument->reserved_8_bits = 0U;
    output_argument->slot = 0U;
    output_argument->payload = body_address;
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 一个实参的引用形态：内联字面量 → 形参引用 → 静态 key；其余失败封闭。 */
static runtimec_status_t vmkv_encoder_map_argument(
    const vmkv_encode_request_t *request, vmkv_encode_function_t *function,
    const vmkv_rwir_argument_view_t *source_argument,
    uint64_t literal_index, binrwir_argument_t *output,
    runtimec_string_view_t logical_program_counter, runtimec_error_t *error) {
    *output = (binrwir_argument_t){0};
    if (source_argument->literal.bytes.data != NULL) {
        output->argument_index = function->next_operand_argument_index;
        output->reference_kind = (uint8_t)BINRWIR_REFERENCE_LITERAL;
        output->payload = literal_index;
        /* 字面量出现也占一个 Program 内唯一的 argidx。 */
        if (function->next_operand_argument_index ==
            BINRWIR_ARGUMENT_INDEX_NONE - 1U) {
            return vmkv_encoder_fail(
                error, RUNTIMEC_STATUS_ERROR, VMKV_ENCODER_DIAG_OUT_OF_MEMORY,
                "vmkv encoder: operand argument index space exhausted",
                logical_program_counter);
        }
        function->next_operand_argument_index += 1U;
        return RUNTIMEC_STATUS_SUCCESS;
    }
    if (source_argument->key_addressability == VMKV_KEY_ADDRESSABILITY_STATIC) {
        if (function->next_operand_argument_index ==
            BINRWIR_ARGUMENT_INDEX_NONE - 1U) {
            return vmkv_encoder_fail(
                error, RUNTIMEC_STATUS_ERROR, VMKV_ENCODER_DIAG_OUT_OF_MEMORY,
                "vmkv encoder: operand argument index space exhausted",
                logical_program_counter);
        }
        const uint32_t argument_index = function->next_operand_argument_index;
        function->next_operand_argument_index += 1U;
        return vmkv_encode_static_argument(request, source_argument,
                                           argument_index, output, error);
    }
    uint16_t parameter_ordinal = 0U;
    bool writes_parameter = false;
    if (source_argument->parameter_name.size == 0U ||
        !vmkv_encoder_parse_parameter_reference(
            source_argument->key, &parameter_ordinal, &writes_parameter) ||
        (writes_parameter ? parameter_ordinal > function->write_parameter_count
                          : parameter_ordinal > function->read_parameter_count)) {
        /* 具名帧局部槽与调用目标名：帧局部槽号是调用期事实，调用目标也不是
         * 可寻址参数；两者都没有可编译的数值槽契约，因此失败封闭。 */
        return vmkv_encoder_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, VMKV_ENCODER_DIAG_DYNAMIC_KEY,
            "vmkv encoder: named frame slot has no numeric slot contract",
            logical_program_counter);
    }
    /* canonical argidx：读参 0..nr-1，写参 nr..nr+nw-1；slot 是该方向形参表里
     * 的序号（0 起），与 `*[0,±k]` 的 k 相差 1。 */
    output->slot = (uint16_t)(parameter_ordinal - 1U);
    output->argument_index =
        writes_parameter
            ? function->read_parameter_count + parameter_ordinal - 1U
            : parameter_ordinal - 1U;
    output->reference_kind =
        (uint8_t)(writes_parameter ? BINRWIR_REFERENCE_PARAMETER_WRITE
                                   : BINRWIR_REFERENCE_PARAMETER_READ);
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 一条已解析操作的编码状态，避免在函数间反复传长参数表。 */
typedef struct vmkv_encoder_operation {
    const vmkv_rwir_capability_t *capability;
    const vmkv_rwir_instruction_view_t *source;
    runtimec_string_view_t logical_program_counter;
} vmkv_encoder_operation_t;

static runtimec_status_t vmkv_encoder_classify_read_arguments(
    vmkv_encoder_t *encoder, vmkv_encoder_operation_t *operation,
    runtimec_error_t *error) {
    const vmkv_rwir_instruction_view_t *source = operation->source;
    for (size_t ordinal = 0U; ordinal < source->read_argument_count; ++ordinal) {
        uint32_t type_set = VMKV_RWIR_TYPE_SET_NONE;
        const vmkv_parameter_type_result_t result =
            vmkv_parameter_type_classify(
                source->read_arguments[ordinal].parameter_type_name, &type_set);
        if (result == VMKV_PARAMETER_TYPE_RESULT_UNION) {
            /* 声明是并集、槽上又没有具体类型：真动态，不猜一个精度。 */
            return vmkv_encoder_fail(
                error, RUNTIMEC_STATUS_UNSUPPORTED,
                VMKV_ENCODER_DIAG_UNION_PARAMETER,
                "vmkv encoder: parameter has no concrete langtype",
                operation->logical_program_counter);
        }
        if (result == VMKV_PARAMETER_TYPE_RESULT_UNMAPPED) {
            return vmkv_encoder_fail(
                error, RUNTIMEC_STATUS_UNSUPPORTED,
                VMKV_ENCODER_DIAG_UNMAPPED_PARAMETER_TYPE,
                "vmkv encoder: parameter type is outside the category set",
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

/* 写参类型：计算型结果规则要求恰好一个写参；其余由写参自己的槽给出。 */
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
        uint32_t type_set = VMKV_RWIR_TYPE_SET_NONE;
        if (vmkv_parameter_type_classify(declared, &type_set) !=
            VMKV_PARAMETER_TYPE_RESULT_SINGLE) {
            return vmkv_encoder_fail(
                error, RUNTIMEC_STATUS_UNSUPPORTED,
                VMKV_ENCODER_DIAG_INVALID_PARAMETER_TYPE,
                "vmkv encoder: write parameter has no concrete langtype",
                operation->logical_program_counter);
        }
        targets[ordinal] = declared;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmkv_encode_instruction(
    vmkv_encoder_t *encoder, const vmkv_encode_request_t *request,
    vmkv_encode_function_t *function,
    const vmkv_rwir_instruction_view_t *source_instruction,
    vmkv_canonical_instruction_t *output_instruction,
    runtimec_error_t *error) {
    if (output_instruction != NULL) {
        *output_instruction = (vmkv_canonical_instruction_t){0};
    }
    runtimec_error_reset(error);
    if (encoder == NULL || output_instruction == NULL || function == NULL ||
        source_instruction == NULL || error == NULL ||
        !vmkv_encoder_request_is_valid(request)) {
        return vmkv_encoder_fail(error, RUNTIMEC_STATUS_INVALID,
                                 VMKV_ENCODER_DIAG_INVALID_REQUEST,
                                 "vmkv encoder: invalid encode request",
                                 runtimec_string_view_make(NULL, 0U));
    }
    /* 非形参 argidx 必须从形参区间之后开始，否则会和签名编号相撞。 */
    if (function->next_operand_argument_index <
        function->read_parameter_count + function->write_parameter_count) {
        return vmkv_encoder_fail(
            error, RUNTIMEC_STATUS_INVALID,
            VMKV_ENCODER_DIAG_INVALID_REQUEST,
            "vmkv encoder: operand argument index cursor starts below the "
            "parameter range",
            runtimec_string_view_make(NULL, 0U));
    }
    const runtimec_string_view_t logical_program_counter =
        vmkv_encoder_set_program_counter(
            encoder, source_instruction->logical_program_counter);
    const vmkv_rwir_capability_t *capability =
        vmkv_rwir_manifest_find_operation(source_instruction->operation_name);
    if (capability == NULL) {
        return vmkv_encoder_fail(
            error, RUNTIMEC_STATUS_ERROR,
            VMKV_ENCODER_DIAG_UNKNOWN_OPERATION,
            "vmkv encoder: operation name is not registered in the alias table",
            logical_program_counter);
    }
    /* 指针性与扩展类别不在 langtype 文本里（指针性在 XValue head 的 ptr 位），
     * 本阶段一律不猜。 */
    if ((capability->read_parameter_type_set &
         (VMKV_RWIR_TYPE_SET_POINTER | VMKV_RWIR_TYPE_SET_EXTENSION)) != 0U ||
        (capability->write_parameter_type_set &
         (VMKV_RWIR_TYPE_SET_POINTER | VMKV_RWIR_TYPE_SET_EXTENSION)) != 0U) {
        return vmkv_encoder_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED,
            VMKV_ENCODER_DIAG_UNMAPPED_PARAMETER_TYPE,
            "vmkv encoder: pointer and extension parameters are not "
            "representable from RWIR text yet",
            logical_program_counter);
    }
    const size_t read_count = source_instruction->read_argument_count;
    const size_t write_count = source_instruction->write_argument_count;
    const size_t total = read_count + write_count;
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
    status = vmkv_encoder_classify_read_arguments(encoder, &operation, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }

    /* 编译器改选：两个操作数的静态类型集都落进声明掩码才改选能力行。 */
    const vmkv_rwir_capability_t *selected = NULL;
    const vmkv_rwir_specialization_result_t specialization =
        vmkv_rwir_manifest_find_specialization(
            source_instruction->operation_name, encoder->parameter_type_sets,
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

    if (selected_capability->read_parameter_type_set !=
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

    runtimec_string_view_t *write_types = encoder->argument_types + read_count;
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

    uint64_t literal_index = 0U;
    for (size_t ordinal = 0U; ordinal < read_count; ++ordinal) {
        const uint64_t current_literal_index =
            source_instruction->read_arguments[ordinal].literal.bytes.data !=
                    NULL
                ? literal_index++
                : 0U;
        status = vmkv_encoder_map_argument(
            request, function, &source_instruction->read_arguments[ordinal],
            current_literal_index, &encoder->arguments[ordinal],
            logical_program_counter, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
        encoder->argument_types[ordinal] =
            source_instruction->read_arguments[ordinal].parameter_type_name;
    }
    for (size_t ordinal = 0U; ordinal < write_count; ++ordinal) {
        const uint64_t current_literal_index =
            source_instruction->write_arguments[ordinal].literal.bytes.data !=
                    NULL
                ? literal_index++
                : 0U;
        status = vmkv_encoder_map_argument(
            request, function, &source_instruction->write_arguments[ordinal],
            current_literal_index, &encoder->arguments[read_count + ordinal],
            logical_program_counter, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
    }

    output_instruction->operation_identifier =
        selected_capability->operation_identifier;
    output_instruction->logical_program_counter =
        source_instruction->logical_program_counter;
    output_instruction->read_arguments.data = encoder->arguments;
    output_instruction->read_arguments.count = read_count;
    output_instruction->write_arguments.data = encoder->arguments + read_count;
    output_instruction->write_arguments.count = write_count;
    output_instruction->read_parameter_types = encoder->argument_types;
    output_instruction->write_parameter_types = encoder->argument_types + read_count;
    output_instruction->aliasing_declared =
        selected_capability->alias_policy != RUNTIMEC_RWIR_ALIAS_FORBIDDEN;
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 能力行的 alias policy 是行级的，签名要的是 (read, write) 对级规则：
 * 允许声明的行对每个（读形参, 写形参）产生一条 MAY_ALIAS，禁止的行不产生规则。
 * 同一对在整份 Program 里只登记一次（签名校验拒绝重复对）。
 */
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
                        runtimec_string_view_make(NULL, 0U));
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

/* 函数形参登记成 Program 签名：读参 0..nr-1，写参 nr..nr+nw-1。 */
static runtimec_status_t vmkv_encoder_register_parameters(
    binrwir_program_builder_t *builder,
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
                runtimec_string_view_make(NULL, 0U));
        }
        uint32_t parameter_index = BINRWIR_ARGUMENT_INDEX_NONE;
        const runtimec_status_t status = binrwir_program_builder_add_parameter(
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
                runtimec_string_view_make(NULL, 0U));
        }
        uint32_t parameter_index = BINRWIR_ARGUMENT_INDEX_NONE;
        const runtimec_status_t status = binrwir_program_builder_add_parameter(
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
                              binrwir_program_t **output_program,
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
                                 runtimec_string_view_make(NULL, 0U));
    }
    vmkv_function_source_t *function = NULL;
    runtimec_snapshot_t snapshot;
    runtimec_snapshot_reset(&snapshot);
    runtimec_status_t status = vmkv_source_open_function(
        request->host_services, request->function_path, &function, &snapshot,
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
            runtimec_string_view_make(NULL, 0U));
    }
    vmkv_encode_function_t function_shape = {
        .read_parameter_count = function->read_parameter_count,
        .write_parameter_count = function->write_parameter_count,
        .next_operand_argument_index = function->read_parameter_count +
                                       function->write_parameter_count,
    };
    const binrwir_program_identity_t identity = {
        .snapshot_generation = snapshot.source_revision.generation,
        .program_generation = VMKV_ENCODER_PROGRAM_GENERATION,
        .schema_version = snapshot.source_revision.schema_version,
        .rwir_manifest_version = VMKV_RWIR_MANIFEST_VERSION,
        .binrwir_binary_interface_generation = BINRWIR_BINARY_INTERFACE_GENERATION,
        .canonical_encoding_revision =
            VMKV_ENCODER_CANONICAL_ENCODING_REVISION,
    };
    binrwir_program_builder_t *builder = NULL;
    status = binrwir_program_builder_create(&encoder->allocator, &identity,
                                            &builder);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        vmkv_source_close_function(function);
        return vmkv_encoder_fail(
            error, status, VMKV_ENCODER_DIAG_PROGRAM_REJECTED,
            "vmkv encoder: program builder rejected the identity",
            runtimec_string_view_make(NULL, 0U));
    }
    encoder->alias_rule_count = 0U;
    status = vmkv_encoder_register_parameters(builder, function, error);
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
        status = vmkv_encode_instruction(encoder, request, &function_shape,
                                         &instruction, &canonical, error);
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
        status = binrwir_program_builder_add_instruction(
            builder, &build_request, &instruction_identifier);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            status = vmkv_encoder_fail(
                error, status, VMKV_ENCODER_DIAG_PROGRAM_REJECTED,
                "vmkv encoder: program builder rejected an instruction",
                runtimec_string_view_make(NULL, 0U));
        }
    }
    if (reader != NULL) {
        vmkv_rwir_reader_destroy(reader);
    }
    for (size_t ordinal = 0U;
         status == RUNTIMEC_STATUS_SUCCESS &&
         ordinal < encoder->alias_rule_count;
         ++ordinal) {
        uint32_t alias_rule_identifier = BINRWIR_IDENTIFIER_NONE;
        status = binrwir_program_builder_add_alias_rule(
            builder, &encoder->alias_rules[ordinal], &alias_rule_identifier);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            status = vmkv_encoder_fail(
                error, status, VMKV_ENCODER_DIAG_PROGRAM_REJECTED,
                "vmkv encoder: program builder rejected an alias rule",
                runtimec_string_view_make(NULL, 0U));
        }
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_program_builder_finalize(builder, output_program);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            status = vmkv_encoder_fail(
                error, status, VMKV_ENCODER_DIAG_PROGRAM_REJECTED,
                "vmkv encoder: program builder rejected the program",
                runtimec_string_view_make(NULL, 0U));
        }
    }
    binrwir_program_builder_destroy(builder);
    vmkv_source_close_function(function);
    return status;
}
