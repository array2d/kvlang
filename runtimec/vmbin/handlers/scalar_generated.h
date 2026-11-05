#ifndef VMBIN_HANDLERS_SCALAR_GENERATED_H
#define VMBIN_HANDLERS_SCALAR_GENERATED_H

/*
 * 组件：vmbin/handlers/scalar_generated
 * 作用：特化标量 handler 的静态条目表——一条特化 opid 一个 inline 函数。
 * 联系：`scalar_generated.c` 由 tools/architecture/generate_scalar_handlers.py
 *       生成；`scalar.c` 把它与手写特例合并后交给 session。
 * 约束：
 *   - 条目是静态存储，进程级生命周期；调用方不得释放；
 *   - 产品可见的只有表与计数——生成的逻辑函数是 `static inline`，只被条目引用；
 *   - 末尾的 `runtimec_scalar_probe_*` 是**测试探针**：同一份模板文本的可见副本。
 *     它存在的唯一原因是 C 没有反射，测试翻译单元看不见 `static inline`。没有它，
 *     「生成的函数算得对不对」就只能靠表形状间接猜——那等于没测（实测过：把 min
 *     的取反做变异，只验表形状的测试全绿）。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "binrwir/execution_exit.h"
#include "binrwir/registry.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const binrwir_registry_entry_t scalar_generated_entries[];
extern const size_t scalar_generated_entry_count;

/*
 * 测试探针。**不是产品 API**：产品代码不得调用，它们没有做任何参数校验，
 * 只做一次运算。
 */
void runtimec_scalar_probe_add_int64_int32_int64(const int64_t *a,
                                                 const int32_t *b,
                                                 int64_t *c);
void runtimec_scalar_probe_mul_int32_int32_int32(const int32_t *a,
                                                 const int32_t *b,
                                                 int32_t *c);
void runtimec_scalar_probe_min_int64_int32_int64(const int64_t *a,
                                                 const int32_t *b,
                                                 int64_t *c);
void runtimec_scalar_probe_max_int32_int32_int32(const int32_t *a,
                                                 const int32_t *b,
                                                 int32_t *c);
void runtimec_scalar_probe_less_int64_uint64_bool(const int64_t *a,
                                                  const uint64_t *b,
                                                  bool *c);
void runtimec_scalar_probe_equal_float64_int8_bool(const double *a,
                                                   const int8_t *b,
                                                   bool *c);
void runtimec_scalar_probe_logical_and_uint8_float64_bool(const uint8_t *a,
                                                          const double *b,
                                                          bool *c);
void runtimec_scalar_probe_bitwise_or_int8_int64_int64(const int8_t *a,
                                                       const int64_t *b,
                                                       int64_t *c);
void runtimec_scalar_probe_square_root_uint64_float64(const uint64_t *a,
                                                      double *out);
void runtimec_scalar_probe_negation_int64_int64(const int64_t *a,
                                                int64_t *out);
void runtimec_scalar_probe_absolute_int8_int8(const int8_t *a, int8_t *out);
void runtimec_scalar_probe_sign_float64_float64(const double *a, double *out);
void runtimec_scalar_probe_shift_left_int8_int8_int8(const int8_t *a,
                                                     const int8_t *b,
                                                     int8_t *c);
void runtimec_scalar_probe_shift_right_int64_int8_int64(const int64_t *a,
                                                        const int8_t *b,
                                                        int64_t *c);
void runtimec_scalar_probe_cast_int64_int8_int8(const int64_t *a, int8_t *out);
void runtimec_scalar_probe_cast_uint8_int8_int8(const uint8_t *a, int8_t *out);
void runtimec_scalar_probe_cast_int8_uint8_uint8(const int8_t *a,
                                                 uint8_t *out);
void runtimec_scalar_probe_add_float32_int64_float32(const float *a,
                                                     const int64_t *b,
                                                     float *c);
void runtimec_scalar_probe_min_int32_float32_float32(const int32_t *a,
                                                     const float *b,
                                                     float *c);
void runtimec_scalar_probe_mul_float32_uint64_float32(const float *a,
                                                      const uint64_t *b,
                                                      float *c);
void runtimec_scalar_probe_add_int8_int8_int8(const int8_t *a,
                                              const int8_t *b, int8_t *c);
void runtimec_scalar_probe_sub_int8_int8_int8(const int8_t *a,
                                              const int8_t *b, int8_t *c);
void runtimec_scalar_probe_sub_uint8_uint8_uint8(const uint8_t *a,
                                                 const uint8_t *b,
                                                 uint8_t *c);
void runtimec_scalar_probe_add_float32_float32_float32(const float *a,
                                                       const float *b,
                                                       float *c);
void runtimec_scalar_probe_less_int32_int32_bool(const int32_t *a,
                                                 const int32_t *b, bool *c);
void runtimec_scalar_probe_equal_int64_int64_bool(const int64_t *a,
                                                  const int64_t *b, bool *c);
void runtimec_scalar_probe_bitwise_and_uint8_uint8_uint8(const uint8_t *a,
                                                         const uint8_t *b,
                                                         uint8_t *c);
void runtimec_scalar_probe_logical_or_float64_float64_bool(const double *a,
                                                           const double *b,
                                                           bool *c);
void runtimec_scalar_probe_logical_not_int64_bool(const int64_t *a,
                                                  bool *out);
void runtimec_scalar_probe_shift_left_uint8_uint8_uint8(const uint8_t *a,
                                                        const uint8_t *b,
                                                        uint8_t *c);
void runtimec_scalar_probe_shift_right_int8_int8_int8(const int8_t *a,
                                                      const int8_t *b,
                                                      int8_t *c);
void runtimec_scalar_probe_cast_float64_int8_int8(
    const double *a, int8_t *out, binrwir_execution_exit_t *output_exit);
void runtimec_scalar_probe_cast_float32_uint8_uint8(
    const float *a, uint8_t *out, binrwir_execution_exit_t *output_exit);
void runtimec_scalar_probe_div_int16_int16_int16(
    const int16_t *a, const int16_t *b, int16_t *c,
    binrwir_execution_exit_t *output_exit);
void runtimec_scalar_probe_div_int64_int64_int64(
    const int64_t *a, const int64_t *b, int64_t *c,
    binrwir_execution_exit_t *output_exit);
void runtimec_scalar_probe_div_float64_float64_float64(const double *a,
                                                       const double *b,
                                                       double *c);
void runtimec_scalar_probe_mod_int64_int64_int64(
    const int64_t *a, const int64_t *b, int64_t *c,
    binrwir_execution_exit_t *output_exit);
void runtimec_scalar_probe_mod_uint8_uint8_uint8(
    const uint8_t *a, const uint8_t *b, uint8_t *c,
    binrwir_execution_exit_t *output_exit);

#ifdef __cplusplus
}
#endif

#endif
