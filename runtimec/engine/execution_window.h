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
#include "contract/diagnostic_band.h"
#include "contract/view.h"
#include "engine/session.h"
#include "binrwir/execution_exit.h"
#include "binrwir/program.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum runtimec_execution_window_diagnostic_t {
    RUNTIMEC_EXECUTION_WINDOW_DIAG_INVALID_REQUEST = UINT32_C(0x4000) + 1U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_MISSING_HOST_CAPABILITY = UINT32_C(0x4000) + 2U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_SNAPSHOT_FAILED = UINT32_C(0x4000) + 3U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_REPOSITORY_FAILED = UINT32_C(0x4000) + 4U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_ENCODE_FAILED = UINT32_C(0x4000) + 5U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_COMPILE_FAILED = UINT32_C(0x4000) + 6U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_COMPILATION_INVALID = UINT32_C(0x4000) + 7U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_COUNT = UINT32_C(0x4000) + 8U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_UNRESOLVED = UINT32_C(0x4000) + 9U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_BINDING_FAILED = UINT32_C(0x4000) + 10U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_FRAME_FAILED = UINT32_C(0x4000) + 11U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_PROGRAM_COUNTER_NOT_FOUND = UINT32_C(0x4000) + 12U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_FRAME_ENSURE_FAILED = UINT32_C(0x4000) + 13U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_EXECUTION_FAILED = UINT32_C(0x4000) + 14U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_CHECKPOINT_FAILED = UINT32_C(0x4000) + 15U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_LOCAL_FAILED = UINT32_C(0x4000) + 16U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_OUT_OF_MEMORY = UINT32_C(0x4000) + 17U,
    /* 实参描述的 key 不合法（空、不可解析）。 */
    RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_KEY_INVALID = UINT32_C(0x4000) + 18U,
    /* 字面量描述缺 bytes 或 langtype。 */
    RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_LITERAL_INVALID = UINT32_C(0x4000) + 19U,
    /* 物化本身失败（写不进去、key 放不下）。 */
    RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_MATERIALIZE_FAILED = UINT32_C(0x4000) + 20U,
    /*
     * 特化失败（编码成功之后、编译之前）。
     *
     * **必须与 5（编码失败）分开**：一个码两个成因，看日志的人就只能靠第二个通道
     * （比如「runner 有没有打 encode 的 message」）去分诊，而那个通道一换就失效，
     * 失效时还不报错。诊断码本身要说得出「死在哪一步」。
     */
    RUNTIMEC_EXECUTION_WINDOW_DIAG_SPECIALIZE_FAILED = UINT32_C(0x4000) + 21U
} runtimec_execution_window_diagnostic_t;

/* 段断言：窗口的码必须在自己的段内（见 contract/diagnostic_band.h）。 */
_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(
                   RUNTIMEC_EXECUTION_WINDOW_DIAG_SPECIALIZE_FAILED,
                   RUNTIMEC_DIAGNOSTIC_BAND_EXECUTION_WINDOW),
               "window diagnostic must stay inside its band");

/* 一个实参的来源。判据是「执行窗口要不要创建它」，不是「谁声明的它」。 */
typedef enum runtimec_argument_source {
    RUNTIMEC_ARGUMENT_SOURCE_INVALID = 0,
    /* 调用方已存在的槽：本层直接用，不创建。 */
    RUNTIMEC_ARGUMENT_SOURCE_EXISTING_KEY = 1,
    /* 常量：本层物化到被调帧的 `._lit<i>`（命名归本层，调用方不算这个名字）。 */
    RUNTIMEC_ARGUMENT_SOURCE_LITERAL = 2,
    /* 调用方尚无的写目标：本层经 `ensure_zero_value` 创建。 */
    RUNTIMEC_ARGUMENT_SOURCE_WRITE_TARGET = 3
} runtimec_argument_source_t;

