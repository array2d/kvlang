#ifndef VMBIN_FRAME_DRIVER_H
#define VMBIN_FRAME_DRIVER_H

/*
 * 组件：engine/frame_driver
 *
 * 作用：**调用帧驱动**——`vmbin/interpreter.c` 执行到 `call` 指令就发
 *       `EXIT_CALL` 交回控制权（带 target_identifier + resume pc），
 *       `execution_window` 把它当 running 写进 `‥status` 就返回；**两者都不负责
 *       推进**。本组件是缺的那一环：把 CALL 变成「在新帧里再执行一次」，
 *       并在返回链断裂时如实报错。
 *
 * 为什么它属于运行时而不是测试工具：CALL 帧构造、写参的有效 key 沿调用链传递，
 * 这些是**执行语义**。任何另做一份驱动的人都会再踩一遍这套逻辑里的坑
 * （`frame_path` vs `depth`、`%s` 读越界的长度视图、预建子帧目录与窗口算法
 * 不一致……），而每一个坑的症状都是「算错」而不是「崩」。所以只有这一份。
 *
 * ── 状态归调用方持有 ──────────────────────────────────────────────
 *
 * 本组件**没有文件级可变状态**。驱动需要的全部跨调用状态在 `frame_driver_t`
 * 里，由调用方分配、持有、传入。理由：文件级 static 会让「一个进程里两个驱动
 * 实例」变成共享状态——跑多线程或多 DSN 时症状是串帧，属最难查的一类。
 * 调用方要几个驱动就有几个实例，互不影响。
 *
 * ── 它不做什么 ────────────────────────────────────────────────────
 *
 *   - **不 encode**：`execution_window` 在编译仓库未命中时按
 *     `request->function_path` 调 `vmkv_encode`。驱动只把被调函数的路径放进请求
 *     再发一次 execute——两个编码入口是这条链上最贵的错误。
 *   - **不写 `‥lib`**：`contract/kvspace_access.c` 的 `ensure_frame` 已经在写。
 *   - **不比对输出、不分类**：那是语料运行器的事，不是执行语义。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "binrwir/execution_exit.h"
#include "contract/error.h"
#include "contract/status.h"
#include "engine/execution_window.h"
#include "engine/session.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 被调函数的入口 source pc。
 *
 * **依据是证据链，不是「约定」**：layout 把函数第 i 条指令落成指令槽
 * `/lib/<fn>/[i,0]`，而 `vmkv/rwir.c:438` 的
 * `output_instruction->logical_program_counter = line` 就是从坐标第一段取的。
 * 实测 `add1` 与 `test` 的槽位都是 `[1,0]` `[2,0]` `[3,0]`，故入口 = 1。
 *
 * **不动态计算**：驱动拿不到被调函数的 Program（encode 在 execution_window
 * 内部发生），硬要算就是造第二条路径。 */
#define FRAME_DRIVER_CALLEE_ENTRY_SOURCE_PC 1U

/*
 * `callee_arguments` 的**条目数**上限，不是「形参个数」上限。
 *
 * **写参目标也占一格**：`f(*int64 $a) -> (*int64 $r)` 有 1 个读参 + 1 个写参，
 * 占 2 条。实测 15 形参 + 1 写参 = 16 条刚好装下；16 形参 + 1 写参 = 17 条，
 * 第 17 条（写参）会被挤掉——症状是「加一个参数就突然失败」，而错误码是
 * `0x4008 argument count`，与真实成因（本数组满）无关。
 *
 * 名字里带 `ENTRIES` 就是为了不再让人把这数当形参上限。超了**具名失败**
 * （见 `frame_driver_execute` 里的预检查），不静默丢。
 */
#define FRAME_DRIVER_MAX_CALLEE_ARGUMENT_ENTRIES 16U

/* 写参有效 key 表的容量。表项是 `(帧路径, 写参序号) → 被绑到的 key`，
 * 帧弹出时按帧路径整段清掉；溢出即失败封闭（不静默丢弃——丢了会让某个写参
 * 落回自己的槽，值写到一个没人读的位置）。 */
#define FRAME_DRIVER_EFFECTIVE_KEY_CAPACITY 256U

/* 驱动内部的路径缓冲上限（`/lib/<pkg>·<fn>` 量级）。与语料运行器的同名常量
 * 数值相同但**独立**：驱动不该依赖测试目录里的常量。 */
#define FRAME_DRIVER_PATH_CAPACITY 1024U

/* 一帧的执行结果，够调用方判「继续 / 结束 / 失败」。 */
typedef enum frame_driver_outcome {
    FRAME_DRIVER_OUTCOME_INVALID = 0,
    FRAME_DRIVER_OUTCOME_DONE,
    FRAME_DRIVER_OUTCOME_HANDOFF,
    FRAME_DRIVER_OUTCOME_ERROR,
    FRAME_DRIVER_OUTCOME_EXECUTE_FAILED,
    /* `‥status` 不是继续态（paused 等）：执行停在当前指令之前，位置已由
     * checkpoint 落盘。调用方可以退出，之后用同一个 vid 再进入。 */
    FRAME_DRIVER_OUTCOME_YIELD
} frame_driver_outcome_t;

typedef struct frame_driver_effective_key {
    char frame_path[128];
    uint16_t parameter_slot;
    char key[128];
} frame_driver_effective_key_t;

/*
 * 一个驱动的全部跨调用状态。
 *
 * `session` 是借用（驱动不创建、不销毁它）。其余字段由本组件维护，
 * 调用方只管把它清零后交进来。
 */
typedef struct frame_driver {
    /* 执行窗口的持有者。借用，生命周期须覆盖本驱动的全部调用。 */
    runtimec_session_t *session;
    frame_driver_effective_key_t effective_keys[FRAME_DRIVER_EFFECTIVE_KEY_CAPACITY];
    size_t effective_key_count;
} frame_driver_t;

/* 初始化：清零并把 session 借进来。session 为 NULL 非法。 */
runtimec_status_t frame_driver_initialize(frame_driver_t *driver,
                                          runtimec_session_t *session);

/*
 * 执行 `function_path` 直到 DONE，遇 CALL 就递归进被调帧，遇 HANDOFF / YIELD
 * 原样交回调用方。
 *
 * 参数：
 *   function_path     绝对路径（`/lib/test`）
 *   source_pc         入口 source pc（根帧用调用方给的，被调帧用
 *                     `FRAME_DRIVER_CALLEE_ENTRY_SOURCE_PC`）
 *   depth             帧号（根帧 1，子帧 +1）
 *   frame_segment     帧路径段（`<depth>` 或 `<depth>.<call_site_pc>`）
 *   vthread_identifier vid 文本
 *   arguments         实参描述（可为 NULL，此时 argument_count 须为 0）
 *   argument_count    实参个数
 *   output_exit       最终（或中断处）的结构化出口
 *   output_message    失败时的可读消息缓冲；成功或消息不可用时置空串
 *
 * 返回 outcome；`FRAME_DRIVER_OUTCOME_EXECUTE_FAILED` 时 output_message 里是
 * 窗口给的原话（那是能定位的那句，`detail_code` 只是分类）。
 */
frame_driver_outcome_t frame_driver_execute(
    frame_driver_t *driver, const char *function_path, uint32_t source_pc,
    int32_t depth, const char *frame_segment,
    const char *vthread_identifier,
    const runtimec_argument_description_t *arguments, size_t argument_count,
    binrwir_execution_exit_t *output_exit, char *output_message,
    size_t output_message_capacity);

#ifdef __cplusplus
}
#endif

#endif
