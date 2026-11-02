#ifndef RUNTIMEC_VMJIT_VERIFIER_H
#define RUNTIMEC_VMJIT_VERIFIER_H

/*
 * 组件：vmjit/verifier
 * 作用：vmjit eligibility 的 fail-closed 判定：先要求 vmbin 确认 Program
 *       有效，再判定 operation/backend 支持、先定义后使用、控制流 guard、
 *       寄存器压力、binary interface 限制、side effect、call、builtin、
 *       handoff、observable 边界与 source program counter map 可构建。
 * 联系：vmjit/compiler 在 plan 前调用；拒绝结果以结构化 refusal 返回，
 *       route 据此回退 vmbin。
 * 约束：拒绝是正常控制流，不是运行时错误；不得“尽量执行”。
 */

#include <stdint.h>

#include "contract/status.h"
#include "vmbin/program.h"
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
    const vmbin_program_t *program;
} vmjit_verified_program_t;

runtimec_status_t vmjit_verify_program(
    const vmbin_program_t *program,
    const vmjit_target_t *target,
    vmjit_verified_program_t *output_verified_program,
    vmjit_refusal_t *refusal,
    runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
