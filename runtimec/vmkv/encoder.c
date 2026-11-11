/*
 * 组件：vmkv/encoder 的实现
 * 作用：明文 RWIR → canonical 指令 → canonical BinRWIR。别名表定 opid，能力行
 *       掩码定参数类型，结果规则定写参类型，参数引用形态定 reference kind。
 * 约束：只做等价编码。不优化、不融合、不生成执行计划或机器码，不执行，不缓存
 *       V.body；任何不确定处失败封闭，不用默认提升或占位地址换取成功。
 */


#include "vmkv/encoder.h"

#include <stdio.h>
#include <string.h>

#include "contract/error.h"
#include "contract/kvspace_access.h"
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
           request->kvspace_access != NULL &&
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
    /*
     * `*` 已由上层剥掉（解引用记进 dereference 位），这里只认槽文本 `[0,±k]`。
     * 两种写法都收：`*[...]`（未剥的历史形态）与 `[...]`（当前形态）——
     * 但**只把 `[0,` 当形参槽**：`[d,±k]` 的 d≠0 是帧坐标表达式，归上层拒绝。
     */
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

/*
 * 静态参数：绝对路径 → host 解引用 XValue 基址 → 唯一推导 body 地址。
 * 路径文本同时登记进符号池：vmbin 阶段按 key 访问，slot 就是符号下标。
 */
