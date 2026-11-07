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
 * binrwir_binfunc_find_instruction 映射。
 */

#include "binrwir/execution_exit.h"
#include "binrwir/binvthread_frame.h"
#include "binrwir/execution_plan.h"
#include "binrwir/binfunc.h"
#include "binrwir/registry.h"
#include "contract/diagnostic_band.h"
#include "contract/status.h"
#include "profile.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 结构化退出的 detail_code：告诉调用方「为什么失败」。
 *
 * 放头文件而不是 .c：测试必须能引用具体码值做断言。只断言 `detail_code != 0`
 * 时，两种失败共用同一个码，测试照样绿——那就等于没测。
 *
 * 编号只增不改：已发布的码值会出现在 exit.detail_code 里，插中间或复用一个
 * 编号都会让下游的判据错位。
 */
typedef enum vmbin_interpreter_diagnostic {
    VMBIN_INTERPRETER_DIAG_INVALID_REQUEST = UINT32_C(0x4100) + 1U,
    VMBIN_INTERPRETER_DIAG_INVALID_CURSOR = UINT32_C(0x4100) + 2U,
    VMBIN_INTERPRETER_DIAG_INVALID_INSTRUCTION = UINT32_C(0x4100) + 3U,
    VMBIN_INTERPRETER_DIAG_WRONG_ARITY = UINT32_C(0x4100) + 4U,
    VMBIN_INTERPRETER_DIAG_UNDECODABLE_TARGET = UINT32_C(0x4100) + 5U,
    VMBIN_INTERPRETER_DIAG_UNRESOLVED_TARGET = UINT32_C(0x4100) + 6U,
    VMBIN_INTERPRETER_DIAG_VALUE_ACCESS = UINT32_C(0x4100) + 7U,
    VMBIN_INTERPRETER_DIAG_HANDLER_FAILED = UINT32_C(0x4100) + 8U,
    VMBIN_INTERPRETER_DIAG_OUT_OF_MEMORY = UINT32_C(0x4100) + 9U,
    /* 表被绕过 append 写坏（条目 handler 为空）。与 INVALID_INSTRUCTION
     * （表里没有这个 opid）必须分开：前者是 bug，后者是编译期与执行期的表不一致。 */
    VMBIN_INTERPRETER_DIAG_CORRUPT_REGISTRY = UINT32_C(0x4100) + 10U,
    /*
     * 两个通道自相矛盾：结构化退出说失败（kind = ERROR），返回状态却说成功。
     * 这是实现 bug——handler 置了错却报了成功。不做静默对齐，用这条码暴露它，
     * 否则调用方按返回状态判成败，会把一次失败当成成功原样拷贝下去。
     */
    VMBIN_INTERPRETER_DIAG_CONTRADICTORY_EXIT = UINT32_C(0x4100) + 11U
} vmbin_interpreter_diagnostic_t;

/*
 * 段断言：本模块的码必须落在自己的段内。
 *
 * 迁移前五个模块各自从 1 开始编号，同一个 `3` 同时是「解释器的指令非法」与
 * 「println 的值格式非法」。那造成过一整晚的误判——**不是细心能解决的，是
 * 命名空间缺失**。这两条断言把「漏加偏移」变成编译期错误。
 */
_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(VMBIN_INTERPRETER_DIAG_CONTRADICTORY_EXIT, RUNTIMEC_DIAGNOSTIC_BAND_INTERPRETER),
               "VMBIN_INTERPRETER_DIAG_CONTRADICTORY_EXIT must stay inside its diagnostic band");
/*
 * 本枚举**没有** `NONE = 0` 条目：解释器不产生「没有诊断」的出口，
 * `exit.detail_code == 0` 只出现在成功路径（`*output_exit = {0}` 清零后未再写）。
 */


/*
 * 「没有 opid」。值必须与写侧同源——这里曾写成自定字面量 `UINT32_C(0)`，
 * 而 `binrwir` 侧把同一个概念定义成 `0xFFFF`：同一个概念两个数，于是写侧校验
 * 放行 0、读侧执行到才拦，错误离出错点很远。
 *
 * `BINRWIR_OPERATION_IDENTIFIER_NONE` 是唯一定义点；本层不另立字面量。
 */
#define VMBIN_OPERATION_IDENTIFIER_NONE BINRWIR_OPERATION_IDENTIFIER_NONE

/*
 * 从 Frame 的当前逻辑程序计数器开始执行，直到产生结构化退出。
 *
 * `kvspace_handle` 是 `kvspaceConnect` 的原样返回值，写进 Frame 供需要存储的
 * handler 借用。它不在本层被解引用，也不是包装类型——本层不认识存储，只做透传。
 */
runtimec_status_t
vmbin_execute(const binrwir_binfunc_t *program,
              const binrwir_execution_plan_t *execution_plan,
              binrwir_binvthread_frame_t *frame,
              const binrwir_registry_t *builtin_registry,
              void *kvspace_handle,
              vmbin_profile_t *profile,
              binrwir_execution_exit_t *output_exit);

#ifdef __cplusplus
}
#endif

#endif
