#ifndef VMBIN_HANDLERS_SCALAR_SPECIAL_H
#define VMBIN_HANDLERS_SCALAR_SPECIAL_H

/*
 * 组件：vmbin/handlers/scalar_special
 * 作用：模板表达不了的标量语义——除零、溢出、NaN、饱和、回绕。
 * 联系：`scalar_generated.c` 由生成器产出「一次运算」；本文件是它的对立面，
 *       每一条都带一个语义判据，所以手写且可读。
 * 约束：
 *   - **会失败的 helper 直接收 `exit *`**，成功才写输出槽；不返回哨兵、不返回
 *     status。哨兵一旦落进输出槽，错误就伪装成值——这是本仓已经吃过一次亏的形态；
 *   - 失败时输出槽**一个字节都不动**（不写 0、不写 max），调用方只看 exit；
 *   - 纯函数，不碰存储、不分配。
 *
 * 函数名带 `runtimec_scalar_` 前缀而非 `static inline`：它们由逻辑函数一行调用，
 * 且要有单元测试，所以必须有外部链接。
 */

#include <stdbool.h>
#include <stdint.h>

#include "binrwir/execution_exit.h"
#include "vmbin/handlers/scalar.h"

#ifdef __cplusplus
extern "C" {
#endif

/* matrix:69 floating-min-max-nan —— NaN 让位于数，两个 NaN 得 NaN。 */
double runtimec_scalar_float_min(double left, double right);
double runtimec_scalar_float_max(double left, double right);

/*
 * 整数除法与取余的失败态。`width` 是操作数位宽（8/16/32/64）。
 * 失败原因分两个码：除数为零、最小值除 -1 溢出。
 */
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

/* matrix:21 floating-to-integer-truncation —— 截断、饱和、NaN 报错。 */
void runtimec_scalar_float_to_signed(double source, uint32_t width,
                                     int64_t *output_value,
                                     binrwir_execution_exit_t *output_exit);
void runtimec_scalar_float_to_unsigned(double source, uint32_t width,
                                       uint64_t *output_value,
                                       binrwir_execution_exit_t *output_exit);

/* matrix:63 cast-target-kind —— 整数窄化是回绕，不是饱和。 */
int64_t runtimec_scalar_wrap_signed(int64_t source, uint32_t source_width,
                                    uint32_t target_width);
uint64_t runtimec_scalar_wrap_unsigned(uint64_t source,
                                       uint32_t target_width);

/*
 * matrix:28 mixed-signed-unsigned-arithmetic —— 跨符号比较按**精确数学值**。
 * 返回 -1 / 0 / 1。
 *
 * 不要改写成「两边都转 int64」：`uint64` 超过 `INT64_MAX` 时转负，结论会反，
 * 而且只在跨过 2^63 的值上暴露。实现里写的是「先判符号、再比值」。
 */
int runtimec_scalar_compare_signed_unsigned(int64_t signed_value,
                                            uint64_t unsigned_value);
int runtimec_scalar_compare_unsigned_signed(uint64_t unsigned_value,
                                            int64_t signed_value);

/*
 * matrix:71 shift-count-masking —— 计数按宽度减一掩码；有符号右移是算术移位，
 * 无符号是逻辑移位；结果种类等于操作数种类。
 *
 * `width` 是**操作数位宽**（8/16/32/64），不是结果宽度：移位不改变种类。
 * 四条分开是因为「算术 / 逻辑」与「有符号 / 无符号」是两件事，合并成一条会让
 * 函数体里出现按符号性的分支。
 */
int64_t runtimec_scalar_shift_left_signed(int64_t source, int64_t count,
                                          uint32_t width);
uint64_t runtimec_scalar_shift_left_unsigned(uint64_t source, uint64_t count,
                                             uint32_t width);
int64_t runtimec_scalar_shift_right_signed(int64_t source, int64_t count,
                                           uint32_t width);
uint64_t runtimec_scalar_shift_right_unsigned(uint64_t source, uint64_t count,
                                              uint32_t width);

/*
 * matrix:35 bool-cast-strict —— 目标 bool 只接受 bool 操作数，其余一律失败。
 *
 * 与 matrix:78（逻辑算子「数值非零即真」）**方向相反**，这是两族必须分开的
 * 原因：`and`/`or` 接受数值，`bool(x)` 不接受；转数值要写成显式比较。
 */
void runtimec_scalar_cast_to_bool(bool source, bool *output_value,
                                  binrwir_execution_exit_t *output_exit);
void runtimec_scalar_cast_to_bool_invalid(binrwir_execution_exit_t *output_exit);

#ifdef __cplusplus
}
#endif

#endif
