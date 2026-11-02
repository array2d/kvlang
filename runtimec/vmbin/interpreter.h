#ifndef VMBIN_INTERPRETER_H
#define VMBIN_INTERPRETER_H

/*
 * 组件：vmbin/interpreter
 * 作用：定义唯一完整的 BinRWIR 正确性执行入口。
 * 联系：engine route 是唯一调用者；vmjit 拒绝时回到此入口。
 * 约束：generic、typed、fused、continuous 只能是本解释器内部策略。
 */

#include "builtin/registry.h"
#include "contract/status.h"
#include "exit.h"
#include "frame.h"
#include "profile.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 从 Frame 的当前逻辑程序计数器开始执行，直到产生结构化退出。 */
runtimec_status_t
vmbin_execute(vmbin_frame_t *frame,
              const vmbin_builtin_registry_t *builtin_registry,
              vmbin_profile_t *profile, vmbin_exit_t *output_exit);

#ifdef __cplusplus
}
#endif

#endif
