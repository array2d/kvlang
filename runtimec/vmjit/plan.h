#ifndef RUNTIMEC_VMJIT_PLAN_H
#define RUNTIMEC_VMJIT_PLAN_H

/*
 * 组件：vmjit/plan
 * 作用：vmjit 计划：control flow graph、basic block、liveness、寄存器分配、
 *       spill、materialization、safepoint、source program counter、side exit、
 *       callsite maps 与每指令可观察性策略。
 * 联系：仅由 verifier 输出的 verified Program 生成；lower 消费；backend 不直接看
 *       plan 的语义，只消费 emit request。
 * 约束：无法构建 deoptimization 或 source program counter map 时必须拒绝，
 *       不得退化成不精确
 *       side exit。
 */

#include <stdbool.h>
#include <stdint.h>

#include "contract/allocator.h"
#include "contract/status.h"
#include "vmbin/program.h"
#include "vmjit/backend.h"
#include "vmjit/verifier.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vmjit_plan vmjit_plan_t;
typedef struct vmjit_plan_options {
    uint32_t tier;
    uint32_t code_generation_revision;
    bool observe_every_instruction;
} vmjit_plan_options_t;

runtimec_status_t vmjit_plan_create(
    const vmjit_verified_program_t *verified_program,
    const vmjit_target_t *target,
    const vmjit_plan_options_t *options,
    const runtimec_allocator_t *allocator,
    vmjit_plan_t **output_plan,
    vmjit_refusal_t *refusal,
    runtimec_error_t *error);
runtimec_status_t vmjit_plan_destroy(vmjit_plan_t *plan);

#ifdef __cplusplus
}
#endif

#endif
