#ifndef VMBIN_HANDLERS_SCALAR_H
#define VMBIN_HANDLERS_SCALAR_H

/*
 * 组件：vmbin/handlers/scalar
 * 作用：标量值算子的 handler 表——加减小乘除、取余、位运算。
 * 联系：由 engine/session 的 builtin 装配流程取走条目并装进 `binrwir_registry_t`；
 *       值经 `vmbin/value_access` 按地址读写，与 core 规则共用同一条数据面。
 * 约束：条目数组是静态存储，生命周期是整个进程；调用方不得释放。
 *       落点不是 `vmbin/builtin/`——`builtin` 是被判错的归属词：handler 是实现
 *       （执行侧），归 vmbin；registry 契约归 binrwir。
 */

#include <stddef.h>
#include <stdint.h>

#include "binrwir/execution_exit.h"
#include "contract/diagnostic_band.h"
#include "binrwir/registry.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 本模块的失败细节码。**编号只增不改**：已发出的码会出现在 `exit.detail_code`。
 *
 * 一个码一个成因。「整数除零」与「INT64_MIN / -1 溢出」是两件事——前者是除数
 * 不合法，后者是商不可表示——共用一个码，读日志的人就分不出该改被除数还是除数。
 *
 * 段位：`container` 占 0x43xx，`handler_call` 占 0x4Axx，`handler_call_text` 占
 * 0x4Bxx，本模块占 0x4Cxx。
 */
typedef enum runtimec_scalar_diagnostic {
    RUNTIMEC_SCALAR_DIAG_NONE = 0,
    /* 整数除法 / 取余的除数为零。 */
    RUNTIMEC_SCALAR_DIAG_DIVIDE_BY_ZERO = 0x4C01,
    /*
     * 商不可表示：`INT64_MIN / -1`（以及等宽的 int8/16/32 最小值 / -1）。
     *
     * C 在这里是未定义行为，x86 上直接 SIGFPE——不是「值错了」，是进程没了。
     * 与除零分开是因为**修法不同**：除零要拒除数，这一条要拒那一对具体取值。
     */
    RUNTIMEC_SCALAR_DIAG_DIVIDE_OVERFLOW = 0x4C02,
    /* 浮点转整数时遇到 NaN（matrix:21 要求报错而不是产出数值）。 */
    RUNTIMEC_SCALAR_DIAG_NOT_A_NUMBER = 0x4C03,
    /* 操作数不是本算子接受的数值种类。 */
    RUNTIMEC_SCALAR_DIAG_OPERAND_NOT_NUMERIC = 0x4C04,
    /* 参数项定位失败（未绑定、地址不可逆）——字符族的变长路径也要这个码。 */
    RUNTIMEC_SCALAR_DIAG_LOCATE_FAILED = 0x4C05,
    /* 写参是只读位置（字面量）。 */
    RUNTIMEC_SCALAR_DIAG_READ_ONLY_TARGET = 0x4C06,
    /* 写参槽长度与结果长度不符（变长结果要求槽恰好等长）。 */
    RUNTIMEC_SCALAR_DIAG_LENGTH_MISMATCH = 0x4C07,
    /* 字符载荷不是该编码的合法序列（matrix:64 / matrix:65 的负向判据）。 */
    RUNTIMEC_SCALAR_DIAG_MALFORMED_CHARACTER = 0x4C08,
    /* 值不是合法 Unicode 标量（> U+10FFFF 或落在代理区）。 */
    RUNTIMEC_SCALAR_DIAG_NOT_A_SCALAR_VALUE = 0x4C09
} runtimec_scalar_diagnostic_t;

/* 段断言：本模块的码必须落在自己的段内（见 contract/diagnostic_band.h）。 */
_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(RUNTIMEC_SCALAR_DIAG_NOT_A_SCALAR_VALUE, RUNTIMEC_DIAGNOSTIC_BAND_SCALAR),
               "RUNTIMEC_SCALAR_DIAG_NOT_A_SCALAR_VALUE must stay inside its diagnostic band");


/*
 * handler 级失败原语：只写结构化出口，**不返回状态**。
 *
 * 成功路径没有 return，错误路径也只是一次赋值——这是用户裁决的形态
 * （「错误直接调运行时错误原语置 exit.status = ERROR」）。
 *
 * ── 为什么状态恒为 INVALID，而不是 ERROR ─────────────────────────────
 *
 * 两个状态的区别是**谁坏了**：
 *
 *   INVALID  调用方给的输入不合法——操作数不在本算子的接受集合内、除数为零、
 *            那一对取值（最小值 / -1）不可表示。程序本身没坏，改输入就能跑通。
 *   ERROR    运行时自身出了问题——存储故障、地址无效、内部不一致。
 *
 * 本族全部落在 INVALID，因为每一个成因都是**操作数的性质**：除零是除数不合法，
 * 溢出是那一对取值让商不可表示，NaN 是一个不能转成整数的值。没有一条是「运行时
 * 坏了」。这与 `contract/error.h` 的分域一致——把操作数问题报成 ERROR，会让人去
 * 查存储层，而那里什么错都没有。
 *
 * 这不是随手选的默认值；改它之前先回答「这个成因里，运行时的哪一部分坏了」。
 *
 * `logical_program_counter` 由 thunk 在调用逻辑函数后统一补齐：handler 拿不到
 * 指令，也不该为它传参。
 */
static inline void runtimec_scalar_fail(binrwir_execution_exit_t *output_exit,
                                        uint32_t detail_code) {
    output_exit->kind = BINRWIR_EXECUTION_EXIT_ERROR;
    output_exit->status = RUNTIMEC_STATUS_INVALID;
    output_exit->detail_code = detail_code;
}

/*
 * 返回本模块的静态条目数组，并把条目数写入 output_count。
 * output_count 为 NULL 时返回 NULL（调用方错用，不猜）。
 */
const binrwir_registry_entry_t *runtimec_scalar_handler_entries(
    size_t *output_count);

#ifdef __cplusplus
}
#endif

#endif
