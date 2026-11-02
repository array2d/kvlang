#ifndef RUNTIMEC_VMJIT_VERIFIER_H
#define RUNTIMEC_VMJIT_VERIFIER_H

/*
 * 组件：vmjit/verifier
 * 作用：vmjit eligibility 的 fail-closed 判定：先按中立 BinRWIR 合同验证
 *       Program，再判定 operation/backend 支持、先定义后使用、控制流 guard、
 *       寄存器压力、binary interface 限制、side effect、call、builtin、
 *       handoff、observable 边界与 source program counter map 可构建。
 * 联系：vmjit/compiler 在 plan 前调用；拒绝结果以结构化 refusal 返回，
 *       compiler 据此产生 NOT_COMPILABLE。
 * 约束：未知架构或不支持的 operation 返回 RUNTIMEC_STATUS_UNSUPPORTED 并
 *       填写 refusal；非法 BinRWIR 返回 INVALID。顶层 vmjit_compile 只把
 *       UNSUPPORTED 映射为成功的 NOT_COMPILABLE；其它错误不得转成拒绝。
 */

#include <stdint.h>

#include "contract/status.h"
#include "binrwir/program.h"
#include "vmjit/backend.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum vmjit_refusal_reason_t {
    VMJIT_REFUSAL_NONE = 0,
    VMJIT_REFUSAL_DISABLED = 1,
    VMJIT_REFUSAL_UNSUPPORTED_TARGET = 2,
    VMJIT_REFUSAL_UNSUPPORTED_PROGRAM = 3,
    VMJIT_REFUSAL_INVALID_PROGRAM = 4,
    VMJIT_REFUSAL_RESOURCE_LIMIT = 5
} vmjit_refusal_reason_t;
typedef struct vmjit_refusal_t {
    vmjit_refusal_reason_t reason;
    uint32_t logical_program_counter;
    uint32_t detail_code;
} vmjit_refusal_t;

typedef struct vmjit_verified_program_t {
    const binrwir_program_t *program;
} vmjit_verified_program_t;

runtimec_status_t vmjit_verify_program(
    const binrwir_program_t *program,
    const vmjit_target_t *target,
    vmjit_verified_program_t *output_verified_program,
    vmjit_refusal_t *refusal,
    runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
