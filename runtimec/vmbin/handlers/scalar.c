/*
 * 组件：vmbin/handlers/scalar 的实现
 * 作用：标量值算子与字符/指针比较的 handler 表——四则、取余、位运算、极值、
 *       一元、超越函数、逻辑、强制转换、字符串拼接与相等、指针相等、debugger。
 * 联系：条目由 session 装进 `binrwir_registry_t`；值经 `vmbin/value_access`
 *       按地址读写，与 core 规则共用同一条数据面。
 * 约束：no-malloc（只用定长栈缓冲）；不缓存 V.body；操作数类型只从 Program 取
 *       （签名的具体 langtype、字面量池、符号池），不猜、不从 body 长度反推
 *       有符号性与浮点性。
 *
 * ── 文件内导航（26 条注册，按共用实现体分组）────────────────────────────
 *
 * 每条 handler 都是薄包装（宏展开后 8~10 行），真正的逻辑在「实现体」里。
 * 分组依据**语义族**，不是 langtype：实现体内没有按 langtype 的 switch，
 * 分叉只发生在 `scalar_class`（signed / unsigned / float / boolean）。
 *
 * | 实现体 | 行数 | 覆盖的算子 | 分叉判据 |
 * |---|---:|---|---|
 * | `scalar_min_max`    |  82 | max min | NaN 让位于数（matrix:69） |
 * | `scalar_unary_integer` | 64 | abs neg sign | 整数回绕（matrix:31/60） |
 * | `scalar_transcendental` | 46 | sqrt exp log | 无分叉，结果恒 float64（matrix:70） |
 * | `scalar_shift`      |  65 | shl shr | 计数低 6 位掩码（matrix:71） |
 * | `scalar_compare`    | 105 | eq neq lt le gt ge | 单一比较核心 + 方向/取反（6 条共用一个） |
 * | `scalar_logical`    |  39 | and or not | 非零即真、不短路（matrix:78） |
 * | `scalar_cast`       | 123 | cast | 目标类别 × 源类别（matrix:21/26/35/63） |
 * | `scalar_concat`     |  40 | concat | 无分叉，两段 UTF-8 相接 |
 * | `scalar_streq`      |  39 | streq streq·not | 跨编码比较（matrix:65） |
 * | `scalar_boolean_equality` | 47 | bool·eq bool·neq | 只接受 bool（matrix boolean-equality） |
 * | `scalar_handle_debugger` | 44 | debugger | 写 ‥status=paused + YIELD |
 *
 * 辅助件：
 *   `scalar_type_of_langtype`（33）  langtype 串 → {类别, 宽度}
 *   `scalar_type_of_argument`（22）  参数项 → 类型，统一走 `value_access_langtype`
 *   `scalar_read_operand`（21）      读一个操作数到栈缓冲 + 取类型
 *   `scalar_result_type`（19）       两侧类型 → 结果类型（matrix:77 提升规则）
 *   `scalar_load` / `scalar_store`   按宽度与符号性在整数/浮点表示间转换
 *   `scalar_char_operand`（24）      字符操作数 → UTF-8（唯一转换点 char_codec）
 *   `scalar_fail`（8）               统一错误写入
 *
 * ── 类型规则（five-language-behavior-matrix.md）─────────────────────────
 *   :77 mixed-precision-promotion-order-independent — 结果宽度取较宽操作数；
 *       任一侧浮点则 float64；规则与操作数顺序无关；有符号与无符号混用 INVALID。
 *   :22 signed-integer-division — 商向零截断，零除数 INVALID。
 *   :68 integer-remainder-sign — 余数随被除数符号，零除数 INVALID。
 *   :29 float-division-by-zero — IEEE inf/nan，算出来，exit 不带错误。
 *   :26 uint64-to-float-promotion — 按无符号值转换，不经 int64。
 *   :69 floating-min-max-nan — NaN 让位于数，两个 NaN 得 NaN。
 *   :70 float-transcendental-result-kind — sqrt/exp/log 结果恒 float64。
 *   :71 shift-count-masking — 计数按宽度减一掩码，有符号右移是算术移位。
 *   :78 logical-operators-truthy — 数值非零即真；结果恒 bool；**不短路**
 *       （实参执行前已求值，短路无处安放）。
 *   :21 floating-to-integer-truncation — 向零截断，越界与 ±Inf 饱和，NaN 报错。
 *   :35 bool-cast-strict — 目标 bool 只接受 bool 操作数。
 *   :63 cast-target-kind — 一次转换，目标类别从写参数项的 langtype 取。
 *   :64 character-concat-kind — 结果恒 char/utf8。
 *   :65 character-equality-by-text — 按解码后的码点序列比较，种类可以不同。
 *   :66 pointer-equality — 按目标 key 比身份；None equals None；指针对非指针 INVALID。
 *       （ptreq 走 `vmbin_value_access_form`）
 *
 * opid 与 catalog 行号见下方 `scalar_entries` 各处注释。
 */

#include "vmbin/handlers/scalar.h"
#include "vmbin/handlers/scalar_special.h"

#include <math.h>
#include <string.h>

#include "binrwir/execution_exit.h"
#include "binrwir/binvthread_frame.h"
#include "binrwir/instruction.h"
#include "binrwir/operation_identifiers.h"
#include "binrwir/binfunc.h"
#include "binrwir/signature.h"
#include "contract/error.h"
#include "contract/status.h"
#include "contract/char_codec.h"
#include "contract/program_counter.h"
#include "contract/view.h"
#include "vmbin/value_access.h"
#include "vmbin/vthread_status.h"


/* 8 字节标量 + 余量；本批 8 条只处理 1/2/4/8 字节数值。 */
#define SCALAR_BODY_CAPACITY 8U

typedef enum scalar_class {
    SCALAR_CLASS_INVALID = 0,
    SCALAR_CLASS_SIGNED = 1,
    SCALAR_CLASS_UNSIGNED = 2,
    SCALAR_CLASS_FLOAT = 3,
    SCALAR_CLASS_BOOLEAN = 4
} scalar_class_t;

typedef struct scalar_type {
    scalar_class_t scalar_class;
    uint8_t width; /* 1/2/4/8 */
} scalar_type_t;

static int scalar_langtype_is_boolean(const scalar_type_t *type) {
    return type->scalar_class == SCALAR_CLASS_BOOLEAN;
}

/* langtype 文本 → 类型；只接受本批处理的 10 种数值，其余返回 INVALID。 */
static runtimec_status_t scalar_type_of_langtype(
    runtimec_string_view_t langtype, scalar_type_t *output_type) {
    static const struct {
        const char *text;
        scalar_class_t scalar_class;
        uint8_t width;
    } table[] = {
        {"int8", SCALAR_CLASS_SIGNED, 1U},
        {"int16", SCALAR_CLASS_SIGNED, 2U},
        {"int32", SCALAR_CLASS_SIGNED, 4U},
        {"int64", SCALAR_CLASS_SIGNED, 8U},
        {"uint8", SCALAR_CLASS_UNSIGNED, 1U},
        {"uint16", SCALAR_CLASS_UNSIGNED, 2U},
        {"uint32", SCALAR_CLASS_UNSIGNED, 4U},
        {"uint64", SCALAR_CLASS_UNSIGNED, 8U},
        {"float32", SCALAR_CLASS_FLOAT, 4U},
        {"float64", SCALAR_CLASS_FLOAT, 8U},
        {"bool", SCALAR_CLASS_BOOLEAN, 1U},
    };
    size_t ordinal;

    output_type->scalar_class = SCALAR_CLASS_INVALID;
    output_type->width = 0U;
    for (ordinal = 0U; ordinal < sizeof(table) / sizeof(table[0]); ++ordinal) {
        if (langtype.size == strlen(table[ordinal].text) &&
            memcmp(langtype.data, table[ordinal].text, langtype.size) == 0) {
            output_type->scalar_class = table[ordinal].scalar_class;
            output_type->width = table[ordinal].width;
            return RUNTIMEC_STATUS_SUCCESS;
        }
    }
    return RUNTIMEC_STATUS_INVALID;
}

/*
 * 取一个参数项的具体类型。
 *
 * 四种引用（字面量 / 局部 / 读参 / 写参）的分派统一交给
 * `vmbin_value_access_langtype`——同一个事实只有一个取法，本文件不重复实现
 * 「哪种 kind 该去哪里找类型」。`STATIC` 只有 key 文本、没有值类型，该函数按
 * 符号池给出空类型并以 UNSUPPORTED 拒绝；本批 8 条不涉及它。
 */
static runtimec_status_t scalar_type_of_argument(
    binrwir_binvthread_frame_t *frame, const binrwir_argument_t *argument,
    scalar_type_t *output_type, runtimec_error_t *error) {
    runtimec_string_view_t langtype = runtimec_string_view_make(NULL, 0U);
    runtimec_status_t status;

    output_type->scalar_class = SCALAR_CLASS_INVALID;
    output_type->width = 0U;
    status = vmbin_value_access_langtype(frame, argument, &langtype, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = scalar_type_of_langtype(langtype, output_type);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        runtimec_error_set(error, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_ERROR_DOMAIN_CONTRACT, 1,
                           runtimec_string_view_from_c_string(
                               "scalar handler: operand type is not numeric"),
                           RUNTIMEC_PROGRAM_COUNTER_NONE);
    }
    return status;
}

