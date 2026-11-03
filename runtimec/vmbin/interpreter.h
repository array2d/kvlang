#ifndef VMBIN_INTERPRETER_H
#define VMBIN_INTERPRETER_H

/*
 * 组件：vmbin/interpreter
 * 作用：定义唯一完整的 BinRWIR 正确性执行入口。
 * 联系：engine route 是唯一调用者；BinRWIR Compiler 可提供只读执行计划。
 * 约束：阶段 1 以 shm 为准，按地址直接读写值体：STATIC 的 payload 就是值体地址，
 *       其余经帧绑定取 XValue 基址再用合同的 runtimec_xvalue_body_address 推导，
 *       LITERAL 取 Program 池内字节。绑定表由 route 在进入本函数之前建好，vmbin
 *       不需要 host。执行计划可为空，plan 不参与取指。不得缓存 V.body，不得拥有
 *       Cell、value slot、register file、spill 或 dirty value cache；写落进值体
 *       后才能推进逻辑程序计数器。
 *
 * 游标：frame->logical_program_counter 是 Program 内的**指令标识符**（稠密序号）；
 * 结构化退出里的 logical_program_counter / resume_logical_program_counter 是
 * **source logical program counter**（layout 的 irseq，可持久化）。两者之间用
 * binrwir_program_find_instruction 映射。
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
