
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
    uint8_t width; 
} scalar_type_t;

static int scalar_langtype_is_boolean(const scalar_type_t *type) {
    return type->scalar_class == SCALAR_CLASS_BOOLEAN;
}

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
        
        *output_float = (double)raw;
    } else {
        *output_float = (double)signed_value;
    }
}

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

typedef enum scalar_unary_operation {
    SCALAR_UNARY_ABS = 0,
    SCALAR_UNARY_NEG = 1,
    SCALAR_UNARY_SIGN = 2,
    SCALAR_UNARY_SQRT = 3,
    SCALAR_UNARY_EXP = 4,
    SCALAR_UNARY_LOG = 5
} scalar_unary_operation_t;

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

static int scalar_boundary_is_truthy(const uint8_t *bytes,
                                     const scalar_type_t *type) {
    int64_t signed_value = 0;
    uint64_t unsigned_value = 0U;
    double float_value = 0.0;

    if (type->scalar_class == SCALAR_CLASS_FLOAT) {
        scalar_load(bytes, type, &signed_value, &unsigned_value, &float_value);
        
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
                
                return scalar_fail(error, 21,
                                   "scalar handler: cast of NaN to an integer");
            }
            
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
    
    count = (unsigned)(count_unsigned & 63U);
    if (value_type.scalar_class == SCALAR_CLASS_UNSIGNED) {
        result_unsigned = shift_left ? (value_unsigned << count)
                                     : (value_unsigned >> count);
    } else {
        
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

#define SCALAR_CHAR_TEXT_CAPACITY 1024U

static int scalar_langtype_is(runtimec_string_view_t langtype, const char *text) {
    const size_t size = strlen(text);

    return langtype.size == size && langtype.data != NULL &&
           memcmp(langtype.data, text, size) == 0;
}

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
    
    status = vmbin_value_access_form_unfollowed(
        frame, &instruction->read_arguments.data[0], &left_form, error);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = vmbin_value_access_form_unfollowed(
            frame, &instruction->read_arguments.data[1], &right_form, error);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    
    if (left_form == VMBIN_VALUE_ACCESS_FORM_EXTENSION ||
        right_form == VMBIN_VALUE_ACCESS_FORM_EXTENSION) {
        return scalar_fail(
            error, 34,
            "scalar handler: ptreq on @ext is undefined (locator equality is "
            "not specified)");
    }
    
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
        
        const int left_is_none = !left_is_pointer && left_buffer.size == 0U;
        const int right_is_none = !right_is_pointer && right_buffer.size == 0U;

        if (left_is_none && right_is_none) {
            
            result = (uint8_t)(invert ? 0U : 1U);
        } else if (left_is_pointer && right_is_pointer) {
            
            const int matched =
                left_buffer.size == right_buffer.size &&
                (left_buffer.size == 0U ||
                 memcmp(left_buffer.data, right_buffer.data,
                        left_buffer.size) == 0);

            result = (uint8_t)((matched != 0) != (invert != 0) ? 1U : 0U);
        } else if (left_is_none || right_is_none) {
            
            result = (uint8_t)(invert ? 1U : 0U);
        } else if (left_is_pointer || right_is_pointer) {
            
            return scalar_fail(
                error, 35,
                "scalar handler: ptreq compares a pointer with a non-pointer");
        } else {
            
            return scalar_fail(error, 36,
                               "scalar handler: ptreq requires pointer operands");
        }
    }
    return vmbin_value_access_write_body(
        frame, &instruction->write_arguments.data[0],
        runtimec_bytes_view_make(&result, 1U), error);
}

SCALAR_FORWARDING_HANDLER_WITH_ARGUMENT(scalar_handle_ptreq, scalar_pointer_equality, 0)

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
    
    output_exit->kind = BINRWIR_EXECUTION_EXIT_YIELD;
    output_exit->status = RUNTIMEC_STATUS_SUCCESS;
    output_exit->logical_program_counter =
        instruction->source_logical_program_counter;
    output_exit->resume_logical_program_counter =
        instruction->source_logical_program_counter;
    return RUNTIMEC_STATUS_SUCCESS;
}

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
    
    status = vmbin_value_access_locate_body(frame, operand, &location, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        {
            uint8_t missing = invert != 0 ? 1U : 0U;

            return vmbin_value_access_write_body(
                frame, &instruction->write_arguments.data[0],
                runtimec_bytes_view_make(&missing, 1U), error);
        }
    }
    
    status = vmbin_value_access_form(frame, operand, &form, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (form == VMBIN_VALUE_ACCESS_FORM_EXTENSION) {
        
        return scalar_fail(error, 36,
                           "scalar handler: nonnull on @ext is undefined "
                           "(extension semantics are not implemented)");
    }
    
    status = vmbin_value_access_langtype(frame, operand, &langtype, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return scalar_fail(error, 37,
                           "scalar handler: nonnull cannot tell whether the "
                           "operand is None — its declared type is unavailable");
    }
    
    result = (location.size == 0U && langtype.size == 0U) ? 0U : 1U;
    if (invert != 0) {
        result = result != 0U ? 0U : 1U;
    }
    return vmbin_value_access_write_body(
        frame, &instruction->write_arguments.data[0],
        runtimec_bytes_view_make(&result, 1U), error);
}

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
