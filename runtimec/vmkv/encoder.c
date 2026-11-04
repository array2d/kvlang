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
#include "vmkv/local_types.h"
#include "vmkv/parameter_type.h"
#include "vmkv/rwir_manifest.h"
#include "vmkv/vmkv_internal.h"

/* 诊断码：与 vmkv/source 的 1–11 连续，编码阶段从 20 起。 */
enum {
    VMKV_ENCODER_DIAG_INVALID_REQUEST = 20,
    VMKV_ENCODER_DIAG_HOST_CAPABILITY_MISSING = 21,
    /* 22 已退役（未知算子改发 FOREIGN 标记），编号不复用。 */
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
    /* 三个并行数组，index 0..read_count-1 是读参，其后是写参。 */
    binrwir_argument_t *arguments;
    runtimec_string_view_t *argument_types;
    uint32_t *parameter_type_sets;
    size_t argument_capacity;
    /* 全函数去重后的别名规则对（只保留首次出现）。 */
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

/*
 * 源码行号 → 诊断用的 pc 结构。vmkv 只知道源码行（layout 的 irseq），不知道运行时
 * 帧号，所以 stack_identifier 取 STACK_IDENTIFIER_NONE、s0 取行号——这是
 * 「源码侧位置」形态：知道第几条指令，没有帧。**不能取 0**，0 是合法帧号，
 * 那等于发明了一个帧（见 program_counter.h 的三种形态）。
 * 不再格式化成 `[n]` 文本：pc 是结构体，文本投影由展示层做。
 */
static runtimec_program_counter_t vmkv_encoder_set_program_counter(
    vmkv_encoder_t *encoder, uint32_t line) {
    (void)encoder;
    return (runtimec_program_counter_t){
        RUNTIMEC_PROGRAM_COUNTER_STACK_IDENTIFIER_NONE, (int32_t)line};
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

/* body 地址只经合同唯一函数推导：零基址与溢出都由它拒绝，本组件不重做偏移算术。 */
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

/* 登记一个符号；失败时带上当前逻辑程序计数器。 */
/* 剥掉 `/lib/` 前缀（若存在）；返回是否剥过。 */
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
    binrwir_program_builder_t *builder, runtimec_string_view_t name,
    runtimec_string_view_t langtype, uint16_t *output_symbol_index,
    runtimec_program_counter_t logical_program_counter, runtimec_error_t *error) {
    const runtimec_status_t status = binrwir_program_builder_add_symbol(
        builder, name, langtype, output_symbol_index);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    return vmkv_encoder_fail(error, status,
                             VMKV_ENCODER_DIAG_PROGRAM_REJECTED,
                             "vmkv encoder: program builder rejected a symbol",
                             logical_program_counter);
}

/*
 * 静态参数：绝对路径 → host 解引用 XValue 基址 → 唯一推导 body 地址。
 * 路径文本同时登记进符号池：vmbin 阶段按 key 访问，slot 就是符号下标。
 */
runtimec_status_t vmkv_encode_static_argument(
    const vmkv_encode_request_t *request, binrwir_program_builder_t *builder,
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
        source_argument->parameter_type_name.size == 0U ||
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
    if (request->host_services->resolve_static_xvalue_address == NULL) {
        return vmkv_encoder_fail(
            error, RUNTIMEC_STATUS_NOT_IMPLEMENTED,
            VMKV_ENCODER_DIAG_HOST_CAPABILITY_MISSING,
            "vmkv encoder: host cannot resolve static xvalue addresses",
            RUNTIMEC_PROGRAM_COUNTER_NONE);
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
    output_argument->reserved_8_bits = 0U;
    output_argument->slot = symbol_index;
    output_argument->payload = body_address;
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 取下一个「非形参引用」的 Program 内唯一 argidx。 */
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

/*
 * 一个实参的引用形态：内联字面量 → 形参引用 → 静态绝对路径 → 名字。
 * 名字按操作分两种：`call` 的第一个读参是函数名（FUNCTION），其余是帧局部变量名
 * （LOCAL）；两者都只把名字登进符号池，`/lib/` 前缀由 vmbin 拼。
 */
static runtimec_status_t vmkv_encoder_map_argument(
    const vmkv_encode_request_t *request, binrwir_program_builder_t *builder,
    const vmkv_local_types_t *local_types, vmkv_encode_function_t *function,
    const vmkv_rwir_argument_view_t *source_argument, bool function_target,
    binrwir_argument_t *output, runtimec_program_counter_t logical_program_counter,
    runtimec_error_t *error) {
    *output = (binrwir_argument_t){0};
    uint32_t argument_index = 0U;
    runtimec_status_t status = vmkv_encoder_take_operand_argument_index(
        function, &argument_index, logical_program_counter, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (source_argument->literal.bytes.data != NULL) {
        uint32_t literal_index = BINRWIR_LITERAL_INDEX_NONE;
        status = binrwir_program_builder_add_literal(
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
        return vmkv_encode_static_argument(request, builder, source_argument,
                                           argument_index, output, error);
    }
    uint16_t parameter_ordinal = 0U;
    bool writes_parameter = false;
    if (source_argument->parameter_name.size != 0U &&
        vmkv_encoder_parse_parameter_reference(
            source_argument->key, &parameter_ordinal, &writes_parameter) &&
        (writes_parameter
             ? parameter_ordinal <= function->write_parameter_count
             : parameter_ordinal <= function->read_parameter_count)) {
        /* canonical argidx：读参 0..nr-1，写参 nr..nr+nw-1；slot 是该方向形参
         * 表里的序号（0 起），与 `*[0,±k]` 的 k 相差 1。形参引用不消耗
         * next_operand_argument_index，把刚取到的那个序号还回去。 */
        function->next_operand_argument_index -= 1U;
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
    /* 帧内槽表达式 `*[d,±k]`（d 非 0）或畸形指针串：没有可编译的槽契约。 */
    if (source_argument->key.size != 0U && source_argument->key.data[0] == '*') {
        return vmkv_encoder_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, VMKV_ENCODER_DIAG_DYNAMIC_KEY,
            "vmkv encoder: framed slot expression is not representable",
            logical_program_counter);
    }
    /* 名字：调用目标是函数名，其余是帧局部变量名。两者都只登名字进符号池。 */
    const runtimec_string_view_t name = runtimec_string_view_make(
        source_argument->key.data, source_argument->key.size);
    runtimec_string_view_t symbol_type =
        source_argument->parameter_type_name;
    if (!function_target) {
        /* 局部槽的大小由 route 按这里登记的类型创建，所以类型必须来自
         * 「该名字在写位置被赋予的那一个类型」，不是本条指令的用法。 */
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
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 一条已解析操作的编码状态，避免在函数间反复传长参数表。 */
typedef struct vmkv_encoder_operation {
    const vmkv_rwir_capability_t *capability;
    const vmkv_rwir_instruction_view_t *source;
    runtimec_program_counter_t logical_program_counter;
} vmkv_encoder_operation_t;

/*
 * 这条能力行是否**需要**把操作数分类才能算出结果。
 *
 * 分类只在需要计算的地方才必要：结果类型由操作数推出（SAME_FIRST / WIDEN…），
 * 或者要判操作数之间的兼容（SAME_CLASS / NUMERIC_COMPATIBLE / NUMERIC_COMPARE）。
 * IDENTITY + NONE 的行（time·now、time/duration·as_nanos 等）类型只是跟着走，
 * 不需要进类别表——为不需要的东西付失败封闭的代价是错的。
 */
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
        /* 并集与空类型在任何位置都不接受：那不是「不分类」，是「没有类型」。 */
        if (result == VMKV_PARAMETER_TYPE_RESULT_UNION) {
            return vmkv_encoder_fail(
                error, RUNTIMEC_STATUS_UNSUPPORTED,
                VMKV_ENCODER_DIAG_UNION_PARAMETER,
                "vmkv encoder: parameter has no concrete langtype",
                operation->logical_program_counter);
        }
        if (!needs_classification) {
            /* 透传位置：只要具体即可，不要求落进类别表。 */
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
    /*
     * 结果类型不由操作数推出（IDENTITY / NONE）时，写参类型就是槽上写的那个。
     * 这里只要求它**是一个具体类型文本**，不要求落进 19 位类别表——与读参侧的
     * 分层同一原则：不参与计算的位置，类型只是跟着走。
     */
    for (size_t ordinal = 0U; ordinal < source->write_argument_count;
         ++ordinal) {
        const runtimec_string_view_t declared =
            source->write_arguments[ordinal].parameter_type_name;
        if (declared.size == 0U || declared.data == NULL ||
            memchr(declared.data, '|', declared.size) != NULL) {
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

#ifdef RUNTIMEC_TEMP_PRINTLN
/*
 * **临时**：三个输出算子的名字 → 保留区 opid。名单只在这里出现一次；
 * 开关关闭时整段不参与编译，`println` 回到 FOREIGN 原行为。
 *
 * opid 取 0xFFFF0001..0xFFFF0003——不是能力表编号，与 FOREIGN(0xFFFFFFFE) 不撞。
 */
static int vmkv_encoder_temp_output_identifier(runtimec_string_view_t name,
                                               uint32_t *output_identifier) {
    static const struct {
        const char *name;
        uint32_t identifier;
    } table[] = {
        {"println", UINT32_C(0xFFFF0001)},
        {"print", UINT32_C(0xFFFF0002)},
        {"cerr", UINT32_C(0xFFFF0003)},
    };

    *output_identifier = 0U;
    for (size_t ordinal = 0U; ordinal < sizeof(table) / sizeof(table[0]);
         ++ordinal) {
        const size_t size = strlen(table[ordinal].name);
        if (name.size == size &&
            memcmp(name.data, table[ordinal].name, size) == 0) {
            *output_identifier = table[ordinal].identifier;
            return 1;
        }
    }
    return 0;
}

/*
 * **临时**：按调用点的实参个数展开成 N 个定长读参槽。
 *
 * 每个实参走既有 `vmkv_encoder_map_argument`：独立取 argidx、独立登记
 * 字面量/符号池、按引用种类（LITERAL / LOCAL / STATIC / 形参）编码。
 * 因此展开不改变任何布局——帧槽容量只由签名与符号池决定，实参不进帧槽。
 *
 * 类型表按「不做类型收敛」处理：输出算子的实参类型由各自值决定，编码期不裁剪。
 */
static runtimec_status_t vmkv_encoder_encode_temporary_output(
    vmkv_encoder_t *encoder, const vmkv_encode_request_t *request,
    binrwir_program_builder_t *builder,
    vmkv_encode_function_t *function,
    const vmkv_rwir_instruction_view_t *source_instruction,
    uint32_t operation_identifier,
    runtimec_program_counter_t logical_program_counter,
    vmkv_canonical_instruction_t *output_instruction,
    runtimec_error_t *error) {
    const size_t read_count = source_instruction->read_argument_count;
    runtimec_status_t status = vmkv_encoder_reserve(
        encoder, read_count == 0U ? 1U : read_count, error);

    (void)builder;
    (void)function;
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    for (size_t ordinal = 0U; ordinal < read_count; ++ordinal) {
        const vmkv_rwir_argument_view_t *source_argument =
            &source_instruction->read_arguments[ordinal];
        binrwir_argument_t *target = &encoder->arguments[ordinal];

        if (source_argument->literal.bytes.data != NULL) {
            uint32_t literal_index = BINRWIR_LITERAL_INDEX_NONE;

            status = binrwir_program_builder_add_literal(
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
            request, builder, NULL, function, source_argument, false, target,
            logical_program_counter, error);
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
#endif /* RUNTIMEC_TEMP_PRINTLN */

runtimec_status_t vmkv_encode_instruction(
    vmkv_encoder_t *encoder, const vmkv_encode_request_t *request,
    binrwir_program_builder_t *builder,
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
    /* 非形参 argidx 必须从形参区间之后开始，否则会和签名编号相撞。 */
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
    /*
     * 「这条是不是函数调用」由 **opcode 槽自己的 langtype** 决定：layout 按目标在
     * 生成时选定——调用槽是 `rwfunc`，算子槽是 `rwir`。不用名字形状猜：拼错的裸名
     * 也可能以 `/lib/` 开头，那会被误当成合法调用。
     *
     * 调用没有 surface 名可查（body 里是目标函数路径，不是算子名），所以直接落到
     * `call` 这条能力行，第一个读参按 FUNCTION 引用编码。
     */
    const bool is_rwfunc_call = vmkv_rwir_manifest_is_rwfunc_call(
        source_instruction->operation_langtype);
    const vmkv_rwir_capability_t *capability =
        is_rwfunc_call
            ? vmkv_rwir_manifest_capability(RUNTIMEC_RWIR_CALL)
            : vmkv_rwir_manifest_find_operation(
                  source_instruction->operation_name);
    if (capability == NULL) {
#ifdef RUNTIMEC_TEMP_PRINTLN
        /*
         * **临时**：`println` / `print` / `cerr` 三个输出算子就地实现，仅为让
         * tutorial 的 runtimec 分组能被 runtimec 自己跑通。正式实现归 byteseek
         * 扩展 runtime（issue #622 决策 D），它们的进程内 opcode 刻意不进
         * myrwircaps 能力表。开关默认 OFF；关闭时本分支不参与编译，三条算子回到
         * 下面的 FOREIGN 原行为。
         *
         * 形状：`def rwir` 声明 nr=1 + dynamic=1（layout 就这么产，别改它），
         * 编码期按**调用点的实参个数**展开成 N 个定长读参槽——每槽独立取 argidx、
         * 独立登记字面量/符号，与其它非形参引用走同一条路。运行期看到的永远是
         * 一条 N 槽的普通指令，不需要知道「变参」这回事。
         *
         * 展开 N 槽**不改变任何布局**：next_operand_argument_index 本来就是每个
         * 非形参引用取一个并单调递增；帧槽容量只由签名与符号池决定，实参照常走
         * 符号池/字面量池，不进帧槽。
         */
        uint32_t temporary_identifier = 0U;
        if (vmkv_encoder_temp_output_identifier(
                source_instruction->operation_name, &temporary_identifier)) {
            runtimec_status_t temp_status =
                vmkv_encoder_encode_temporary_output(
                    encoder, request, builder, function, source_instruction,
                    temporary_identifier, logical_program_counter,
                    output_instruction, error);
            if (temp_status != RUNTIMEC_STATUS_SUCCESS) {
                return temp_status;
            }
            return RUNTIMEC_STATUS_SUCCESS;
        }
#endif
        /*
         * 不在 alias 表里 = 本 runtime 不提供的能力（外部 runtime 就地实现，例如
         * println）。照常留在 Program 里，编成 FOREIGN 结构标记：只携带 source pc，
         * 零读写参数、不进签名、不做类型收敛，因此它的操作数既不登符号池也不
         * 消耗 argidx——这条指令一个字节都不由本 runtime 执行。
         */
        output_instruction->operation_identifier =
            BINRWIR_OPERATION_IDENTIFIER_FOREIGN;
        output_instruction->logical_program_counter =
            source_instruction->logical_program_counter;
        output_instruction->read_arguments = (binrwir_argument_span_t){0};
        output_instruction->write_arguments = (binrwir_argument_span_t){0};
        output_instruction->read_parameter_types = NULL;
        output_instruction->write_parameter_types = NULL;
        output_instruction->aliasing_declared = false;
        return RUNTIMEC_STATUS_SUCCESS;
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
    /*
     * `rwfunc` 调用在读参区最前多占一个 FUNCTION 引用（目标函数）。所有「读参之后」
     * 的偏移都必须带上它，否则写参会落到读参的槽位上——那不会报错，只会把写参
     * 当成一个类型串拷贝，产生 kind=INVALID 的参数项。
     */
    /*
     * 参数池的分区布局**只在下面这一处算**。
     *
     * 读参区在调用时多一个 FUNCTION 引用（目标函数），写参区紧随其后右移一格。
     * 三处各自加 offset 是错的：漏加的那一处不会报错，只会把类型字符串当成参数项
     * 拷进去，产出一个 kind=INVALID 的参数——数据错了但流程照走。
     */
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

    /*
     * `rwfunc` 调用：目标函数路径在 **opcode 槽的 body** 里（layout 的
     * `new_rwfunc_call` 把计数头与路径写在一起），不在读参里——读参是这次调用的
     * 真实实参。所以先追加一个 FUNCTION 引用指向符号池里的目标路径，再映射实参。
     *
     * 位置不要求在最前：vmbin 是在读参里**扫描** FUNCTION 引用（`interpreter.c`
     * 遍历到即取目标），不是固定取第一个。
     */
    if (is_rwfunc_call) {
        /*
         * 符号池只存**函数名**，不含 `/lib/`（`binrwir/argument.h` 对
         * BINRWIR_REFERENCE_FUNCTION 的约定；路径前缀由 vmbin 在调用入口拼）。
         * opcode 槽的 body 是完整路径，这里剥掉 `/lib/`。
         */
        runtimec_string_view_t target_name = source_instruction->operation_name;
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
            builder, target_name, source_instruction->operation_langtype,
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
        /* FUNCTION 引用不参与值类型：签名里没有它，类型表留空视图。 */
        encoder->argument_types[0] = runtimec_string_view_make(NULL, 0U);
    }
    for (size_t ordinal = 0U; ordinal < read_count; ++ordinal) {
        status = vmkv_encoder_map_argument(
            request, builder, local_types, function,
            &source_instruction->read_arguments[ordinal], false,
            &encoder->arguments[target_arguments + ordinal],
            logical_program_counter, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
        encoder->argument_types[target_arguments + ordinal] =
            source_instruction->read_arguments[ordinal].parameter_type_name;
    }
    for (size_t ordinal = 0U; ordinal < write_count; ++ordinal) {
        status = vmkv_encoder_map_argument(
            request, builder, local_types, function,
            &source_instruction->write_arguments[ordinal], false,
            &encoder->arguments[write_region_begin + ordinal],
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
    /* 调用多一个 FUNCTION 引用（目标函数），它在分区最前。 */
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
                RUNTIMEC_PROGRAM_COUNTER_NONE);
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
                RUNTIMEC_PROGRAM_COUNTER_NONE);
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
                                 RUNTIMEC_PROGRAM_COUNTER_NONE);
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
            RUNTIMEC_PROGRAM_COUNTER_NONE);
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
            RUNTIMEC_PROGRAM_COUNTER_NONE);
    }
    encoder->alias_rule_count = 0U;
    status = vmkv_encoder_register_parameters(builder, function, error);
    /* 局部槽的类型必须在编码任何指令之前定下来：route 按它建槽，vmbin 没有类型。 */
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
    /*
     * 覆盖完整性由读取器保证，编码器不重复判：`vmkv_rwir_reader_next` 从 1 起逐行
     * 递增，`instruction_line_count` 是最大行号，任何一行缺 opcode 槽都直接
     * MISSING_OPCODE 失败。因此这里收到的 source pc 恒为无洞升序序列，FOREIGN 也
     * 照此逐条占位。在这一层再加一遍判据就是为不存在的分支写代码。
     */
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
        status = binrwir_program_builder_add_instruction(
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
        status = binrwir_program_builder_add_alias_rule(
            builder, &encoder->alias_rules[ordinal], &alias_rule_identifier);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            status = vmkv_encoder_fail(
                error, status, VMKV_ENCODER_DIAG_PROGRAM_REJECTED,
                "vmkv encoder: program builder rejected an alias rule",
                RUNTIMEC_PROGRAM_COUNTER_NONE);
        }
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = binrwir_program_builder_finalize(builder, output_program);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            status = vmkv_encoder_fail(
                error, status, VMKV_ENCODER_DIAG_PROGRAM_REJECTED,
                "vmkv encoder: program builder rejected the program",
                RUNTIMEC_PROGRAM_COUNTER_NONE);
        }
    }
    binrwir_program_builder_destroy(builder);
    vmkv_source_close_function(function);
    return status;
}