static runtimec_status_t vmkv_encode_static_argument_into(
    const vmkv_encode_request_t *request, binrwir_binfunc_builder_t *builder,
    const vmkv_rwir_argument_view_t *source_argument,
    uint32_t argument_index, binrwir_argument_t *output_argument,
    runtimec_error_t *error) {
    if (output_argument != NULL) {
        memset(output_argument, 0, sizeof(*output_argument));
    }
    /*
     * **`parameter_type_name` 允许为空**：STATIC 读参（`/lib/math·Pi`）的类型是
     * 运行态事实，源码给不出来（裁决 `decision-type-from-target-local.md`）。
     * 其余检查照旧——key 必须有效、builder 必须在。
     */
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
        runtimec_kvspace_access_resolve_static_xvalue_address(
            request->kvspace_access, &request->expected_snapshot,
            source_argument->key, &xvalue_address, error);
    /*
     * key 是否**存在**不是可静态判定的属性：程序自己会创建 key（同一函数内
     * 先写后读就是常态）。契约里「尚未分配的 key 必须返回 UNSUPPORTED」正是
     * 这个信号——此时仍登记符号（key 是执行期解析/创建的依据），payload 记
     * NONE，由执行期按 key 解析（写目标不存在则创建）。
     *
     * 失败时机下移、判据不下移：执行期解析不到仍然明确失败（见值访问层），
     * 只是不再把「编码时还不存在」当成错误。
     */
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
/*
 * 动态成员键的诊断消息**必须带实际键文本**。
 *
 * 只写一个字面量 `` `base·*k` `` 的话，下一个人要知道「是哪条键」得开 gdb——
 * 而这条判据拦下来的恰恰是 layout 脱糖出来的临时键（`data·*_8`、`names·*_16`），
 * 名字里带循环临时变量，**不打开看根本猜不到**。
 *
 * 键是 `runtimec_key_view_t`（data + size），**不是 NUL 结尾**：按 size 截断。
 */
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

/* langtype 去掉开头的 `[dims]` 形状段之后是否等于 `name`。 */
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

/*
 * 字面量是不是「字符文本」：langtype 是 `char/utf8` / `char/utf32` / `char/ascii`，
 * 或者带形状段的 `[N]char/utf32`（数组字面量的形态）。
 *
 * 为什么不能直接比前缀：实测 `"/tmp/probe/"` 的 langtype 是 `[11]char/utf32`——
 * **带 `[N]` 形状段**。比 `memcmp("char", 4)` 会漏掉这一类。
 */
static bool vmkv_encoder_literal_is_character_text(
    runtimec_string_view_t langtype) {
    if (langtype.data == NULL || langtype.size < 4U) {
        return false;
    }
    size_t start = 0U;
    /* 跳过开头的 `[dims]`：形状段不含字母，找到第一个 `c` 就是 `char`。 */
    while (start < langtype.size && langtype.data[start] == '[') {
        while (start < langtype.size && langtype.data[start] != ']') {
            ++start;
        }
        if (start < langtype.size) {
            ++start; /* 跳过 `]` */
        }
    }
    if (langtype.size - start < 4U) {
        return false;
    }
    return memcmp(langtype.data + start, "char", 4U) == 0;
}

/*
 * 取某个参数位置的语义角色。`roles` 是逗号分隔的文本（`"key"`、`"key,"`、空），
 * 第 i 项对第 i 个参数。越界或空 → ANY。
 *
 * 为什么用文本而不是位集：预处理器传不了带逗号的小列表——花括号里的逗号会被当成
 * 实参分隔符，圆括号又会被原样保留成逗号表达式。字符串里逗号没有特殊含义。
 * 解析在这里做，不在生成期展开：表是静态的，一次线性扫描，代价可忽略。
 */
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
            /* 只有恰好 `key` 三个字节才算 KEY；其余（含空项）都是 ANY。 */
            return length == 3U && roles.data[start] == 'k' &&
                   roles.data[start + 1U] == 'e' && roles.data[start + 2U] == 'y';
        }
        ++position;
        if (cursor >= roles.size) {
            break;
        }
        ++cursor; /* 跳过逗号 */
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
    /*
     * `*p` 的 `*` 是**引用标记**，不是键文本的一部分：剥掉它，把「跟一跳」记在
     * 参数项的 dereference 位上。不剥的后果实测过——它被当成畸形帧槽串整条拒绝
     * （`framed slot expression is not representable`），`&x -> p; *p` 整族进不了
     * BinRWIR。
     *
     * **`*` 一律不在 key 文本里**：`*name`（局部/静态解引用）与 `*[0,±k]`
     * （形参槽解引用）在这里统一剥掉，含义只由 dereference 位承载——同一个含义
     * 两种编码，消费者就要记两套规则，那是「一个编码一个含义」的反面。
     * 剥完的 `[0,±k]` 继续交给形参解析；`[d,±k]`（d≠0）仍是不可表示的帧槽。
     * `**p` 目前没有语义，失败封闭而不是猜「跟两跳」。
     */
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
    /*
     * **key 位置上的字符串是这个位置的键，不是内联值数据。**
     *
     * 判据来自能力行的 `read_roles`/`write_roles`（catalog 是唯一权威），不是
     * 「内容以 `/` 开头」这类脏猜——`"/tmp/x"` 在别处可以只是一个普通字符串。
     *
     * 必须在 literal 分支**之前**判：字面量分支一命中就 return，走不到后面的
     * STATIC 分支。这正是 `0x4603` 的成因——`kvspace·mkindex("/tmp/x/")` 的实参
     * 被编成 LITERAL，而 handler 只接受 STATIC。
     *
     * 只对 char 族的字符串字面量生效：数字字面量放在 key 位置上是类型错误，
     * 那不是本判据要处理的，交给后面的类型校验失败封闭。
     */
    if (parameter_is_key && source_argument->literal.bytes.data != NULL &&
        vmkv_encoder_literal_is_character_text(
            source_argument->literal.langtype)) {

        /*
         * `literal.bytes` 是**整条 wire**（head + body + attr），不是 body
         * （`vmkv/rwir.c:193` 直接给 `slot->wire`）。所以长度取 head 里的
         * bodylen，起点跳过 16 字节头——直接 memcpy 会把 bodylen 与 attrslen
         * 当成路径开头的两个字节（实测符号表里存进去的就是 `,`＝0x2c）。
         */
        char key_text[1024];
        const uint64_t wire_size = (uint64_t)source_argument->literal.bytes.size;
        if (wire_size < KVSPACE_XVALUE_HEAD_LEN) {
            return vmkv_encoder_fail(
                error, RUNTIMEC_STATUS_INVALID, VMKV_ENCODER_DIAG_INVALID_REQUEST,
                "vmkv encoder: key-position literal wire is truncated",
                logical_program_counter);
        }
        const kvspaceXvalueHead *literal_head =
            (const kvspaceXvalueHead *)(const void *)source_argument->literal.bytes.data;
        const uint8_t *text_bytes =
            source_argument->literal.bytes.data + KVSPACE_XVALUE_HEAD_LEN;
        const size_t text_size = (size_t)kvspaceXvalueHeadLen(literal_head);
        if (text_size == 0U || text_size >= sizeof(key_text) ||
            KVSPACE_XVALUE_HEAD_LEN + text_size > wire_size) {
            return vmkv_encoder_fail(
                error, RUNTIMEC_STATUS_INVALID, VMKV_ENCODER_DIAG_INVALID_REQUEST,
                "vmkv encoder: key-position literal is empty or too long",
                logical_program_counter);
        }
        /*
         * 字面量的字节是**该 langtype 的编码**：`char/utf32` 是 4 字节小端码点，
         * 而 key 文本恒 utf8。直接 memcpy 会把 `2f 00 00 00` 当成四字节路径，
         * 符号表里存进去的就是垃圾。所以这里按 langtype 转一次。
         */
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
                /* 只接受 ASCII：路径/键在 runtimec 里没有非 ASCII 的先例，
                 * 猜一个码点表比拒绝更危险。 */
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
        /*
         * **类型从局部类型表查**，不能用字面量自己的 `[N]char/utf32`。
         *
         * 符号表按「同名同类型」幂等：`10 -> /cp_src` 已把 `/cp_src` 登记为
         * `int64`；这里拿字面量的数组类型去登记就是同名异型 → 冲突（实测
         * `code=34`）。这一支与上面「局部变量必须查 local_types」是同一条规矩
         * ——key 位置的符号是那个 key 的槽，不是字面量。
         *
         * 查不到就沿用实参自带类型：那说明这个 key 还没有任何写位置，符号表里
         * 也没有同名项可与它冲突。
         */
        /*
         * **同一个物理 key 在符号池里只该有一条。**
         *
         * `10 -> /cp_src` 已经把它登记成 `int64`；这里若拿字面量自己的
         * `[7]char/utf32` 再登记一次，`add_symbol` 会按「同名异型」拒绝
         * （实测 `code=34`）。key 位置的符号是**那个 key 的槽**，类型由写位置
         * 决定，不是由「这个实参是个字符串」决定。
         *
         * 所以先按名字查：命中就复用下标（绝不重新登记），未命中才登记，且用
         * 实参自带类型（此时池里没有同名项，不可能冲突）。
         */
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
    /*
     * **判别位是 key 的形态，不是名字**：`#name` 是源码注释，落库时名字不存在，
     * `parameter_name` 恒为空。帧参数引用的唯一形态是 `*[0,±k]`，就看这个。
     */
    if (source_argument->key_addressability == VMKV_KEY_ADDRESSABILITY_DYNAMIC &&
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
        /*
         * **形参引用上的 `*` 是引用语法，不是解引用**：`*[0,±k]` 表示「调用方
         * 第 k 个槽」，槽文本剥星只是统一写法；真正是否跟指针由调用方那个值
         * 自己决定（执行期按 head ptr 判）。这里**不能**置 dereference——
         * 置了就等于「所有形参引用都要跟一跳」，实测会让 10 条函数调用用例
         * 在 pc=1 报「解引用目标不是指针」。
         */
        output->dereference = 0U;
        return RUNTIMEC_STATUS_SUCCESS;
    }
    /*
     * 帧内槽表达式 `[d,±k]`（d 非 0）或畸形串：没有可编译的槽契约。
     *
     * `*` 已经在这一层**统一剥掉**并记进 dereference 位（`*name` 与 `*[0,±k]`
     * 是同一种含义，不许两种编码），所以走到这里还以 `[` 开头、又没被形参解析
     * 认领的，只能是帧坐标表达式。
     */
    if (source_argument->key.size != 0U && source_argument->key.data[0] == '[') {
        return vmkv_encoder_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED, VMKV_ENCODER_DIAG_DYNAMIC_KEY,
            "vmkv encoder: framed slot expression is not representable",
            logical_program_counter);
    }
    /*
     * 动态成员键 `base·*k`：**失败封闭，不许字面化**。
     *
     * 它必须在登记符号**之前**拦下——一旦写成符号池条目，写和读就用同一个
     * 错误键，整条链上再也看不出「键取错了」（`输出算子(m·a)` 得 0，而同写法的
     * `输出算子(m·*k)` 得 10，即 p1 说的「以奇怪的方式成功」）。
     *
     * 三种「以 `*` 出现」的形态在这里互不重叠：
     *   `*[0,±k]` 形参引用 —— 上面已经 return；
     *   `*[d,±k]` 帧坐标表达式 —— 下面第二支自己拦（以 `*` 开头）；
     *   `base·*k` 动态成员键 —— 本支拦（以 `·*` 出现，不在开头）。
     */
    if (vmkv_rwir_key_is_dynamic_member(source_argument->key)) {
        return vmkv_encoder_dynamic_member_fail(
            error, source_argument->key, logical_program_counter);
    }
    /* 名字：调用目标是函数名，其余是帧局部变量名。两者都只登名字进符号池。 */
    const runtimec_string_view_t name = runtimec_string_view_make(
        source_argument->key.data, source_argument->key.size);
    runtimec_string_view_t symbol_type =
        source_argument->parameter_type_name;
    /*
     * **成员键不是局部变量**，不查「写位置赋予的类型」（spec `05-runtime语义/
     * 03-成员访问`：`base·name` 是独立物理 key，不是帧里的变量槽）。
     * 它的槽类型就取**本条指令声明的类型**——读成员得到的是那个成员的值类型。
     *
     * 而局部变量必须查：局部槽的大小由 route 按写位置的类型创建，用本条指令的
     * 用法会让同一变量在不同指令处建成不同大小。
     */
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

