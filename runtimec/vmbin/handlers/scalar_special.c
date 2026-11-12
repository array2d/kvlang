
#include "vmbin/handlers/scalar_special.h"

#include <math.h>
#include <stdint.h>

double runtimec_scalar_float_min(double left, double right) {
    if (isnan(left)) {
        return right;
    }
    if (isnan(right)) {
        return left;
    }
    return left < right ? left : right;
}

double runtimec_scalar_float_max(double left, double right) {
    if (isnan(left)) {
        return right;
    }
    if (isnan(right)) {
        return left;
    }
    return left > right ? left : right;
}

void runtimec_scalar_divide_signed(int64_t left, int64_t right,
                                   uint32_t width, int64_t *output_value,
                                   binrwir_execution_exit_t *output_exit) {
    const int64_t minimum = -(INT64_C(1) << (width - 1U));

    if (right == 0) {
        runtimec_scalar_fail(output_exit, RUNTIMEC_SCALAR_DIAG_DIVIDE_BY_ZERO);
        return;
    }
    if (left == minimum && right == -1) {
        
        *output_value = minimum;
        return;
    }
    *output_value = left / right;
}

void runtimec_scalar_remainder_signed(int64_t left, int64_t right,
                                      uint32_t width, int64_t *output_value,
                                      binrwir_execution_exit_t *output_exit) {
    const int64_t minimum = -(INT64_C(1) << (width - 1U));

    if (right == 0) {
        runtimec_scalar_fail(output_exit, RUNTIMEC_SCALAR_DIAG_DIVIDE_BY_ZERO);
        return;
    }
    if (left == minimum && right == -1) {
        
        *output_value = 0;
        return;
    }
    *output_value = left % right;
}

void runtimec_scalar_divide_unsigned(uint64_t left, uint64_t right,
                                     uint64_t *output_value,
                                     binrwir_execution_exit_t *output_exit) {
    if (right == 0U) {
        runtimec_scalar_fail(output_exit, RUNTIMEC_SCALAR_DIAG_DIVIDE_BY_ZERO);
        return;
    }
    *output_value = left / right;
}

void runtimec_scalar_remainder_unsigned(uint64_t left, uint64_t right,
                                        uint64_t *output_value,
                                        binrwir_execution_exit_t *output_exit) {
    if (right == 0U) {
        runtimec_scalar_fail(output_exit, RUNTIMEC_SCALAR_DIAG_DIVIDE_BY_ZERO);
        return;
    }
    *output_value = left % right;
}

void runtimec_scalar_float_to_signed(double source, uint32_t width,
                                     int64_t *output_value,
                                     binrwir_execution_exit_t *output_exit) {
    const int64_t maximum = (int64_t)((UINT64_C(1) << (width - 1U)) - 1U);
    const int64_t minimum = -(INT64_C(1) << (width - 1U));

    if (isnan(source)) {
        runtimec_scalar_fail(output_exit, RUNTIMEC_SCALAR_DIAG_NOT_A_NUMBER);
        return;
    }
    if (source >= (double)maximum) {
        *output_value = maximum;
        return;
    }
    if (source <= (double)minimum) {
        *output_value = minimum;
        return;
    }
    *output_value = (int64_t)source;
}

void runtimec_scalar_float_to_unsigned(double source, uint32_t width,
                                       uint64_t *output_value,
                                       binrwir_execution_exit_t *output_exit) {
    const uint64_t maximum =
        width == 64U ? UINT64_MAX : ((UINT64_C(1) << width) - 1U);

    if (isnan(source)) {
        runtimec_scalar_fail(output_exit, RUNTIMEC_SCALAR_DIAG_NOT_A_NUMBER);
        return;
    }
    if (source >= (double)maximum) {
        *output_value = maximum;
        return;
    }
    if (source <= 0.0) {
        *output_value = 0U;
        return;
    }
    *output_value = (uint64_t)source;
}

int64_t runtimec_scalar_wrap_signed(int64_t source, uint32_t source_width,
                                    uint32_t target_width) {
    const uint32_t shift = 64U - target_width;

    (void)source_width;
    
    return (int64_t)((uint64_t)source << shift) >> shift;
}

uint64_t runtimec_scalar_wrap_unsigned(uint64_t source,
                                       uint32_t target_width) {
    return target_width == 64U ? source
                               : (source & ((UINT64_C(1) << target_width) - 1U));
}

int runtimec_scalar_compare_signed_unsigned(int64_t signed_value,
                                            uint64_t unsigned_value) {
    if (signed_value < 0) {
        return -1;
    }
    return (uint64_t)signed_value < unsigned_value
               ? -1
               : ((uint64_t)signed_value > unsigned_value ? 1 : 0);
}

int runtimec_scalar_compare_unsigned_signed(uint64_t unsigned_value,
                                            int64_t signed_value) {
    return -runtimec_scalar_compare_signed_unsigned(signed_value,
                                                    unsigned_value);
}

int64_t runtimec_scalar_shift_left_signed(int64_t source, int64_t count,
                                          uint32_t width) {
    const uint64_t mask = (uint64_t)width - 1U;
    
    const uint64_t shifted =
        (uint64_t)source << ((uint64_t)count & mask);
    return runtimec_scalar_wrap_signed((int64_t)shifted, 64U, width);
}

uint64_t runtimec_scalar_shift_left_unsigned(uint64_t source, uint64_t count,
                                             uint32_t width) {
    const uint64_t mask = (uint64_t)width - 1U;
    return runtimec_scalar_wrap_unsigned(source << (count & mask), width);
}

int64_t runtimec_scalar_shift_right_signed(int64_t source, int64_t count,
                                           uint32_t width) {
    const uint64_t mask = (uint64_t)width - 1U;
    
    const int64_t narrowed =
        runtimec_scalar_wrap_signed(source, 64U, width);
    const uint64_t shift = (uint64_t)count & mask;

    if (shift == 0U) {
        return narrowed;
    }
    
    {
        const uint64_t raw = (uint64_t)narrowed & runtimec_scalar_wrap_unsigned(
                                                     UINT64_MAX, width);
        const uint64_t logical = raw >> shift;
        const uint64_t sign_bit =
            (raw >> (width - 1U)) & UINT64_C(1);
        const uint64_t extension =
            sign_bit != 0U
                ? (runtimec_scalar_wrap_unsigned(UINT64_MAX, width) <<
                   (width - shift))
                : 0U;
        return runtimec_scalar_wrap_signed(
            (int64_t)runtimec_scalar_wrap_unsigned(logical | extension, width),
            64U, width);
    }
}

uint64_t runtimec_scalar_shift_right_unsigned(uint64_t source, uint64_t count,
                                              uint32_t width) {
    const uint64_t mask = (uint64_t)width - 1U;
    const uint64_t narrowed = runtimec_scalar_wrap_unsigned(source, width);

    return narrowed >> (count & mask);
}

void runtimec_scalar_cast_to_bool(bool source, bool *output_value,
                                  binrwir_execution_exit_t *output_exit) {
    if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) {
        
        return;
    }
    *output_value = source;
}

void runtimec_scalar_cast_to_bool_invalid(binrwir_execution_exit_t *output_exit) {
    runtimec_scalar_fail(output_exit,
                         RUNTIMEC_SCALAR_DIAG_OPERAND_NOT_NUMERIC);
}