/*
 * 一个实参的描述。
 *
 * `key` 的三种用法按 `source` 解释：
 *   EXISTING_KEY / WRITE_TARGET —— 绝对槽键（`/vthread/...`），本层不拼不猜；
 *   LITERAL                     —— **不用**，常量没有 key。
 *
 * `literal_bytes` / `literal_langtype` 只在 LITERAL 时有值：
 * 前者是已编码的 wire 字节（XValue head + body + attr），后者是该值的具体
 * langtype 文本。两者都由本层借用至本次调用结束，调用方保生存期。
 */
typedef struct runtimec_argument_description {
    runtimec_argument_source_t source;
    uint32_t reserved_32_bits;
    runtimec_string_view_t key;
    runtimec_bytes_view_t literal_bytes;
    runtimec_string_view_t literal_langtype;
} runtimec_argument_description_t;

typedef struct runtimec_execution_window_request_t {
    /* 要编译并执行的函数绝对路径。 */
    runtimec_string_view_t function_path;
    /* checkpoint 的落点：非空。本层不拼路径，只原样交给适配层。 */
    runtimec_string_view_t vthread_identifier;
    /*
     * 帧的绝对路径 `/vthread/{vid}/[{depth}]`：局部槽的父路径。
     *
     * `stack_identifier` 是同一个帧号的**整数形态**，调用方必须两者一致地给出：
     * pc 是 struct（两个 int32），本层不从路径文本里解析帧号——那会让整数 pc
     * 又依赖一次字符串解析。
     */
    runtimec_string_view_t frame_path;
    int32_t stack_identifier;
    /*
     * 起始位置，是 **source logical program counter**（layout 的 irseq），不是
     * 稠密指令标识符：本层用 binrwir_program_find_instruction 换算出执行游标。
     */
    uint32_t source_logical_program_counter;
    /*
     * 实参**描述**，读参在前、写参在后，数量等于被调签名的读参 + 写参数。
     *
     * 早先这里是「已造好的绝对槽键」。那样要求调用方**有能力造出 key**，
     * 而造 key 需要 snapshot 与 `ensure_zero_value`——两者都在本层内部。
     * 让调用方去造，等于逼它抄一份内部状态，绕开来又会破坏 revision
     * （实测症状：`function source revision differs`）。
     *
     * 现在调用方只描述「这是什么」，物化归本层。三种来源恰好覆盖全部实参：
     *   EXISTING_KEY —— 调用方已存在的槽，本层直接用，不创建；
     *   LITERAL      —— 常量，本层物化到被调帧的 `._lit<i>`（**命名归本层**）；
     *   WRITE_TARGET —— 调用方尚无的写目标（`add1(41) -> v` 的 `v`），
     *                   本层经 `ensure_zero_value` 创建。
     * 判据是「本层要不要创建它」，不是「谁声明的它」。
     *
     * **顺序**：先物化全部实参，**再**取快照。反过来的话，物化的写会让地址
     * 重定位、`source_revision` 前进，取到的世代在物化之后就失效了。
     */
    const runtimec_argument_description_t *arguments;
    size_t argument_count;
} runtimec_execution_window_request_t;

typedef struct runtimec_execution_window_result_t {
    runtimec_status_t status;
    binrwir_execution_exit_t execution_exit;
    runtimec_string_view_t return_value;
    /*
     * 本帧实际执行的那份 Program（只读借用，生存期到下一次 execute 或 session 销毁）。
     *
     * **为什么必须借出**：`execution_exit.target_identifier` 是**这份 Program
     * 符号池**的下标。调用方要解析被调函数名，就必须用同一份 Program——
     * 用别的 Program 不会报错，只会拿到不相关的名字，然后以「查不到函数」的
     * 形式失败，离根因很远。
     *
     * 递归时每帧的 Program 不同（`/lib/test` 与 `/lib/fib` 各一份），
     * 所以它是**每帧**的事实，不能存在驱动里当全局。
     */
    const binrwir_program_t *program;
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
