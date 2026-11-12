#ifndef VMBIN_HANDLERS_SCALAR_SPECIAL_H
#define VMBIN_HANDLERS_SCALAR_SPECIAL_H

#include <stdbool.h>
#include <stdint.h>

#include "binrwir/execution_exit.h"
#include "vmbin/handlers/scalar.h"

#ifdef __cplusplus
extern "C" {
#endif

double runtimec_scalar_float_min(double left, double right);
double runtimec_scalar_float_max(double left, double right);

void runtimec_scalar_divide_signed(int64_t left, int64_t right,
                                   uint32_t width, int64_t *output_value,
                                   binrwir_execution_exit_t *output_exit);
void runtimec_scalar_remainder_signed(int64_t left, int64_t right,
                                      uint32_t width, int64_t *output_value,
                                      binrwir_execution_exit_t *output_exit);
void runtimec_scalar_divide_unsigned(uint64_t left, uint64_t right,
                                     uint64_t *output_value,
                                     binrwir_execution_exit_t *output_exit);
void runtimec_scalar_remainder_unsigned(uint64_t left, uint64_t right,
                                        uint64_t *output_value,
                                        binrwir_execution_exit_t *output_exit);

void runtimec_scalar_float_to_signed(double source, uint32_t width,
                                     int64_t *output_value,
                                     binrwir_execution_exit_t *output_exit);
void runtimec_scalar_float_to_unsigned(double source, uint32_t width,
                                       uint64_t *output_value,
                                       binrwir_execution_exit_t *output_exit);

int64_t runtimec_scalar_wrap_signed(int64_t source, uint32_t source_width,
                                    uint32_t target_width);
uint64_t runtimec_scalar_wrap_unsigned(uint64_t source,
                                       uint32_t target_width);

int runtimec_scalar_compare_signed_unsigned(int64_t signed_value,
                                            uint64_t unsigned_value);
int runtimec_scalar_compare_unsigned_signed(uint64_t unsigned_value,
                                            int64_t signed_value);

int64_t runtimec_scalar_shift_left_signed(int64_t source, int64_t count,
                                          uint32_t width);
uint64_t runtimec_scalar_shift_left_unsigned(uint64_t source, uint64_t count,
                                             uint32_t width);
int64_t runtimec_scalar_shift_right_signed(int64_t source, int64_t count,
                                           uint32_t width);
uint64_t runtimec_scalar_shift_right_unsigned(uint64_t source, uint64_t count,
                                              uint32_t width);

void runtimec_scalar_cast_to_bool(bool source, bool *output_value,
                                  binrwir_execution_exit_t *output_exit);
void runtimec_scalar_cast_to_bool_invalid(binrwir_execution_exit_t *output_exit);

#ifdef __cplusplus
}
#endif

#endif