/* 按宽度与符号性把 8 字节读成「有符号值 + 无符号值 + 浮点值」三元组。 */
static void scalar_load(const uint8_t *bytes, const scalar_type_t *type,
                        int64_t *output_signed, uint64_t *output_unsigned,
                        double *output_float) {
    uint64_t raw = 0U;
    int64_t signed_value;

    switch (type->width) {
    case 1U: {
        uint8_t narrow = 0U;
        memcpy(&narrow, bytes, 1U);
        raw = narrow;
        signed_value = (int8_t)narrow;
        break;
    }
    case 2U: {
        uint16_t narrow = 0U;
        memcpy(&narrow, bytes, 2U);
        raw = narrow;
        signed_value = (int16_t)narrow;
        break;
    }
    case 4U: {
        uint32_t narrow = 0U;
        memcpy(&narrow, bytes, 4U);
        raw = narrow;
        signed_value = (int32_t)narrow;
        break;
    }
    default:
        memcpy(&raw, bytes, 8U);
        memcpy(&signed_value, bytes, 8U);
        break;
    }
    *output_unsigned = raw;
    *output_signed = signed_value;
    if (type->scalar_class == SCALAR_CLASS_FLOAT) {
        if (type->width == 4U) {
            float narrow = 0.0f;
            memcpy(&narrow, bytes, 4U);
            *output_float = (double)narrow;
        } else {
            double wide = 0.0;
            memcpy(&wide, bytes, 8U);
            *output_float = wide;
        }
    } else if (type->scalar_class == SCALAR_CLASS_UNSIGNED) {
        /* uint64 按无符号值转换，绝不先经 int64（matrix:26）。 */
        *output_float = (double)raw;
    } else {
        *output_float = (double)signed_value;
    }
}

/* 把结果按目标宽度写回；只做截断/回绕，不经浮点中转。 */
static void scalar_store(uint8_t *bytes, const scalar_type_t *type,
                         int64_t signed_value, uint64_t unsigned_value,
                         double float_value) {
    switch (type->width) {
    case 1U: {
        uint8_t narrow =
            type->scalar_class == SCALAR_CLASS_FLOAT
                ? (uint8_t)(int64_t)float_value
                : (type->scalar_class == SCALAR_CLASS_UNSIGNED
                       ? (uint8_t)unsigned_value
                       : (uint8_t)signed_value);
        memcpy(bytes, &narrow, 1U);
        return;
    }
    case 2U: {
        uint16_t narrow =
            type->scalar_class == SCALAR_CLASS_FLOAT
                ? (uint16_t)(int64_t)float_value
                : (type->scalar_class == SCALAR_CLASS_UNSIGNED
                       ? (uint16_t)unsigned_value
                       : (uint16_t)signed_value);
        memcpy(bytes, &narrow, 2U);
        return;
    }
    case 4U: {
        if (type->scalar_class == SCALAR_CLASS_FLOAT) {
            float narrow = (float)float_value;
            memcpy(bytes, &narrow, 4U);
            return;
        }
        uint32_t narrow = type->scalar_class == SCALAR_CLASS_UNSIGNED
                              ? (uint32_t)unsigned_value
                              : (uint32_t)signed_value;
        memcpy(bytes, &narrow, 4U);
        return;
    }
    default:
        if (type->scalar_class == SCALAR_CLASS_FLOAT) {
            memcpy(bytes, &float_value, 8U);
            return;
        }
        uint64_t wide = type->scalar_class == SCALAR_CLASS_UNSIGNED
                            ? unsigned_value
                            : (uint64_t)signed_value;
        memcpy(bytes, &wide, 8U);
        return;
    }
}

/*
 * 结果类型：取较宽操作数；任一侧浮点则 float64；有符号与无符号混用 INVALID；
 * 结果宽度与操作数顺序无关（matrix:77）。
 */
