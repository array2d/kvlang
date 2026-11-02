#ifndef VMBIN_INTERPRETER_H
#define VMBIN_INTERPRETER_H

/*
 * 组件：vmbin/interpreter
 * 作用：定义唯一完整的 BinRWIR 正确性执行入口。
 * 联系：engine route 是唯一调用者；BinRWIR Compiler 可提供只读执行计划。
 * 约束：执行计划可为空；此时仍须执行全部合法 canonical BinRWIR。每个参数
 *       只通过 instruction 的 64 位 argaddr 直接访问 kvspace V.body。VMBIN
 *       不得缓存 V.body，不得拥有 Cell、value slot、register file、spill 或
 *       dirty value cache；写结果落入 V.body 后才能推进逻辑程序计数器。
 */

#include "binrwir/execution_exit.h"
#include "binrwir/execution_frame.h"
#include "binrwir/execution_plan.h"
#include "binrwir/program.h"
#include "builtin/registry.h"
#include "contract/status.h"
#include "profile.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 从 Frame 的当前逻辑程序计数器开始执行，直到产生结构化退出。 */
runtimec_status_t
vmbin_execute(const binrwir_program_t *program,
              const binrwir_execution_plan_t *execution_plan,
              binrwir_execution_frame_t *frame,
              const vmbin_builtin_registry_t *builtin_registry,
              vmbin_profile_t *profile,
              binrwir_execution_exit_t *output_exit);

#ifdef __cplusplus
}
#endif

#endif
