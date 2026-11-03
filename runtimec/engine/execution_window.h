#ifndef RUNTIMEC_ENGINE_EXECUTION_WINDOW_H
#define RUNTIMEC_ENGINE_EXECUTION_WINDOW_H

/*
 * 组件：engine/execution_window
 * 作用：执行窗口：为一个 vthread 的一个窗口做执行前准备并执行——取权威快照
 *       → 查 compilation repository → 未命中则 vmkv 等价编码 canonical
 *       BinRWIR → BinRWIR Compiler 编译 → 读已确定的
 *       binrwir_execution_target_t → 绑定顶层帧形参 → 单次分派（VMBIN 或 JIT）
 *       → 提交完成边界的 checkpoint。
 * 联系：**调度不在本层**。「下一条指令归谁执行、某条 opcode 属于哪个 runtime」
 *       是 kvspace 调度结构与外部调度者的职责；本层只回答「我执行到哪」。
 *       只有本文件同时包含 vmkv、vmbin 与 BinRWIR JIT；host port 只经本层提交
 *       durable effect；公共接口层把请求翻译成本层请求。
 * 约束：NOT_COMPILABLE 是已完成的编译判定并选择 VMBIN；ERROR 直接失败。执行
 *       期不重新编译、不重新解析地址、不重新选择目标，所选入口失败后也不切换
 *       至另一执行器。请求只有一种形状：函数 + vthread + 帧路径 + 起始 source
 *       pc。其它文件不得绕过本层自行选择执行器或提交 checkpoint。
 */

#include <stddef.h>
#include <stdint.h>
#include "contract/status.h"
#include "contract/view.h"
#include "engine/session.h"
#include "binrwir/execution_exit.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct runtimec_execution_window_request_t {
    /* 要编译并执行的函数绝对路径。 */
    runtimec_string_view_t function_path;
    /* checkpoint 的落点：非空。本层不拼路径，只原样交给适配层。 */
    runtimec_string_view_t vthread_identifier;
    /*
     * 帧的绝对路径 `/vthread/{vid}/[{depth}]`：局部槽的父路径。depth 由调用方
     * 给出，本层不推导——它没有父帧链的事实源。
     */
    runtimec_string_view_t frame_path;
    /*
     * 起始位置，是 **source logical program counter**（layout 的 irseq），不是
     * 稠密指令标识符：本层用 binrwir_program_find_instruction 换算出执行游标。
     */
    uint32_t source_logical_program_counter;
    /*
     * 绝对槽键，读参在前、写参在后，数量等于被调签名的读参 + 写参数。
     * 本层不解包、不拼路径、不猜 vid/depth：裸名与相对名一律失败封闭。
     */
    const runtimec_string_view_t *arguments;
    size_t argument_count;
} runtimec_execution_window_request_t;

typedef struct runtimec_execution_window_result_t {
    runtimec_status_t status;
    binrwir_execution_exit_t execution_exit;
    runtimec_string_view_t return_value;
} runtimec_execution_window_result_t;

runtimec_status_t runtimec_execution_window_execute(
    runtimec_session_t *session,
    const runtimec_execution_window_request_t *request,
    runtimec_execution_window_result_t *output_result,
    runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