static runtimec_status_t scalar_result_type(const scalar_type_t *left,
                                           const scalar_type_t *right,
                                           scalar_type_t *output_type) {
    output_type->scalar_class = SCALAR_CLASS_INVALID;
    output_type->width = 0U;
    if (left->scalar_class == SCALAR_CLASS_FLOAT ||
        right->scalar_class == SCALAR_CLASS_FLOAT) {
        output_type->scalar_class = SCALAR_CLASS_FLOAT;
        output_type->width = 8U;
        return RUNTIMEC_STATUS_SUCCESS;
    }
    if (left->scalar_class != right->scalar_class) {
        /* 有符号与无符号不得混用（matrix:28）。 */
        return RUNTIMEC_STATUS_INVALID;
    }
    output_type->scalar_class = left->scalar_class;
    output_type->width = left->width > right->width ? left->width : right->width;
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t scalar_fail(runtimec_error_t *error, int64_t code,
                                     const char *message) {
    runtimec_error_set(error, RUNTIMEC_STATUS_INVALID,
                       RUNTIMEC_ERROR_DOMAIN_CONTRACT, code,
                       runtimec_string_view_from_c_string(message),
                       RUNTIMEC_PROGRAM_COUNTER_NONE);
    return RUNTIMEC_STATUS_INVALID;
}

/* 读一个参数项到栈缓冲；返回字节数与类型。 */
static runtimec_status_t scalar_read_operand(
    binrwir_binvthread_frame_t *frame, const binrwir_argument_t *argument,
    uint8_t *bytes, scalar_type_t *output_type, runtimec_error_t *error) {
    runtimec_bytes_buffer_t buffer = {bytes, 0U, SCALAR_BODY_CAPACITY};
    runtimec_status_t status =
        scalar_type_of_argument(frame, argument, output_type, error);

    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = vmbin_value_access_read_body(frame, argument, &buffer, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (buffer.size != output_type->width) {
        return scalar_fail(error, 2,
                           "scalar handler: operand width disagrees with its "
                           "declared langtype");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}



/*
 * 一元算子：读一个操作数，算完写回同一个形状。
 *
 * matrix:60 signed-neg-abs-sign — 按 C 与 Go：负号按操作数宽度回绕；
 * `abs` 对最小值回绕（int8 abs(-128) 得 -128）；`sign` 返回 -1/0/1。
 * matrix:31 unsigned-neg-abs-sign — 无符号：neg 回绕、abs 恒等、sign 按无符号值。
 * matrix:70 float-transcendental-result-kind — sqrt/exp/log 结果恒 float64，
 * 输入先按值转 double 再算；整数不经「整数→浮点→整数」往返。
 */
typedef enum scalar_unary_operation {
    SCALAR_UNARY_ABS = 0,
    SCALAR_UNARY_NEG = 1,
    SCALAR_UNARY_SIGN = 2,
    SCALAR_UNARY_SQRT = 3,
    SCALAR_UNARY_EXP = 4,
    SCALAR_UNARY_LOG = 5
} scalar_unary_operation_t;

/*
 * 整数族一元：abs / neg / sign。三个算子共用一次读、一次写。
 *
 * 语义按 matrix:60（有符号）与 matrix:31（无符号）：
 *   - 有符号：neg 按宽度回绕；abs 对最小值回绕（abs(INT64_MIN) 仍是它）；
 *     sign 返回 -1 / 0 / 1。
 *   - 无符号：neg 回绕；abs 恒等；sign 按无符号值给 0 或 1。
 * 这里只按「有符号 / 无符号」分叉，不按 langtype 分叉——宽度由 scalar_store 处理。
 */
static runtimec_status_t scalar_unary_integer(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    scalar_unary_operation_t operation, runtimec_error_t *error) {
    uint8_t operand_bytes[SCALAR_BODY_CAPACITY];
    uint8_t result_bytes[SCALAR_BODY_CAPACITY];
    scalar_type_t operand_type;
    int64_t signed_value = 0;
    uint64_t unsigned_value = 0U;
    double ignored_float = 0.0;
    int64_t result_signed = 0;
    uint64_t result_unsigned = 0U;
    runtimec_status_t status;

    if (instruction->read_arguments.count != 1U ||
        instruction->write_arguments.count != 1U) {
        return scalar_fail(error, 11,
                           "scalar handler: unary op expects one read and one "
                           "write");
    }
    status = scalar_read_operand(frame, &instruction->read_arguments.data[0],
                                 operand_bytes, &operand_type, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (operand_type.scalar_class == SCALAR_CLASS_FLOAT) {
        return scalar_fail(error, 16,
                           "scalar handler: abs/neg/sign require an integer "
                           "operand");
    }
    scalar_load(operand_bytes, &operand_type, &signed_value, &unsigned_value,
                &ignored_float);
    if (operand_type.scalar_class == SCALAR_CLASS_UNSIGNED) {
        switch (operation) {
        case SCALAR_UNARY_ABS:
            result_unsigned = unsigned_value;
            break;
        case SCALAR_UNARY_NEG:
            result_unsigned = (uint64_t)0U - unsigned_value;
            break;
        default:
            result_unsigned = unsigned_value > 0U ? 1U : 0U;
            break;
        }
    } else {
        switch (operation) {
        case SCALAR_UNARY_ABS:
            result_signed = signed_value < 0 ? -signed_value : signed_value;
            break;
        case SCALAR_UNARY_NEG:
            result_signed = -signed_value;
            break;
        default:
            result_signed = signed_value > 0 ? 1 : (signed_value < 0 ? -1 : 0);
            break;
        }
    }
    memset(result_bytes, 0, sizeof(result_bytes));
    scalar_store(result_bytes, &operand_type, result_signed, result_unsigned,
                 0.0);
    return vmbin_value_access_write_body(
        frame, &instruction->write_arguments.data[0],
        runtimec_bytes_view_make(result_bytes, operand_type.width), error);
}

/*
 * 超越函数：sqrt / exp / log。结果**恒 float64**，与操作数种类无关（matrix:70）：
 * 整数或 float32 操作数先按值转 double 再算，结果种类不跟随操作数。
 * 输入不是标量数值时由 scalar_read_operand 拒绝。
 */
static runtimec_status_t scalar_transcendental(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    scalar_unary_operation_t operation, runtimec_error_t *error) {
    uint8_t operand_bytes[SCALAR_BODY_CAPACITY];
    uint8_t result_bytes[SCALAR_BODY_CAPACITY];
    scalar_type_t operand_type;
    scalar_type_t result_type;
    int64_t signed_value = 0;
    uint64_t unsigned_value = 0U;
    double float_value = 0.0;
    double result_float;
    runtimec_status_t status;

    if (instruction->read_arguments.count != 1U ||
        instruction->write_arguments.count != 1U) {
        return scalar_fail(error, 17,
                           "scalar handler: transcendental op expects one read "
                           "and one write");
    }
    status = scalar_read_operand(frame, &instruction->read_arguments.data[0],
                                 operand_bytes, &operand_type, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    scalar_load(operand_bytes, &operand_type, &signed_value, &unsigned_value,
                &float_value);
    switch (operation) {
    case SCALAR_UNARY_SQRT:
        result_float = sqrt(float_value);
        break;
    case SCALAR_UNARY_EXP:
        result_float = exp(float_value);
        break;
    default:
        result_float = log(float_value);
        break;
    }
    result_type.scalar_class = SCALAR_CLASS_FLOAT;
    result_type.width = 8U;
    memset(result_bytes, 0, sizeof(result_bytes));
    scalar_store(result_bytes, &result_type, 0, 0U, result_float);
    return vmbin_value_access_write_body(
        frame, &instruction->write_arguments.data[0],
        runtimec_bytes_view_make(result_bytes, result_type.width), error);
}

#define SCALAR_INTEGER_UNARY_HANDLER(name, operation)                         \
    static runtimec_status_t name(                                            \
        binrwir_binvthread_frame_t *frame,                                     \
        const binrwir_instruction_view_t *instruction,                        \
        binrwir_execution_exit_t *output_exit) {                              \
        runtimec_error_t error;                                               \
        (void)output_exit;                                                    \
        runtimec_error_reset(&error);                                         \
        return scalar_unary_integer(frame, instruction, operation, &error);   \
    }


/*
 * `pow(x, y)` —— 二元超越函数，结果**恒 float64**（matrix:70，与一元那条同源）。
 * 两个操作数先按值转 double 再算；`uint64` 走无符号值本身，不经 int64（matrix:26）。
 *
 * **为什么这里要有基础条目**：本条**不是**生产入口。生产走
 * `execution_window → binrwir_binfunc_specialize`，只要特化表里有对应行，
 * 基础 opid 就被无条件改写成 `0x8xxx`，执行的是 `scalar_generated_power_*`。
 *
 * 补它的理由是**测试矩阵完整**：`handler_behavior` 按基础 opid 组织标量语义，
 * 标量族 37 个算子只有 `pow` 缺条目。缺这一条会让「矩阵全覆盖」变成假话，
 * 而不是让生产少一个能力。
 */
static runtimec_status_t scalar_power(binrwir_binvthread_frame_t *frame,
                                      const binrwir_instruction_view_t *instruction,
                                      runtimec_error_t *error) {
    uint8_t left_bytes[SCALAR_BODY_CAPACITY];
    uint8_t right_bytes[SCALAR_BODY_CAPACITY];
    uint8_t result_bytes[SCALAR_BODY_CAPACITY];
    scalar_type_t left_type;
    scalar_type_t right_type;
    scalar_type_t result_type;
    int64_t ignored_signed = 0;
    uint64_t ignored_unsigned = 0U;
    double left_float = 0.0;
    double right_float = 0.0;
    double result_float;
    runtimec_status_t status;

    if (instruction->read_arguments.count != 2U ||
        instruction->write_arguments.count != 1U) {
        return scalar_fail(error, 36,
                           "scalar handler: pow expects two reads and one write");
    }
    status = scalar_read_operand(frame, &instruction->read_arguments.data[0],
                                 left_bytes, &left_type, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = scalar_read_operand(frame, &instruction->read_arguments.data[1],
                                 right_bytes, &right_type, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    scalar_load(left_bytes, &left_type, &ignored_signed, &ignored_unsigned,
                &left_float);
    scalar_load(right_bytes, &right_type, &ignored_signed, &ignored_unsigned,
                &right_float);
    result_float = pow(left_float, right_float);
    result_type.scalar_class = SCALAR_CLASS_FLOAT;
    result_type.width = 8U;
    memset(result_bytes, 0, sizeof(result_bytes));
    scalar_store(result_bytes, &result_type, 0, 0U, result_float);
    return vmbin_value_access_write_body(
        frame, &instruction->write_arguments.data[0],
        runtimec_bytes_view_make(result_bytes, result_type.width), error);
}

#define SCALAR_TRANSCENDENTAL_HANDLER(name, operation)                        \
    static runtimec_status_t name(                                            \
        binrwir_binvthread_frame_t *frame,                                     \
        const binrwir_instruction_view_t *instruction,                        \
        binrwir_execution_exit_t *output_exit) {                              \
        runtimec_error_t error;                                               \
        (void)output_exit;                                                    \
        runtimec_error_reset(&error);                                         \
        return scalar_transcendental(frame, instruction, operation, &error);  \
    }

SCALAR_INTEGER_UNARY_HANDLER(scalar_handle_abs, SCALAR_UNARY_ABS)
SCALAR_INTEGER_UNARY_HANDLER(scalar_handle_neg, SCALAR_UNARY_NEG)
SCALAR_INTEGER_UNARY_HANDLER(scalar_handle_sign, SCALAR_UNARY_SIGN)
SCALAR_TRANSCENDENTAL_HANDLER(scalar_handle_sqrt, SCALAR_UNARY_SQRT)
SCALAR_TRANSCENDENTAL_HANDLER(scalar_handle_exp, SCALAR_UNARY_EXP)
SCALAR_TRANSCENDENTAL_HANDLER(scalar_handle_log, SCALAR_UNARY_LOG)

/*
 * max/min — matrix:69 floating-min-max-nan：NaN 让位于数，两个 NaN 得 NaN，
 * 结果与操作数顺序无关（按 C 与 Rust）。整数路径先比整数，不比浮点。
 */
static runtimec_status_t scalar_min_max(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, int want_maximum,
    runtimec_error_t *error) {
    uint8_t left_bytes[SCALAR_BODY_CAPACITY];
    uint8_t right_bytes[SCALAR_BODY_CAPACITY];
    uint8_t result_bytes[SCALAR_BODY_CAPACITY];
    scalar_type_t left_type;
    scalar_type_t right_type;
    scalar_type_t result_type;
    int64_t left_signed = 0;
    int64_t right_signed = 0;
    uint64_t left_unsigned = 0U;
    uint64_t right_unsigned = 0U;
    double left_float = 0.0;
    double right_float = 0.0;
    int64_t result_signed = 0;
    uint64_t result_unsigned = 0U;
    double result_float = 0.0;
    runtimec_status_t status;

    if (instruction->read_arguments.count != 2U ||
        instruction->write_arguments.count != 1U) {
        return scalar_fail(error, 12,
                           "scalar handler: min/max expects two reads and one "
                           "write");
    }
    status = scalar_read_operand(frame, &instruction->read_arguments.data[0],
                                 left_bytes, &left_type, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = scalar_read_operand(frame, &instruction->read_arguments.data[1],
                                 right_bytes, &right_type, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = scalar_result_type(&left_type, &right_type, &result_type);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return scalar_fail(
            error, 13,
            "scalar handler: signed and unsigned operands cannot mix");
    }
    scalar_load(left_bytes, &left_type, &left_signed, &left_unsigned,
                &left_float);
    scalar_load(right_bytes, &right_type, &right_signed, &right_unsigned,
                &right_float);
    if (result_type.scalar_class == SCALAR_CLASS_FLOAT) {
        /* NaN 让位于数：任一为 NaN 时取另一个；两个都 NaN 得 NaN（matrix:69）。 */
        const int left_nan = left_float != left_float;
        const int right_nan = right_float != right_float;

        if (left_nan && right_nan) {
            result_float = left_float;
        } else if (left_nan) {
            result_float = right_float;
        } else if (right_nan) {
            result_float = left_float;
        } else {
            const int left_is_larger = left_float > right_float;

            result_float = (want_maximum == left_is_larger) ? left_float
                                                            : right_float;
        }
    } else if (result_type.scalar_class == SCALAR_CLASS_UNSIGNED) {
        const int left_is_larger = left_unsigned > right_unsigned;

        result_unsigned =
            (want_maximum == left_is_larger) ? left_unsigned : right_unsigned;
    } else {
        const int left_is_larger = left_signed > right_signed;

        result_signed =
            (want_maximum == left_is_larger) ? left_signed : right_signed;
    }
    memset(result_bytes, 0, sizeof(result_bytes));
    scalar_store(result_bytes, &result_type, result_signed, result_unsigned,
                 result_float);
    return vmbin_value_access_write_body(
        frame, &instruction->write_arguments.data[0],
        runtimec_bytes_view_make(result_bytes, result_type.width), error);
}

#define SCALAR_MIN_MAX_HANDLER(name, want_maximum)                            \
    static runtimec_status_t name(                                            \
        binrwir_binvthread_frame_t *frame,                                     \
        const binrwir_instruction_view_t *instruction,                        \
        binrwir_execution_exit_t *output_exit) {                              \
        runtimec_error_t error;                                               \
        (void)output_exit;                                                    \
        runtimec_error_reset(&error);                                         \
        return scalar_min_max(frame, instruction, want_maximum, &error);      \
    }

SCALAR_MIN_MAX_HANDLER(scalar_handle_max, 1)
SCALAR_MIN_MAX_HANDLER(scalar_handle_min, 0)

/*
 * and / or / not — matrix:78 logical-operators-truthy：接受数值与布尔操作数，
 * 非零数值与 true 等价；结果**恒 bool**。
 *
 * 三条都不做短路：binrwir 的实参在执行前已经求值，本层拿不到「未求值的第二
 * 操作数」——短路在这里无处安放，不是没选。两个都已读出后，`left && right`
 * 与短路版本结果相同。
 */
static int scalar_boundary_is_truthy(const uint8_t *bytes,
                                     const scalar_type_t *type) {
    int64_t signed_value = 0;
    uint64_t unsigned_value = 0U;
    double float_value = 0.0;

    if (type->scalar_class == SCALAR_CLASS_FLOAT) {
        scalar_load(bytes, type, &signed_value, &unsigned_value, &float_value);
        /* 非零即真；NaN 也是非零（!= 0.0 为真）。 */
        return float_value != 0.0;
    }
    scalar_load(bytes, type, &signed_value, &unsigned_value, &float_value);
    return type->scalar_class == SCALAR_CLASS_UNSIGNED ? unsigned_value != 0U
                                                       : signed_value != 0;
}

static runtimec_status_t scalar_logical(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, int is_and, int is_not,
    runtimec_error_t *error) {
    uint8_t bytes[SCALAR_BODY_CAPACITY];
    uint8_t result_bytes[1];
    scalar_type_t type;
    int truthy;
    runtimec_status_t status;
    const size_t expected_reads = is_not ? 1U : 2U;

    if (instruction->read_arguments.count != expected_reads ||
        instruction->write_arguments.count != 1U) {
        return scalar_fail(error, 18,
                           "scalar handler: logical op has the wrong arity");
    }
    status = scalar_read_operand(frame, &instruction->read_arguments.data[0],
                                 bytes, &type, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    truthy = scalar_boundary_is_truthy(bytes, &type);
    if (is_not) {
        truthy = !truthy;
    } else {
        status = scalar_read_operand(frame, &instruction->read_arguments.data[1],
                                     bytes, &type, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
        truthy = is_and ? (truthy && scalar_boundary_is_truthy(bytes, &type))
                        : (truthy || scalar_boundary_is_truthy(bytes, &type));
    }
    /* bool 的 body 是 1 字节，取值 0 或 1。 */
    result_bytes[0] = truthy ? 1U : 0U;
    return vmbin_value_access_write_body(
        frame, &instruction->write_arguments.data[0],
        runtimec_bytes_view_make(result_bytes, 1U), error);
}

#define SCALAR_LOGICAL_HANDLER(name, is_and, is_not)                          \
    static runtimec_status_t name(                                            \
        binrwir_binvthread_frame_t *frame,                                     \
        const binrwir_instruction_view_t *instruction,                        \
        binrwir_execution_exit_t *output_exit) {                              \
        runtimec_error_t error;                                               \
        (void)output_exit;                                                    \
        runtimec_error_reset(&error);                                         \
        return scalar_logical(frame, instruction, is_and, is_not, &error);    \
    }

SCALAR_LOGICAL_HANDLER(scalar_handle_and, 1, 0)
SCALAR_LOGICAL_HANDLER(scalar_handle_or, 0, 0)
SCALAR_LOGICAL_HANDLER(scalar_handle_not, 0, 1)

/*
 * cast — matrix:63 cast-target-kind：一次转换、只命名目标种类，不做链式转换；
 * 目标种类必须在十四个标量内，容器源一律 INVALID。
 *
 * 逐族规则（matrix 各条）：
 *   :21 floating-to-integer-truncation — 向零截断；越界与 ±Inf **饱和**到目标宽度；
 *       NaN 是错误（TypeError），不产出数值。
 *   :26 uint64-to-float-promotion      — 按无符号值转换，不经 int64。
 *   int-to-float32                     — 直接舍入到 float32 一次，不先经 float64。
 *   :35 bool-cast-strict               — 目标 bool 只接受 bool 操作数。
 *   :62 character-conversion-result    — 字符转换产出字符串值；本批不涉及（需 codec）。
 *
 * 目标类型从**写参数项**的 langtype 取——那是 Program 里唯一声明结果种类的地方。
 */
static int64_t scalar_saturate_to_width(int64_t value, scalar_type_t target) {
    int64_t low;
    int64_t high;

    switch (target.width) {
    case 1U:
        low = target.scalar_class == SCALAR_CLASS_UNSIGNED ? 0 : INT8_MIN;
        high = target.scalar_class == SCALAR_CLASS_UNSIGNED ? UINT8_MAX
                                                           : INT8_MAX;
        break;
    case 2U:
        low = target.scalar_class == SCALAR_CLASS_UNSIGNED ? 0 : INT16_MIN;
        high = target.scalar_class == SCALAR_CLASS_UNSIGNED ? UINT16_MAX
                                                           : INT16_MAX;
        break;
    case 4U:
        low = target.scalar_class == SCALAR_CLASS_UNSIGNED ? 0 : INT32_MIN;
        high = target.scalar_class == SCALAR_CLASS_UNSIGNED ? UINT32_MAX
                                                           : INT32_MAX;
        break;
    default:
        return value;
    }
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

static runtimec_status_t scalar_cast(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, runtimec_error_t *error) {
    uint8_t source_bytes[SCALAR_BODY_CAPACITY];
    uint8_t result_bytes[SCALAR_BODY_CAPACITY];
    scalar_type_t source_type;
    scalar_type_t target_type;
    runtimec_string_view_t target_langtype =
        runtimec_string_view_make(NULL, 0U);
    int64_t source_signed = 0;
    uint64_t source_unsigned = 0U;
    double source_float = 0.0;
    int64_t result_signed = 0;
    uint64_t result_unsigned = 0U;
    double result_float = 0.0;
    runtimec_status_t status;

    if (instruction->read_arguments.count != 1U ||
        instruction->write_arguments.count != 1U) {
        return scalar_fail(error, 19,
                           "scalar handler: cast expects one read and one "
                           "write");
    }
    status = scalar_read_operand(frame, &instruction->read_arguments.data[0],
                                 source_bytes, &source_type, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = vmbin_value_access_langtype(frame,
                                         &instruction->write_arguments.data[0],
                                         &target_langtype, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = scalar_type_of_langtype(target_langtype, &target_type);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return scalar_fail(
            error, 20,
            "scalar handler: cast target kind is outside the scalar set");
    }
    scalar_load(source_bytes, &source_type, &source_signed, &source_unsigned,
                &source_float);

    if (target_type.scalar_class == SCALAR_CLASS_FLOAT) {
        if (target_type.width == 4U) {
            /* 直接从源值舍入到 float32 一次（不先经 float64 中转）。 */
            float narrow;
            if (source_type.scalar_class == SCALAR_CLASS_FLOAT) {
                narrow = (float)source_float;
            } else if (source_type.scalar_class == SCALAR_CLASS_UNSIGNED) {
                narrow = (float)source_unsigned;
            } else {
                narrow = (float)source_signed;
            }
            result_float = (double)narrow;
        } else {
            result_float = source_type.scalar_class == SCALAR_CLASS_UNSIGNED
                               ? (double)source_unsigned
                               : (double)source_signed;
            if (source_type.scalar_class == SCALAR_CLASS_FLOAT) {
                result_float = source_float;
            }
        }
    } else if (target_type.scalar_class == SCALAR_CLASS_UNSIGNED) {
        if (source_type.scalar_class == SCALAR_CLASS_FLOAT) {
            if (source_float != source_float) {
                /* NaN 不是可转换的数值（matrix:21）。 */
                return scalar_fail(error, 21,
                                   "scalar handler: cast of NaN to an integer");
            }
            /* 向零截断后饱和；超宽用 INT64 上下界先落一次。 */
            if (source_float >= 18446744073709551616.0) {
                result_unsigned = UINT64_MAX;
            } else if (source_float <= -1.0) {
                result_unsigned = 0U;
            } else {
                result_unsigned = (uint64_t)source_float;
            }
        } else if (source_type.scalar_class == SCALAR_CLASS_SIGNED) {
            result_unsigned = source_signed < 0 ? 0U : (uint64_t)source_signed;
        } else {
            result_unsigned = source_unsigned;
        }
        result_unsigned = (uint64_t)scalar_saturate_to_width(
            (int64_t)(result_unsigned > (uint64_t)INT64_MAX ? INT64_MAX
                                                            : result_unsigned),
            target_type);
        if (target_type.width < 8U && result_unsigned != 0U) {
            const uint64_t limit = (UINT64_C(1) << (target_type.width * 8U));

            if (result_unsigned >= limit) {
                result_unsigned = limit - 1U;
            }
        }
    } else {
        if (source_type.scalar_class == SCALAR_CLASS_FLOAT) {
            if (source_float != source_float) {
                return scalar_fail(error, 22,
                                   "scalar handler: cast of NaN to an integer");
            }
            if (source_float >= 9223372036854775808.0) {
                result_signed = INT64_MAX;
            } else if (source_float <= -9223372036854775808.0) {
                result_signed = INT64_MIN;
            } else {
                result_signed = (int64_t)source_float;
            }
        } else if (source_type.scalar_class == SCALAR_CLASS_UNSIGNED) {
            result_signed = source_unsigned > (uint64_t)INT64_MAX
                                ? INT64_MAX
                                : (int64_t)source_unsigned;
        } else {
            result_signed = source_signed;
        }
        result_signed = scalar_saturate_to_width(result_signed, target_type);
    }
    memset(result_bytes, 0, sizeof(result_bytes));
    scalar_store(result_bytes, &target_type, result_signed, result_unsigned,
                 result_float);
    return vmbin_value_access_write_body(
        frame, &instruction->write_arguments.data[0],
        runtimec_bytes_view_make(result_bytes, target_type.width), error);
}

/*
 * 纯转发的 handler：把 frame/instruction/exit 原样交给实现体，自己不做任何事。
 *
 * 为什么要宏而不是手写三行：这条包装的**全部内容**就是「准备 error 槽、说明
 * output_exit 不在这里用、调用、返回」。手写时那四件事会各占一行，超过 3 行上限；
 * 而为过上限把它们用逗号运算符挤成一行，是让代码以奇怪的方式通过检查——读的人
 * 要在同一行里认出两个不相干的动作，而门禁想要的「一个函数一件事」反而更远了。
 *
 * 宏里藏着同样的四件事，但**函数体只剩一行调用**，形状与字符族的
 * `VMBIN_HANDLER_*` 一致：参数准备归宏，逻辑函数只做一件事。
 *
 * `output_exit` 在这里确实用不到——转发目标只写 `error`，结构化退出由调用方
 * 根据返回值处置。显式 `(void)` 是为了让这条「用不到」变成写下来的事实。
 */
#define SCALAR_FORWARDING_HANDLER(name, implementation)                       \
    static runtimec_status_t name(                                            \
        binrwir_binvthread_frame_t *frame,                                     \
        const binrwir_instruction_view_t *instruction,                        \
        binrwir_execution_exit_t *output_exit) {                              \
        runtimec_error_t error;                                               \
        (void)output_exit;                                                    \
        runtimec_error_reset(&error);                                         \
        return implementation(frame, instruction, &error);                    \
    }

/* 带一个额外常量参数的同类转发（`scalar_pointer_equality` 的 invert 位）。 */
#define SCALAR_FORWARDING_HANDLER_WITH_ARGUMENT(name, implementation, argument) \
    static runtimec_status_t name(                                            \
        binrwir_binvthread_frame_t *frame,                                     \
        const binrwir_instruction_view_t *instruction,                        \
        binrwir_execution_exit_t *output_exit) {                              \
        runtimec_error_t error;                                               \
        (void)output_exit;                                                    \
        runtimec_error_reset(&error);                                         \
        return implementation(frame, instruction, argument, &error);          \
    }

SCALAR_FORWARDING_HANDLER(scalar_handle_cast, scalar_cast)
SCALAR_FORWARDING_HANDLER(scalar_handle_pow, scalar_power)

/*
 * shl/shr — matrix:71 shift-count-masking：计数按「宽度减一」掩码，64 位就是低 6 位；
 * 有符号右移是算术移位、无符号是逻辑移位；结果类型等于操作数类型。
 * catalog 掩码只给 int64 / uint64，其余一律 INVALID。
 */
static runtimec_status_t scalar_shift(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, int shift_left,
    runtimec_error_t *error) {
    uint8_t value_bytes[SCALAR_BODY_CAPACITY];
    uint8_t count_bytes[SCALAR_BODY_CAPACITY];
    uint8_t result_bytes[SCALAR_BODY_CAPACITY];
    scalar_type_t value_type;
    scalar_type_t count_type;
    int64_t value_signed = 0;
    int64_t count_signed = 0;
    uint64_t value_unsigned = 0U;
    uint64_t count_unsigned = 0U;
    double ignored_value = 0.0;
    double ignored_count = 0.0;
    int64_t result_signed = 0;
    uint64_t result_unsigned = 0U;
    unsigned count;
    runtimec_status_t status;

    if (instruction->read_arguments.count != 2U ||
        instruction->write_arguments.count != 1U) {
        return scalar_fail(error, 14,
                           "scalar handler: shift expects two reads and one "
                           "write");
    }
    status = scalar_read_operand(frame, &instruction->read_arguments.data[0],
                                 value_bytes, &value_type, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = scalar_read_operand(frame, &instruction->read_arguments.data[1],
                                 count_bytes, &count_type, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (value_type.width != 8U || count_type.width != 8U ||
        value_type.scalar_class == SCALAR_CLASS_FLOAT ||
        count_type.scalar_class == SCALAR_CLASS_FLOAT) {
        return scalar_fail(error, 15,
                           "scalar handler: shift operands must be 64-bit "
                           "integers");
    }
    scalar_load(value_bytes, &value_type, &value_signed, &value_unsigned,
                &ignored_value);
    scalar_load(count_bytes, &count_type, &count_signed, &count_unsigned,
                &ignored_count);
    /* 计数取低 6 位：与 C/Go/Rust/V8 一致，移位不越出宽度（matrix:71）。 */
    count = (unsigned)(count_unsigned & 63U);
    if (value_type.scalar_class == SCALAR_CLASS_UNSIGNED) {
        result_unsigned = shift_left ? (value_unsigned << count)
                                     : (value_unsigned >> count);
    } else {
        /* 有符号右移是算术移位；左移按 64 位回绕。 */
        result_signed = shift_left
                            ? (int64_t)((uint64_t)value_signed << count)
                            : (value_signed >> count);
    }
    memset(result_bytes, 0, sizeof(result_bytes));
    scalar_store(result_bytes, &value_type, result_signed, result_unsigned,
                 0.0);
    return vmbin_value_access_write_body(
        frame, &instruction->write_arguments.data[0],
        runtimec_bytes_view_make(result_bytes, value_type.width), error);
}

#define SCALAR_SHIFT_HANDLER(name, shift_left)                                \
    static runtimec_status_t name(                                            \
        binrwir_binvthread_frame_t *frame,                                     \
        const binrwir_instruction_view_t *instruction,                        \
        binrwir_execution_exit_t *output_exit) {                              \
        runtimec_error_t error;                                               \
        (void)output_exit;                                                    \
        runtimec_error_reset(&error);                                         \
        return scalar_shift(frame, instruction, shift_left, &error);          \
    }

SCALAR_SHIFT_HANDLER(scalar_handle_shl, 1)
SCALAR_SHIFT_HANDLER(scalar_handle_shr, 0)

/*
 * 字符算子：concat（拼接）与 streq（相等）。两者都要**跨编码**工作——
 * char/ascii 的 `a` 与 char/utf8 的 `a` 相等（matrix:65），拼接结果恒 char/utf8
 * （matrix:64）。
 *
 * 唯一转换点是 contract/char_codec：本文件只把操作数送去解码、再比对或编码，
 * 不内联任何编码逻辑。
 */
#define SCALAR_CHAR_TEXT_CAPACITY 1024U

static int scalar_langtype_is(runtimec_string_view_t langtype, const char *text) {
    const size_t size = strlen(text);

    return langtype.size == size && langtype.data != NULL &&
           memcmp(langtype.data, text, size) == 0;
}

/* 把任意字符种类的 body 解码成 UTF-8 文本。 */
static bool scalar_char_to_utf8(runtimec_string_view_t langtype,
                                const uint8_t *body, size_t size, char *text,
                                size_t capacity, size_t *output_size) {
    *output_size = 0U;
    if (scalar_langtype_is(langtype, "char/utf8")) {
        if (size >= capacity) {
            return false;
        }
        if (size != 0U) {
            memcpy(text, body, size);
        }
        *output_size = size;
        return true;
    }
    if (scalar_langtype_is(langtype, "char/utf32")) {
        return runtimec_char_utf32_to_utf8(body, size, text, capacity,
                                           output_size);
    }
    if (scalar_langtype_is(langtype, "char/ascii")) {
        return runtimec_char_ascii_to_utf8(body, size, text, capacity,
                                           output_size);
    }
    return false;
}

/* 读一个字符操作数并解码成 UTF-8。 */
static runtimec_status_t scalar_char_operand(
    binrwir_binvthread_frame_t *frame, const binrwir_argument_t *argument,
    char *text, size_t capacity, size_t *output_size, runtimec_error_t *error) {
    uint8_t bytes[SCALAR_CHAR_TEXT_CAPACITY];
    runtimec_bytes_buffer_t buffer = {bytes, 0U, sizeof(bytes)};
    runtimec_string_view_t langtype = runtimec_string_view_make(NULL, 0U);
    runtimec_status_t status;

    status = vmbin_value_access_langtype(frame, argument, &langtype, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = vmbin_value_access_read_body(frame, argument, &buffer, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (!scalar_char_to_utf8(langtype, bytes, buffer.size, text, capacity,
                             output_size)) {
        return scalar_fail(
            error, 23,
            "scalar handler: character operand is not a decodable character");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/* concat：两段 UTF-8 依次写出，结果恒 char/utf8（matrix:64）。 */
static runtimec_status_t scalar_concat(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, runtimec_error_t *error) {
    char left[SCALAR_CHAR_TEXT_CAPACITY];
    char right[SCALAR_CHAR_TEXT_CAPACITY];
    char joined[SCALAR_CHAR_TEXT_CAPACITY];
    size_t left_size = 0U;
    size_t right_size = 0U;
    runtimec_status_t status;

    if (instruction->read_arguments.count != 2U ||
        instruction->write_arguments.count != 1U) {
        return scalar_fail(error, 24,
                           "scalar handler: concat expects two reads and one "
                           "write");
    }
    status = scalar_char_operand(frame, &instruction->read_arguments.data[0],
                                 left, sizeof(left), &left_size, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = scalar_char_operand(frame, &instruction->read_arguments.data[1],
                                 right, sizeof(right), &right_size, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (left_size + right_size >= sizeof(joined)) {
        return scalar_fail(error, 25,
                           "scalar handler: concatenated text is too long");
    }
    if (left_size != 0U) {
        memcpy(joined, left, left_size);
    }
    if (right_size != 0U) {
        memcpy(joined + left_size, right, right_size);
    }
    return vmbin_value_access_write_body(
        frame, &instruction->write_arguments.data[0],
        runtimec_bytes_view_make(joined, left_size + right_size), error);
}

/* streq：比较解码后的码点序列，种类可以不同（matrix:65）。 */
static runtimec_status_t scalar_streq(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, int invert,
    runtimec_error_t *error) {
    char left[SCALAR_CHAR_TEXT_CAPACITY];
    char right[SCALAR_CHAR_TEXT_CAPACITY];
    size_t left_size = 0U;
    size_t right_size = 0U;
    uint8_t result;
    runtimec_status_t status;

    if (instruction->read_arguments.count != 2U ||
        instruction->write_arguments.count != 1U) {
        return scalar_fail(error, 26,
                           "scalar handler: streq expects two reads and one "
                           "write");
    }
    status = scalar_char_operand(frame, &instruction->read_arguments.data[0],
                                 left, sizeof(left), &left_size, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = scalar_char_operand(frame, &instruction->read_arguments.data[1],
                                 right, sizeof(right), &right_size, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    /* 两边都已是 UTF-8 码点序列，逐字节比较即逐码点比较。 */
    result = (left_size == right_size &&
              (left_size == 0U || memcmp(left, right, left_size) == 0))
                 ? 1U
                 : 0U;
    if (invert) {
        result = result != 0U ? 0U : 1U;
    }
    return vmbin_value_access_write_body(
        frame, &instruction->write_arguments.data[0],
        runtimec_bytes_view_make(&result, 1U), error);
}

#define SCALAR_CHAR_HANDLER(name, call)                                       \
    static runtimec_status_t name(                                            \
        binrwir_binvthread_frame_t *frame,                                     \
        const binrwir_instruction_view_t *instruction,                        \
        binrwir_execution_exit_t *output_exit) {                              \
        runtimec_error_t error;                                               \
        (void)output_exit;                                                    \
        runtimec_error_reset(&error);                                         \
        return call;                                                          \
    }

SCALAR_CHAR_HANDLER(scalar_handle_concat,
                    scalar_concat(frame, instruction, &error))
SCALAR_CHAR_HANDLER(scalar_handle_streq,
                    scalar_streq(frame, instruction, 0, &error))
SCALAR_CHAR_HANDLER(scalar_handle_streq_not,
                    scalar_streq(frame, instruction, 1, &error))

/*
 * debugger（0x0023）— rwir_catalog.def:317，behavior-decision
 * `debugger-pauses-execution`。
 *
 * spec 定义 `debugger()` ≡ `vthread·setstatus("paused")`：暂停能力就是写
 * `/vthread/{vid}/‥status`。所以本 handler：
 *
 *   1. 写 `‥status = paused`（唯一写入点 runtimec_vthread_status_write）；
 *   2. 产出 `BINRWIR_EXECUTION_EXIT_YIELD`——`PAUSED` 既非完成也非错误，
 *      `YIELD` 是 git 枚举里唯一表达「主动让出、可恢复」的出口；
 *   3. 不推进游标（由解释器按出口类型决定）。
 *
 * 目录已有说明：本 handler 不产出值，catalog 的 ResultRule 是 NONE、读写掩码
 * 都是 0，所以不接受任何参数。
 */
/*
 * eq / neq / lt / le / gt / ge（0x0015-0x001A）—— 六条数值比较共用同一个核心，
 * 只差方向与是否取反。结果恒 bool（matrix:23 mixed-numeric-equality）。
 *
 * 类型统一按 matrix:77：结果宽度取较宽操作数；任一侧浮点则按 float64 比较；
 * 有符号与无符号混用一律 INVALID（matrix:28）——**不猜一个提升**。
 *
 * 与 cast 的关键差别：比较**不损失精度**。两个整数比较走整数分支；只有一侧
 * 确实是浮点时才转 double（那时原生就只有一个表示）。
 */
typedef enum scalar_comparison {
    SCALAR_COMPARISON_EQUAL = 0,
    SCALAR_COMPARISON_LESS = 1,
    SCALAR_COMPARISON_LESS_EQUAL = 2,
    SCALAR_COMPARISON_GREATER = 3,
    SCALAR_COMPARISON_GREATER_EQUAL = 4
} scalar_comparison_t;

static runtimec_status_t scalar_compare(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    scalar_comparison_t comparison, int invert, runtimec_error_t *error) {
    uint8_t left_bytes[SCALAR_BODY_CAPACITY];
    uint8_t right_bytes[SCALAR_BODY_CAPACITY];
    scalar_type_t left_type;
    scalar_type_t right_type;
    scalar_type_t result_type;
    int64_t left_signed = 0;
    int64_t right_signed = 0;
    uint64_t left_unsigned = 0U;
    uint64_t right_unsigned = 0U;
    double left_float = 0.0;
    double right_float = 0.0;
    int matched;
    uint8_t result;
    runtimec_status_t status;

    if (instruction->read_arguments.count != 2U ||
        instruction->write_arguments.count != 1U) {
        return scalar_fail(error, 28,
                           "scalar handler: comparison expects two reads and "
                           "one write");
    }
    status = scalar_read_operand(frame, &instruction->read_arguments.data[0],
                                 left_bytes, &left_type, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = scalar_read_operand(frame, &instruction->read_arguments.data[1],
                                 right_bytes, &right_type, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    scalar_load(left_bytes, &left_type, &left_signed, &left_unsigned,
                &left_float);
    scalar_load(right_bytes, &right_type, &right_signed, &right_unsigned,
                &right_float);
    /*
     * 混合符号性（一侧有符号、一侧无符号）：**按精确数学值比较，不拒绝**
     * （spec `05-runtime语义/11-指令派发与特化.kv`；matrix:28 的
     * 「comparisons use exact mathematical values」，例子是 `-1` 小于
     * `UINT64_MAX`）。
     *
     * 这与算术**分口径**：`u64 + i64` 与 `u64 & i64` 仍然拒绝（那是
     * `scalar_result_type` 的判据）。所以这一支**不能**塞进
     * `scalar_result_type`——那会把算术的规则一起放宽。
     *
     * 判据只认**符号性不同**，不认宽度不同：`int32` vs `int64` 是两个有符号数，
     * 走下面的常规分支。
     */
    {
        const int left_is_integer = left_type.scalar_class == SCALAR_CLASS_SIGNED ||
                                    left_type.scalar_class == SCALAR_CLASS_UNSIGNED;
        const int right_is_integer = right_type.scalar_class == SCALAR_CLASS_SIGNED ||
                                     right_type.scalar_class == SCALAR_CLASS_UNSIGNED;
        const int crosses_sign = left_is_integer && right_is_integer &&
                                 left_type.scalar_class != right_type.scalar_class;

        if (crosses_sign) {
            int order;

            if (left_type.scalar_class == SCALAR_CLASS_SIGNED) {
                order = runtimec_scalar_compare_signed_unsigned(
                    left_signed, right_unsigned);
            } else {
                order = runtimec_scalar_compare_unsigned_signed(
                    left_unsigned, right_signed);
            }
            switch (comparison) {
            case SCALAR_COMPARISON_EQUAL:
                matched = order == 0;
                break;
            case SCALAR_COMPARISON_LESS:
                matched = order < 0;
                break;
            case SCALAR_COMPARISON_LESS_EQUAL:
                matched = order <= 0;
                break;
            case SCALAR_COMPARISON_GREATER:
                matched = order > 0;
                break;
            default:
                matched = order >= 0;
                break;
            }
            result = (uint8_t)((matched != 0) != (invert != 0) ? 1U : 0U);
            return vmbin_value_access_write_body(
                frame, &instruction->write_arguments.data[0],
                runtimec_bytes_view_make(&result, 1U), error);
        }
    }
    status = scalar_result_type(&left_type, &right_type, &result_type);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return scalar_fail(error, 29,
                           "scalar handler: comparison rejected an operand pair");
    }
    if (result_type.scalar_class == SCALAR_CLASS_FLOAT) {
        switch (comparison) {
        case SCALAR_COMPARISON_EQUAL:
            matched = left_float == right_float;
            break;
        case SCALAR_COMPARISON_LESS:
            matched = left_float < right_float;
            break;
        case SCALAR_COMPARISON_LESS_EQUAL:
            matched = left_float <= right_float;
            break;
        case SCALAR_COMPARISON_GREATER:
            matched = left_float > right_float;
            break;
        default:
            matched = left_float >= right_float;
            break;
        }
    } else if (result_type.scalar_class == SCALAR_CLASS_UNSIGNED) {
        switch (comparison) {
        case SCALAR_COMPARISON_EQUAL:
            matched = left_unsigned == right_unsigned;
            break;
        case SCALAR_COMPARISON_LESS:
            matched = left_unsigned < right_unsigned;
            break;
        case SCALAR_COMPARISON_LESS_EQUAL:
            matched = left_unsigned <= right_unsigned;
            break;
        case SCALAR_COMPARISON_GREATER:
            matched = left_unsigned > right_unsigned;
            break;
        default:
            matched = left_unsigned >= right_unsigned;
            break;
        }
    } else {
        switch (comparison) {
        case SCALAR_COMPARISON_EQUAL:
            matched = left_signed == right_signed;
            break;
        case SCALAR_COMPARISON_LESS:
            matched = left_signed < right_signed;
            break;
        case SCALAR_COMPARISON_LESS_EQUAL:
            matched = left_signed <= right_signed;
            break;
        case SCALAR_COMPARISON_GREATER:
            matched = left_signed > right_signed;
            break;
        default:
            matched = left_signed >= right_signed;
            break;
        }
    }
    result = (uint8_t)((matched != 0) != (invert != 0) ? 1U : 0U);
    return vmbin_value_access_write_body(
        frame, &instruction->write_arguments.data[0],
        runtimec_bytes_view_make(&result, 1U), error);
}

/*
 * bool·eq / bool·neq（0x0022 / 0x002B）—— catalog 掩码只给 bool，传别的种类
 * 一律 INVALID（matrix 的 boolean-equality，不是「按真值比较」）。
 */
static runtimec_status_t scalar_boolean_equality(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, int invert,
    runtimec_error_t *error) {
    uint8_t left_bytes[SCALAR_BODY_CAPACITY];
    uint8_t right_bytes[SCALAR_BODY_CAPACITY];
    scalar_type_t type;
    uint8_t result;
    runtimec_status_t status;

    if (instruction->read_arguments.count != 2U ||
        instruction->write_arguments.count != 1U) {
        return scalar_fail(error, 30,
                           "scalar handler: bool equality expects two reads "
                           "and one write");
    }
    status = scalar_read_operand(frame, &instruction->read_arguments.data[0],
                                 left_bytes, &type, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (!scalar_langtype_is_boolean(&type)) {
        return scalar_fail(error, 31,
                           "scalar handler: bool equality requires bool "
                           "operands");
    }
    status = scalar_read_operand(frame, &instruction->read_arguments.data[1],
                                 right_bytes, &type, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (!scalar_langtype_is_boolean(&type)) {
        return scalar_fail(error, 32,
                           "scalar handler: bool equality requires bool "
                           "operands");
    }
    /* bool 的 body 是 1 字节，非零即 true。 */
    result = (uint8_t)(((left_bytes[0] != 0U) == (right_bytes[0] != 0U))
                           ? 1U
                           : 0U);
    if (invert) {
        result = result != 0U ? 0U : 1U;
    }
    return vmbin_value_access_write_body(
        frame, &instruction->write_arguments.data[0],
        runtimec_bytes_view_make(&result, 1U), error);
}

#define SCALAR_COMPARISON_HANDLER(name, comparison, invert)                   \
    static runtimec_status_t name(                                            \
        binrwir_binvthread_frame_t *frame,                                     \
        const binrwir_instruction_view_t *instruction,                        \
        binrwir_execution_exit_t *output_exit) {                              \
        runtimec_error_t error;                                               \
        (void)output_exit;                                                    \
        runtimec_error_reset(&error);                                         \
        return scalar_compare(frame, instruction, comparison, invert,         \
                              &error);                                        \
    }

#define SCALAR_BOOLEAN_HANDLER(name, invert)                                  \
    static runtimec_status_t name(                                            \
        binrwir_binvthread_frame_t *frame,                                     \
        const binrwir_instruction_view_t *instruction,                        \
        binrwir_execution_exit_t *output_exit) {                              \
        runtimec_error_t error;                                               \
        (void)output_exit;                                                    \
        runtimec_error_reset(&error);                                         \
        return scalar_boolean_equality(frame, instruction, invert, &error);   \
    }

SCALAR_COMPARISON_HANDLER(scalar_handle_eq, SCALAR_COMPARISON_EQUAL, 0)
SCALAR_COMPARISON_HANDLER(scalar_handle_neq, SCALAR_COMPARISON_EQUAL, 1)
SCALAR_COMPARISON_HANDLER(scalar_handle_lt, SCALAR_COMPARISON_LESS, 0)
SCALAR_COMPARISON_HANDLER(scalar_handle_le, SCALAR_COMPARISON_LESS_EQUAL, 0)
SCALAR_COMPARISON_HANDLER(scalar_handle_gt, SCALAR_COMPARISON_GREATER, 0)
SCALAR_COMPARISON_HANDLER(scalar_handle_ge, SCALAR_COMPARISON_GREATER_EQUAL, 0)
SCALAR_BOOLEAN_HANDLER(scalar_handle_bool_eq, 0)
SCALAR_BOOLEAN_HANDLER(scalar_handle_bool_neq, 1)

/*
 * ptreq（0x0021）— rwir_catalog.def:282，behavior-decision `pointer-equality`。
 *
 * matrix:66：两个指针相等**当且仅当它们携带相同的目标 key**；None equals None；
 * 携带目标的指针永不等于 None；**指针对非指针返回 INVALID**。
 *
 * 身份来自 wire：Ptr 的 body 就是目标 KV 路径（spec 03-类型系统/03-种类），
 * 空 body 即空引用（= None）。判定「这是不是指针」必须读 head 的 `ptr` 字节
 * ——wire langtype 不含 `*` 前缀（spec 07-文法与合法性:100），只能靠 ref 位。
 *
 * **ref 不足以判定 None**：None 的 ref 与普通标量同为 `INLINE`，只凭 ref 分不开
 * 指针 / None / 标量这三态。所以必须先读 body，再按 `(ref, body 是否为空)` 判：
 *   - ref == PTR              → 指针（body 是目标 key）
 *   - ref != PTR 且 body 为空  → None
 *   - ref != PTR 且 body 非空  → 普通标量（ptreq 不接受）
 *
 * **指针 vs None 是合法比较、结果为假**，不是 INVALID——matrix:66 原文是
 * 「a pointer carrying a target never equals None」。INVALID 只留给
 * 「指针对非指针非 None」。
 * 这两件事都由 `vmbin_value_access_form` 与 `_read_body` 提供，
 * 本实现不写任何偏移算术、不从 body 形状猜类型。
 *
 * `@ext` 的「相等」未定义（定位串相同 ≠ 同一个扩展对象），按裁决返回
 * UNSUPPORTED 并点名原因，不发明一种表示。
 */
static runtimec_status_t scalar_pointer_equality(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, int invert,
    runtimec_error_t *error) {
    uint8_t left_bytes[SCALAR_CHAR_TEXT_CAPACITY];
    uint8_t right_bytes[SCALAR_CHAR_TEXT_CAPACITY];
    runtimec_bytes_buffer_t left_buffer = {left_bytes, 0U, sizeof(left_bytes)};
    runtimec_bytes_buffer_t right_buffer = {
        right_bytes, 0U, sizeof(right_bytes)};
    uint8_t left_form = VMBIN_VALUE_ACCESS_FORM_VALUE;
    uint8_t right_form = VMBIN_VALUE_ACCESS_FORM_VALUE;
    uint8_t result;
    runtimec_status_t status;

    if (instruction->read_arguments.count != 2U ||
        instruction->write_arguments.count != 1U) {
        return scalar_fail(error, 33,
                           "scalar handler: ptreq expects two reads and one "
                           "write");
    }
    /*
     * **用不跟跳的取形入口**：`ptreq` 比较的是「两个指针是否指向同一目标」
     * （matrix:66），要指针值本身的形态。
     *
     * 跟跳之后：① `h·*k != None` 判存在时，目标本就不存在，跟跳直接失败；
     * ② 即使跳成功，拿到的是目标的形态而非指针的形态。
     */
    status = vmbin_value_access_form_unfollowed(
        frame, &instruction->read_arguments.data[0], &left_form, error);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = vmbin_value_access_form_unfollowed(
            frame, &instruction->read_arguments.data[1], &right_form, error);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    /* @ext 的相等语义未定，两侧任一命中就明确拒绝，不猜。 */
    if (left_form == VMBIN_VALUE_ACCESS_FORM_EXTENSION ||
        right_form == VMBIN_VALUE_ACCESS_FORM_EXTENSION) {
        return scalar_fail(
            error, 34,
            "scalar handler: ptreq on @ext is undefined (locator equality is "
            "not specified)");
    }
    /*
     * None 是合法操作数（matrix:66「None equals None」）。spec 03-类型系统/03 的
     * None 判据有两条路径：storetype 0 的 None 形态（ref 为 INLINE），或 ptr=1
     * 且 body 为空。前者在 ref 上看起来和普通标量一样，所以「两侧都是 INLINE」
     * 时先按 body 读出来再判：两个空 body 就是 None==None。
     */
    /*
     * 两侧的字节都要读出来：None 的判据是「body 为空」，而 None 的 ref 与普通
     * 标量同为 INLINE，仅凭 ref 分不开。所以先读 body，再按 (ref, body 是否为空)
     * 三态判定：指针 / None / 标量。
     */
    status = vmbin_value_access_read_body(
        frame, &instruction->read_arguments.data[0], &left_buffer, error);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = vmbin_value_access_read_body(
            frame, &instruction->read_arguments.data[1], &right_buffer, error);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    {
        const int left_is_pointer = left_form == VMBIN_VALUE_ACCESS_FORM_POINTER;
        const int right_is_pointer = right_form == VMBIN_VALUE_ACCESS_FORM_POINTER;
        /* None：不是指针、且 body 为空（spec 03-类型系统/03 的两条判据合取）。 */
        const int left_is_none = !left_is_pointer && left_buffer.size == 0U;
        const int right_is_none = !right_is_pointer && right_buffer.size == 0U;

        if (left_is_none && right_is_none) {
            /* None equals None。 */
            result = (uint8_t)(invert ? 0U : 1U);
        } else if (left_is_pointer && right_is_pointer) {
            /* 两侧都是指针：body（目标 key）逐字节比较；空 body 的指针是空引用，
             * 与另一个空引用相等、与非空永不相等。 */
            const int matched =
                left_buffer.size == right_buffer.size &&
                (left_buffer.size == 0U ||
                 memcmp(left_buffer.data, right_buffer.data,
                        left_buffer.size) == 0);

            result = (uint8_t)((matched != 0) != (invert != 0) ? 1U : 0U);
        } else if (left_is_none || right_is_none) {
            /*
             * 一侧是 None、另一侧是指针：matrix:66「a pointer carrying a target
             * never equals None」——这是**合法的比较**，结果为不等，不是 INVALID。
             */
            result = (uint8_t)(invert ? 1U : 0U);
        } else if (left_is_pointer || right_is_pointer) {
            /*
             * 一侧指针、另一侧既不是指针也不是 None：按 matrix:66 是
             * 「指针对非指针 → INVALID」，不是「不等」。
             */
            return scalar_fail(
                error, 35,
                "scalar handler: ptreq compares a pointer with a non-pointer");
        } else {
            /* 两侧都是非 None 的标量：ptreq 不接受。 */
            return scalar_fail(error, 36,
                               "scalar handler: ptreq requires pointer operands");
        }
    }
    return vmbin_value_access_write_body(
        frame, &instruction->write_arguments.data[0],
        runtimec_bytes_view_make(&result, 1U), error);
}

SCALAR_FORWARDING_HANDLER_WITH_ARGUMENT(scalar_handle_ptreq, scalar_pointer_equality, 0)
/* ptreq·not：与 ptreq 同构，只把 invert 置 1——和 streq/streq·not、
 * bool·eq/bool·neq 两对完全同形。不为它单开一条实现体：三对同构，
 * 第四对也该同构，否则「取反」这件事就有四下写法。 */
SCALAR_FORWARDING_HANDLER_WITH_ARGUMENT(scalar_handle_ptreq_not,
                                        scalar_pointer_equality, 1)

static runtimec_status_t scalar_handle_debugger(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    static const char paused[] = "paused";
    char status_key[RUNTIMEC_VTHREAD_STATUS_CAPACITY];
    runtimec_error_t error;
    void *handle = NULL;
    runtimec_status_t status;

    runtimec_error_reset(&error);
    *output_exit = (binrwir_execution_exit_t){0};
    if (instruction->read_arguments.count != 0U ||
        instruction->write_arguments.count != 0U) {
        return scalar_fail(&error, 27,
                           "scalar handler: debugger takes no arguments");
    }
    status = runtimec_vthread_status_handle(frame, &handle);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_vthread_status_key(
            frame->frame_path, runtimec_string_view_from_c_string("/‥status"),
            status_key, sizeof(status_key));
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_vthread_status_write(
            handle, status_key,
            runtimec_bytes_view_make(paused, sizeof(paused) - 1U));
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        output_exit->kind = BINRWIR_EXECUTION_EXIT_ERROR;
        output_exit->status = status;
        output_exit->logical_program_counter =
            instruction->source_logical_program_counter;
        return status;
    }
    /* 停滞出口：非完成、非错误；游标不推进（解释器按 YIELD 处理）。 */
    output_exit->kind = BINRWIR_EXECUTION_EXIT_YIELD;
    output_exit->status = RUNTIMEC_STATUS_SUCCESS;
    output_exit->logical_program_counter =
        instruction->source_logical_program_counter;
    output_exit->resume_logical_program_counter =
        instruction->source_logical_program_counter;
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 静态存储，进程级生命周期；调用方不得释放。 */
/*
 * `nonnull`（0x01A4）/ `nonnull·not`（0x01A5）—— 存在性判定。
 *
 * behavior-decision `none-criterion`（spec `03-类型系统/03-种类-代码与特殊值.kv`
 * 「None 判据」）：`is_none(v)` 当且仅当 **body 为空 + wire 无 langtype + key 不是
 * 目录**。所以 `nonnull(x)` 回答「x 这个位置有值吗」，不是「x 是不是零」。
 *
 * ── 与「零值」的关键区分（必须写进测试，不许含糊） ────────────────────
 *
 * 已物化的零值 local **存在**：`x:int64=0` 的 wire 带 langtype `int64`，
 * `is_none` 第 2 条不成立 ⇒ `x == None` 为 **false**。
 * 声明未写的局部同样由帧准备物化零值 ⇒ 也是 false。kvlang 的局部是**存储位置**，
 * 「未写」不等于「不存在」——这与 C 的未初始化内存是两回事。
 *
 * ── 缺失成员：locate 失败即视为「无值」，不许把 0x4403 抛出去 ──────────
 *
 * `m·*k != None` 的 `m·*k` 可能是**从没写过的成员键**。普通读路径此时报
 * `UNBOUND_SLOT`（`0x4403`）；但存在性判定问的正是「有没有」，把它当错误抛出去
 * 等于让这条语义在最常见的场景下不可用。所以本算子自己接住 locate 失败：
 * 判为「无值」⇒ `nonnull` false / `nonnull·not` true。
 *
 * **只影响这两个算子**：普通读缺失成员仍然失败，语义不变。
 *
 * ── 判据走值访问层，不自己扫字节 ──────────────────────────────────────
 *
 * 「是不是 None」用 `vmbin_value_access_form`（与 `ptreq` 同一入口）：它按
 * ref + body 判定 Form，本实现不写偏移算术、不从 body 形状猜类型。
 * 「有没有 langtype」由 `_langtype` 取——取不到就是无记录。两条同时成立即 None。
 */
static runtimec_status_t scalar_nonexistence(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, int invert,
    runtimec_error_t *error) {
    const binrwir_argument_t *operand;
    runtimec_string_view_t langtype;
    vmbin_value_location_t location;
    uint8_t form = VMBIN_VALUE_ACCESS_FORM_VALUE;
    uint8_t result;
    runtimec_status_t status;

    if (instruction->read_arguments.count != 1U ||
        instruction->write_arguments.count != 1U) {
        return scalar_fail(error, 35,
                           "scalar handler: nonnull expects one read and one "
                           "write");
    }
    operand = &instruction->read_arguments.data[0];
    /*
     * **缺失位置先接住**：locate 失败在这里不是错误，是「没有值」这个答案。
     * 这是本算子与所有其它算子唯一不同的一处，且只对它成立。
     */
    status = vmbin_value_access_locate_body(frame, operand, &location, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        {
            uint8_t missing = invert != 0 ? 1U : 0U;

            return vmbin_value_access_write_body(
                frame, &instruction->write_arguments.data[0],
                runtimec_bytes_view_make(&missing, 1U), error);
        }
    }
    /* 引用槽有值时也是「存在」——ptr 指向某处与「没有值」是两件事。 */
    status = vmbin_value_access_form(frame, operand, &form, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (form == VMBIN_VALUE_ACCESS_FORM_EXTENSION) {
        /* @ext 的运行时语义本期不实现（spec 03-类型系统/03「扩展定位」），
         * 存在性判定同样无从兑现——明确拒绝，不猜。 */
        return scalar_fail(error, 36,
                           "scalar handler: nonnull on @ext is undefined "
                           "(extension semantics are not implemented)");
    }
    /*
     * **取不到类型 ≠ 值是 None。**
     *
     * 这里踩过一次：`eq0 = x == None` 的 `eq0` 是**未声明类型的局部**，
     * `_langtype` 对它失败（没有声明），我原先把这条失败当成「无值」直接写
     * false/true——于是任何对无类型局部的判空都答「是 None」，而错误现场被
     * 折成一条与成因无关的 `0x4A03`。
     *
     * 判据分开：**取不到类型是「答不出」**，必须具名失败；「有类型但 body 空」
     * 才是 None（`is_none` 的三条里第 2 条问的是「wire 有没有 langtype 记录」，
     * 不是「取类型这个操作成不成功」）。
     */
    status = vmbin_value_access_langtype(frame, operand, &langtype, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return scalar_fail(error, 37,
                           "scalar handler: nonnull cannot tell whether the "
                           "operand is None — its declared type is unavailable");
    }
    /*
     * `is_none` 三条：
     *   body 为空（location.size == 0）
     *   且 wire 无 langtype 记录（langtype.size == 0）
     *   且 key 不是目录（这一条由「能作为值读到」隐含：目录不是值位置）
     */
    result = (location.size == 0U && langtype.size == 0U) ? 0U : 1U;
    if (invert != 0) {
        result = result != 0U ? 0U : 1U;
    }
    return vmbin_value_access_write_body(
        frame, &instruction->write_arguments.data[0],
        runtimec_bytes_view_make(&result, 1U), error);
}

/*
 * 注册用的薄封装：主体是 `scalar_nonexistence`，注册表要的是函数地址。
 *
 * 用本文件已有的 `SCALAR_FORWARDING_HANDLER_WITH_ARGUMENT`（`ptreq`/`ptreq·not`
 * 同一族就是这个形状），而不是手写两个只差 `invert` 常量的壳——那会让
 * 「转发」这条规则出现第二次抄写。
 */
SCALAR_FORWARDING_HANDLER_WITH_ARGUMENT(scalar_handle_nonnull,
                                        scalar_nonexistence, 0)
SCALAR_FORWARDING_HANDLER_WITH_ARGUMENT(scalar_handle_nonnull_not,
                                        scalar_nonexistence, 1)

static const binrwir_registry_entry_t scalar_entries[] = {
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_MAX,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scalar_handle_max},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_MIN,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scalar_handle_min},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_ABSOLUTE,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scalar_handle_abs},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_NEGATION,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scalar_handle_neg},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_SIGN,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scalar_handle_sign},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_SHIFT_LEFT,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scalar_handle_shl},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_SHIFT_RIGHT,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scalar_handle_shr},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_SQUARE_ROOT,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scalar_handle_sqrt},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_EXPONENTIAL,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scalar_handle_exp},
    /* `pow` 的**基础条目**：为测试矩阵完整而存在，不是生产入口（生产走特化）。
     * 见 `scalar_power` 的注释。 */
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_POWER,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scalar_handle_pow},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_LOGARITHM,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scalar_handle_log},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_LOGICAL_AND,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scalar_handle_and},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_LOGICAL_OR,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scalar_handle_or},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_LOGICAL_NOT,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scalar_handle_not},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_CAST,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scalar_handle_cast},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_CONCAT,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scalar_handle_concat},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_CHARACTER_EQUAL,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scalar_handle_streq},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_CHARACTER_NOT_EQUAL,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scalar_handle_streq_not},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_DEBUGGER,
     .flags = BINRWIR_REGISTRY_FLAG_MAY_YIELD,
     .handler = scalar_handle_debugger},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_EQUAL,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scalar_handle_eq},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_NOT_EQUAL,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scalar_handle_neq},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_LESS,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scalar_handle_lt},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_LESS_EQUAL,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scalar_handle_le},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_GREATER,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scalar_handle_gt},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_GREATER_EQUAL,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scalar_handle_ge},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_BOOLEAN_EQUAL,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scalar_handle_bool_eq},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_BOOLEAN_NOT_EQUAL,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scalar_handle_bool_neq},
    /* `nonnull` / `nonnull·not`：存在性判定，一元 → bool。 */
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_NONNULL,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scalar_handle_nonnull},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_NONNULL_NOT,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scalar_handle_nonnull_not},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_POINTER_EQUAL,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scalar_handle_ptreq},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_POINTER_NOT_EQUAL,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scalar_handle_ptreq_not},
};

const binrwir_registry_entry_t *runtimec_scalar_handler_entries(
    size_t *output_count) {
    if (output_count == NULL) {
        return NULL;
    }
    *output_count = sizeof(scalar_entries) / sizeof(scalar_entries[0]);
    return scalar_entries;
}
