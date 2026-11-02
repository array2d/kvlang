#ifndef RUNTIMEC_VMJIT_H
#define RUNTIMEC_VMJIT_H

/*
 * 组件：vmjit/vmjit
 * 作用：执行已编译机器码。可编译性只在 compiler、verifier 与 backend
 *       阶段决定；执行入口没有拒绝、重编译或替代执行路径。
 * 联系：engine/route 只在 BinRWIR compilation 的不可变执行目标为 VMJIT
 *       时调用本入口；输出为 BinRWIR execution exit，交给 route 提交。
 * 约束：vmjit 不读 RWIR、不访问 kvspace、不依赖 vmbin；非法机器码句柄、
 *       out-of-memory 或内部错误必须直接返回错误。
 */

#include "contract/status.h"
#include "binrwir/execution_exit.h"
#include "binrwir/execution_frame.h"
#include "vmjit/code.h"

#ifdef __cplusplus
extern "C" {
#endif

runtimec_status_t vmjit_execute(
    const vmjit_code_t *code,
    binrwir_execution_frame_t *frame,
    binrwir_execution_exit_t *output_exit,
    runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
