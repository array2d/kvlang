#ifndef RUNTIMEC_BINRWIR_JIT_EXECUTOR_H
#define RUNTIMEC_BINRWIR_JIT_EXECUTOR_H

/*
 * 组件：binrwir_compiler/jit/executor
 * 作用：执行 BinRWIR Compiler 已选定并生成的机器码。可编译性只在 compiler、
 *       verifier 与 backend 阶段决定；执行入口没有拒绝、重编译或替代路径。
 * 联系：engine/route 只在 BinRWIR compilation 的不可变执行目标为 JIT 时调用；
 *       输出为 BinRWIR execution exit，交给 route 提交。
 * 约束：JIT executor 不读 RWIR、不依赖 vmbin。机器码只可按 BinRWIR argaddr
 *       读取或提交 KVspace V.body。每条完整 logical instruction 的 dirty state
 *       必须先 commit，之后才可推进 logical program counter/checkpoint；当前
 *       指令的 speculative/partial state 在 guard failure/error 时必须 discard，
 *       不得写回。recovery/resume 丢弃全部机器状态。非法机器码句柄、commit
 *       失败或内部错误必须直接返回错误，不得改走 VMBIN。
 */

#include "binrwir/execution_exit.h"
#include "binrwir/execution_frame.h"
#include "binrwir_compiler/jit/code.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

runtimec_status_t binrwir_jit_execute(
    const binrwir_jit_code_t *code,
    binrwir_execution_frame_t *frame,
    binrwir_execution_exit_t *output_exit,
    runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
