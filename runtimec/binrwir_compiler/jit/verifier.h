#ifndef RUNTIMEC_BINRWIR_JIT_VERIFIER_H
#define RUNTIMEC_BINRWIR_JIT_VERIFIER_H

/*
 * 组件：binrwir_compiler/jit/verifier
 * 作用：binrwir_jit eligibility 的 fail-closed 判定：先按中立 BinRWIR 合同验证
 *       Program，再判定 operation/backend 支持、所有参数具有非零 V.body 地址、
 *       控制流 guard、binary interface 限制、side effect、call、builtin、
 *       handoff、每条 instruction completion、observable boundary 与 source
 *       program counter map 可构建。guard failure/error 必须可区分未完成状态。
 *       寄存器压力只属于后续 register_allocation，不得进入中立 BinRWIR 合同。
 * 联系：binrwir_compiler/jit/compiler 在 plan 前调用；拒绝结果以结构化 refusal 返回，
 *       compiler 据此产生 NOT_COMPILABLE。
 * 约束：未知架构或不支持的 operation 返回 RUNTIMEC_STATUS_UNSUPPORTED 并
 *       填写 refusal；非法 BinRWIR 返回 INVALID。顶层 binrwir_jit_compile 只把
 *       UNSUPPORTED 映射为成功的 NOT_COMPILABLE；其它错误不得转成拒绝。
 */

#include <stdint.h>

#include "contract/status.h"
#include "binrwir/binfunc.h"
#include "binrwir_compiler/jit/backend.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum binrwir_jit_refusal_reason_t {
    BINRWIR_JIT_REFUSAL_NONE = 0,
    BINRWIR_JIT_REFUSAL_DISABLED = 1,
    BINRWIR_JIT_REFUSAL_UNSUPPORTED_TARGET = 2,
    BINRWIR_JIT_REFUSAL_UNSUPPORTED_PROGRAM = 3,
    BINRWIR_JIT_REFUSAL_INVALID_PROGRAM = 4,
    BINRWIR_JIT_REFUSAL_RESOURCE_LIMIT = 5
} binrwir_jit_refusal_reason_t;
typedef struct binrwir_jit_refusal_t {
    binrwir_jit_refusal_reason_t reason;
    uint32_t logical_program_counter;
    uint32_t detail_code;
} binrwir_jit_refusal_t;

typedef struct binrwir_jit_verified_program_t {
    const binrwir_binfunc_t *program;
} binrwir_jit_verified_program_t;

runtimec_status_t binrwir_jit_verify_program(
    const binrwir_binfunc_t *program,
    const binrwir_jit_target_t *target,
    binrwir_jit_verified_program_t *output_verified_program,
    binrwir_jit_refusal_t *refusal,
    runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