/* 公开入口：读参语义（key 必须已存在）。写参走 `_into(..., true)`。 */
runtimec_status_t vmkv_encode_static_argument(
    const vmkv_encode_request_t *request, binrwir_binfunc_builder_t *builder,
    const vmkv_rwir_argument_view_t *source_argument,
    uint32_t argument_index, binrwir_argument_t *output_argument,
    runtimec_error_t *error) {
    return vmkv_encode_static_argument_into(request, builder, source_argument,
                                            argument_index, output_argument,
                                            error);
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
        /*
         * **`ANY_SINGLE` 行豁免「必须有具体类型」**：`nonnull` / `nonnull·not`
         * 的读参恰恰**就是**一个没有类型的 None 字面量——那是它的定义域，不是
         * 缺席。若这里要求具体类型，能力行落地了却永远编不过。
         *
         * 范围严格：只有该行读侧掩码**恰好是 ANY_SINGLE 这一位**才豁免。并集、
         * 以及任何别的行都不受影响——不许把「没有类型」变成通用口子。
         */
        if (result == VMKV_PARAMETER_TYPE_RESULT_NONE &&
            vmkv_encoder_requires_classification(operation->capability) &&
            operation->capability->read_parameter_type_set ==
                VMKV_RWIR_TYPE_SET_ANY_SINGLE) {
            encoder->parameter_type_sets[ordinal] = VMKV_RWIR_TYPE_SET_NONE;
            continue;
        }
        /* 并集与空类型在任何位置都不接受：那不是「不分类」，是「没有类型」。 */
        if (result == VMKV_PARAMETER_TYPE_RESULT_UNION) {
            return vmkv_encoder_fail(
                error, RUNTIMEC_STATUS_UNSUPPORTED,
                VMKV_ENCODER_DIAG_UNION_PARAMETER,
                "vmkv encoder: parameter has no concrete langtype",
                operation->logical_program_counter);
        }
        /*
         * **STATIC 读参允许没有类型**（裁决 `decision-type-from-target-local.md`）：
         * `copy` 的读参若是一次绝对 key 引用（`/lib/math·Pi`），它的类型不可能由
         * 源码给出——那是运行态事实，只能从目标 XValue 的头取。放行它，同时把
         * 写侧槽标记成「类型来自目标」（`local_types` 那一侧已经这么标）。
         *
         * **范围严格**：只放行 `copy` 且读参是 STATIC 这一格；其余任何「没有类型」
         * 仍是具名失败——不许把缺席类型变成通用口子。
         */
        if (result == VMKV_PARAMETER_TYPE_RESULT_NONE &&
            source->read_argument_count == 1U &&
            source->read_arguments[ordinal].key_addressability ==
                VMKV_KEY_ADDRESSABILITY_STATIC) {
            encoder->parameter_type_sets[ordinal] = VMKV_RWIR_TYPE_SET_NONE;
            continue;
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
        /*
         * **写参必须有具体类型，没有例外。**
         *
         * 这里曾有一条豁免：读来源是单个 STATIC 绝对 key 时，允许写参类型缺席，
         * 由运行期从目标 XValue 头取（裁决 `decision-type-from-target-local.md`）。
         * 那条豁免已随「局部必须声明类型」的规则落地而失效——实测 246 条语料
         * （40 + 206）里，豁免分支**一次都没有被进入**，是死代码。
         *
         * 现在缺类型是**规则违约**，不是可豁免的情形：layout 必须给出具体类型。
         * 失败封闭，没有后门。
         */
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

/*
 * 三个输出算子的名字 → 保留区 opid。名单只在这里出现一次。
 *
 * **不再受 `RUNTIMEC_TEMP_OUTPUT` 控制**：那开关管的是「runtimec **库**自带实现」，
 * 而本表管的是「编码期认不认得这个名字」。宿主（驱动 / byteseek）通过扩展表提供
 * 实现，编码期照样要把名字翻成 opid——否则源码里的调用被编成 FOREIGN，
 * 执行期落进「没有这个能力」，与「宿主忘了注册」无法区分（实测过一次：
 * 现象是 `blocked op=65281`，查了半天以为扩展没注册，实际是编码期没认出编号）。
 *
 * opid 取 0xFF01..0xFF03（16 位保留区，见 binrwir/instruction.h 的占用记账）——
 * 不是能力表编号，与 FOREIGN(0xFFFE) 不撞。
 */
/*
 * 槽名是**纯十进制数字**时，把它当保留区 opid（`"65281"` → 0xFF01）。
 *
 * 为什么不是名字表：`layout/src/builtin.rs` 是「用户可见名 → 指令编号」的**唯一
 * 映射点**，它把编号写成十进制字符串。runtimec 侧若再有一张名字表，就是第二份
 * 真相——而它一定会漂移（实测：layout 早已只产数字，名字表永远匹配不到，
 * 现象是 `blocked op=65281`）。
 *
 * 越界值（> 0xFFFF）与空串一律不认，返回 0 —— 让调用方走 FOREIGN。
 */
static int vmkv_encoder_numeric_reserved_identifier(
    runtimec_string_view_t name,
    binrwir_operation_identifier_t *output_identifier) {
    *output_identifier = 0U;
    if (name.data == NULL || name.size == 0U || name.size > 5U) {
        return 0;
    }
    uint32_t value = 0U;
    for (size_t at = 0U; at < name.size; ++at) {
        /* 只认十进制数字。前导零也认（`"065281"` 不会出现，但认它不产生歧义）。 */
        if (name.data[at] < '0' || name.data[at] > '9') {
            return 0;
        }
        value = value * 10U + (uint32_t)(name.data[at] - '0');
        if (value > UINT16_MAX) {
            return 0;
        }
    }
    /* 只接受**保留区**（>= RESERVED_BASE）：基础区与特化区的编号来自能力表，
     * 槽名里直接写它们的编号意味着 layout 与能力表已经对不上了，那是错误不是功能。 */
    if (value < BINRWIR_OPERATION_IDENTIFIER_RESERVED_BASE) {
        return 0;
    }
    *output_identifier = (binrwir_operation_identifier_t)value;
    return 1;
}

/*
 * 按调用点的实参个数展开成 N 个定长读参槽。
 *
 * **不受 `RUNTIMEC_TEMP_OUTPUT` 控制**：那开关管「库自带实现」，而本函数管
 * 「怎么把这些算子的调用编成指令」。宿主通过扩展表提供实现时，编码期照样要展开。
 *
 * 每个实参走既有 `vmkv_encoder_map_argument`：独立取 argidx、独立登记
 * 字面量/符号池、按引用种类（LITERAL / LOCAL / STATIC / 形参）编码。
 * 因此展开不改变任何布局——帧槽容量只由签名与符号池决定，实参不进帧槽。
 *
 * 类型表按「不做类型收敛」处理：输出算子的实参类型由各自值决定，编码期不裁剪。
 */
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

    /* 计数非零但指针为空是畸形视图：直接失败，不往下解引用。 */
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
        /* 这条路径不在「按能力行编码实参」的位置上（它是签名槽展开），
         * 没有角色上下文：传 false，行为不变。 */
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

/*
 * 该侧在「能力行含 POINTER 位」时是否应拒绝整条指令。
 * 规则见调用点注释：单一位只信能力行；多位并集看操作数槽的声明类型；
 * 没有操作数可核对时拒绝（不猜）。
 *
 * **后两条分支今天不可达**，且这是**已知且刻意**的：catalog 里 `0x00040000`
 * 只出现 3 处（`ptreq` / `ptreq·not` 的读侧、`kvspace/key·abs` 的写侧），
 * 全是单一位，没有任何一行把 POINTER 与其它位并集。保留它们是因为它们防的是
 * 「将来有人加一条并集行」——删掉分支等于把规则一起删掉。**不要把它们算进
 * 覆盖率：没有用例能走到。**
 */
/* 视图与 C 串逐字节相等（本文件内唯一的文本比较，避免各处手写 memcmp 三件套）。 */
static bool vmkv_encoder_view_equals(runtimec_string_view_t view,
                                     const char *text) {
    const size_t size = strlen(text);

    return view.size == size && view.data != NULL &&
           memcmp(view.data, text, size) == 0;
}

/*
 * 该操作数是不是 **None 字面量**。
 *
 * 判据取自 wire 事实，不取 langtype 文本：None 的字面量是**空 body + 无 langtype**。
 * 不从 `parameter_type_name` 判——layout 已经把它补成了「另一侧的类型」（那是
 * 给编码器分类用的上下文类型），照它判会把 None 认成 `int64` 而永远不触发。
 *
 * `literal` 只有内联字面量才有效；其余引用形态（槽/形参）不可能是 None 字面量。
 */
static bool vmkv_encoder_operand_is_none(
    const vmkv_rwir_argument_view_t *argument) {
    if (argument == NULL) {
        return false;
    }
    /*
     * 判据：**参数类型名、字面量 langtype、字面量 body 三者都为空**。
     *
     * 不判 `literal.bytes.data != NULL`：空 body 的字面量在视图里 data 可能就是
     * NULL（实测过——加了这个前提，判据永不成立，归一化从不触发，`== None`
     * 静默走数值 `eq`）。空 body 的「空」由 `size == 0` 表达，`data` 是不是空
     * 指针与语义无关。
     *
     * 三空同时成立才是 None：只有 body 空而类型在，那是空字符串/空容器，不是
     * None（spec `03-类型系统/03` 的 None 判据第 2 条）。
     */
    /*
     * 判据：**参数类型名为空 + 字面量 langtype 为空 + 字面量是「None 形态的 wire」**。
     *
     * 最后一条件不能用 `bytes.size == 0`：`new_none()` 产的是**完整的 16 字节
     * 空 head wire**（body 0 字节、无 langtype 记录），bytes.size 是 16 不是 0。
     * 实测过：按 size==0 判，归一化永不触发。
     *
     * None 形态的判据与 spec `03-类型系统/03`「None 判据」同一组：head 里的
     * bodylen 为 0，且 attr 区没有 langtype 记录（attrslen 为 0 —— 一条记录
     * 都没有才叫「无 langtype」）。
     */
    if (argument->parameter_type_name.size != 0U ||
        argument->literal.langtype.size != 0U) {
        return false;
    }
    if (argument->literal.bytes.size == 0U) {
        return true; /* 完全空：调用方连 wire 都没给，按 None 处理 */
    }
    if (argument->literal.bytes.size < KVSPACE_XVALUE_HEAD_LEN ||
        argument->literal.bytes.data == NULL) {
        return false;
    }
    {
        kvspaceXvalueHead head;

        memcpy(&head, argument->literal.bytes.data, sizeof head);
        return kvspaceXvalueHeadLen(&head) == 0U &&
               kvspaceXvalueHeadAttrsLen(&head) == 0U;
    }
}

/*
 * 该操作数的类型是不是**引用**（Ptr `*T` 或 structref `/lib/X`）。
 *
 * 分岔依据：引用判空走 `ptreq`（语义是「目标路径是否为空」），非引用判空走
 * `nonnull`（语义是「这个位置有没有值」）。两者不能合流——把引用判空吞进
 * `nonnull` 会让 `p == None` 在 p 指向一个存在的目标时答错。
 */
static bool vmkv_encoder_type_is_reference(runtimec_string_view_t type_name) {
    if (type_name.size == 0U || type_name.data == NULL) {
        return false;
    }
    return type_name.data[0] == '*' || type_name.data[0] == '/';
}

/*
 * 两个读参里哪一侧是 None 字面量；都不是则返回 2。
 *
 * 两侧都是 None 时返回 0（`None == None`）——那在本步骤里会走到「另一侧也是
 * None ⇒ 不是引用 ⇒ 选 nonnull」，语义上正确：两侧都无值，`nonnull(None)` 为
 * false，`==` 取反后为 true。
 */
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
    /*
     * `None` 判空的**归一化**（在能力查找之前做，因为它要改的正是被查找的名字）。
     *
     * `x == None` / `x != None` / `m·*k != None` 在 wire 上两侧的类型不对称：
     * `None` 侧是**空 body 字面量**，没有类型可进特化表。所以它不能靠
     * `RUNTIMEC_RWIR_SPECIALIZATION`（那张表按「两侧同掩码」选行），只能在这里
     * 按「**这一侧的值是 None 字面量** + **另一侧不是引用**」两个条件改选：
     *
     *   另一侧是引用（`*T` / `/lib/X`） ⇒ 保持 `==`/`!=`，由特化表选 `ptreq`
     *                                     （引用判空是另一套语义，不能吞掉）
     *   另一侧不是引用               ⇒ 改选 `nonnull`（`!=`）/ `nonnull·not`（`==`）
     *
     * 这不是「在编码层再造一套算子选择」：特化表表达不了「一侧无类型」的形态，
     * 而 layout 已经把这个操作数的**上下文类型**补好了（`code.rs` 的 none_type），
     * 本步只把「值是 None」这一个事实翻成算子名。
     *
     * 与数值 `eq` 的分界也在这一步：`None` 判空若回落到 `eq`，会把 None 当 0 比较
     * ——空 body 读成零值，那是静默错误。
     */
    runtimec_string_view_t operation_name = source_instruction->operation_name;
    /*
     * 归一化会同时**改算子名与实参形状**——两件事必须一起做，只改名字就是当前
     * 那个违约：指令声称自己是 `nonnull`（一元），却带着 `==` 的两条实参。
     *
     * 手法照抄本文件已有的 `dereferenced_view`：**派生一份视图**，后续流程全部读
     * 它。这样 8 处按 `read_argument_count` 循环的地方一处都不用改——它们本来
     * 就从视图里读条数。
     */
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
                /*
                 * `nonnull` / `nonnull·not` 是**一元**：它的定义域是「这个位置
                 * 有没有值」，只有一个操作数有意义。丢掉 None 那一侧，留下的
                 * 就是被检查的那个值。
                 *
                 * `read_arguments` 与 `read_argument_count` 一起改，其余字段
                 * （opcode langtype、pc、写参）原样保留。
                 */
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
        /*
         * **保留区编号由 layout 直接写在槽名里**（`layout/src/builtin.rs`：
         * 「输出算子名 → `"65281"`，即 0xFF01；runtimec 的 encoder 只按『槽名全为
         * 数字即 opid』处理，一个算子名也不认识」）。
         *
         * 所以本分支认的是**十进制数字**，不是名字。这一条是本仓与 layout 之间
         * 的契约，写反了就会两头都不认：layout 已经不产名字了（早先按名字匹配，
         * 实测拿到的是 `"65281"`，永远匹配不到）。
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
        /*
         * 不在 alias 表里 = 本 runtime 不提供的能力（外部 runtime 就地实现，例如
         * 输出算子）。照常留在 Program 里，编成 FOREIGN 结构标记：只携带 source pc，
         * 零读写参数、不进签名、不做类型收敛，因此它的操作数既不登符号池也不
         * 消耗 argidx——这条指令一个字节都不由本 runtime 执行。
         */
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
    /*
     * 指针性与扩展类别不在 langtype 文本里（指针性在 XValue head 的 ptr 位）。
     *
     * **两者的拒绝条件不同，不许合成一个判据**：
     *   - EXTENSION（bit17）是**存储形态**，本阶段仍不支持，照旧整条拒绝；
     *   - POINTER（bit18）是**值类别**。能力行把它写在**写侧**且掩码只有这一位时
     *     （`kvspace/key·abs`，`rwir_catalog.def:963`），含义是「结果一定是指针」，
     *     这类指令的参数并没有裸指针文本——整条拒绝就把 `&x` 挡死了。
     *
     * 判据按可得信息分三层（拿不到就失败封闭；不去 `/lib/<op>` 另读一份签名，
     * 那是第二份真相）：
     *   1. 该侧掩码**只有 POINTER 一位** ⇒ 信能力行，放行；
     *   2. 该侧是**多位并集** ⇒ 不看行，改看该侧每个操作数槽自己的声明类型，
     *      任一以 `*` 开头即拒绝；
     *   3. 该侧没有任何操作数却声称含 POINTER ⇒ 无从核对，拒绝。
     */
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

    /*
     * `ANY_SINGLE` 行**跳过掩码匹配**：它的读参就是没有类型的 None 字面量
     * （`parameter_type_sets` 是 `NONE`），按位与任何具体位都是 0。要求匹配等于
     * 要求「任意单一类型」这个标记必须列出全部具体类型——那正是它要避免的事。
     *
     * 范围严格：只有掩码**恰好是 ANY_SINGLE 这一位**才跳过；并集与其它行不受影响。
     */
    if (selected_capability->read_parameter_type_set ==
        VMKV_RWIR_TYPE_SET_ANY_SINGLE) {
        /* 见上：无类型读参是这个能力行的定义域，不参与掩码匹配。 */
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
        /* FUNCTION 引用不参与值类型：签名里没有它，类型表留空视图。 */
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
        request->kvspace_access, request->function_path, &function, &snapshot,
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
