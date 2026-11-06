#ifndef RUNTIMEC_ENGINE_FRAME_STATE_H
#define RUNTIMEC_ENGINE_FRAME_STATE_H

/*
 * 组件：engine/frame_state
 * 作用：**跨多次 `execution_window_execute` 存活的帧状态**——一张
 *       `frame_path → {bindings, function_path, generation}` 表。
 *
 * 为什么需要：窗口原先每次 execute 都 `binrwir_frame_bindings_create` 新建一张
 * 绑定表，用完即销毁。而驱动（含 `kv_runner`）在子帧返回后**对同一 frame_path
 * 再 execute 一次**来 resume 父帧。于是：
 *
 *   ① 父帧指令运行时绑定的局部槽（`s` → `/vthread/1/[1]/s`）在第一次 execute
 *      末尾随表销毁；
 *   ② resume 那次建的是**一张新表**，只重新绑形参；
 *   ③ 父帧后续指令读那个局部槽 ⇒ `UNBOUND_SLOT`（`0x4501`）。
 *
 * **帧状态归 session，不归窗口**：窗口的生命周期是「一次 execute」，帧的生命周期
 * 是「从建立到 DONE/ERROR」，两者不同。让窗口持有帧状态，等于每次 execute 都
 * 重新发明一次「这个帧之前绑过什么」。
 *
 * 联系：`session.c` 持有 `runtimec_frame_state_table_t` 并在
 *       `runtimec_session_destroy` 兜底清理；`execution_window.c` 经
 *       `runtimec_session_get_frame_state_table` 借出它，进入时 `open`、
 *       DONE/ERROR 退出时 `close`。
 * 约束：
 *   - **同一 `frame_path` 只能绑一个函数**：不同函数 ⇒ 失败，不覆盖；
 *   - **释放时机只有一个**：**终态 = `DONE` / `ERROR`，或任何失败状态**。
 *     `CALL`/`HANDOFF`/`YIELD`/`WAIT` 成功返回时一律保留（那正是要 resume 的
 *     情形）；而 `execute` 返回了非 SUCCESS 时**任何 kind 都是终态**——失败已经
 *     写进 error，这一帧不可能再 resume，留着只会让状态表在 session 销毁时
 *     说一句「残留 > 0」（实测 tutorial 语料十几条）；
 *   - **比较值必须与写入值同源**：表里记的 `generation` 与 `open` 时拿来做
 *     REUSED/REBUILT 比较的必须是同一个量。踩过：拿「绑槽那一刻的适配器代次」
 *     写进表、拿「帧目录存储代次」来比，两次必然不等 ⇒ 每次 execute 都 REBUILT，
 *     37 条掉到 23 条。同理 `frame->snapshot_generation` 在 REUSED 时也必须取
 *     表里记下的那一代，不能现取——现取与绑定表里那批不是一次观测；
 *   - 销毁后残留 > 0 是缺陷，不是日志（`session_destroy` 兜底 + 测试断言）。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "binrwir/frame_bindings.h"
#include "contract/allocator.h"
#include "contract/error.h"
#include "contract/status.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

/* `open` 的结果三态。**「是否重绑形参」只由它决定**，不由 `arguments != NULL`
 * 之类的外部条件决定——那会让同一件事有两个判据。 */
typedef enum runtimec_frame_state_result {
    RUNTIMEC_FRAME_STATE_INVALID = 0,
    /* 首次建立：**必须**绑形参、物化局部。 */
    RUNTIMEC_FRAME_STATE_CREATED = 1,
    /* 同路径、同函数、代次未变：**跳过**绑形参与物化。
     * resume 时驱动给的 `arguments` 可能是空的或旧的，重绑会把旧实参盖回来。 */
    RUNTIMEC_FRAME_STATE_REUSED = 2,
    /* 同路径但存储代次变了：旧表作废，**必须**重绑形参、重新物化。 */
    RUNTIMEC_FRAME_STATE_REBUILT = 3
} runtimec_frame_state_result_t;

/*
 * 一帧跨 execute 存活的全部状态。`bindings` 与 `frame_path_text` 由表拥有。
 *
 * `function_path` 是**借用**（指向 Program 内的函数路径）：只在 CREATED/REBUILT
 * 时写入，而那两次的 Program 由本次 execute 的编译租约保持，租约比本函数调用长；
 * 它只用于「同路径不同函数」的判据，不参与任何地址计算。
 */
typedef struct runtimec_frame_state {
    runtimec_key_view_t frame_path;
    char *frame_path_text;
    runtimec_string_view_t function_path;
    binrwir_frame_bindings_t *bindings;
    uint64_t generation;
} runtimec_frame_state_t;

/*
 * 表本体。session 按值持有一个（`session.h` 的访问器借出），不透明字段只有本
 * 组件解释。
 */
typedef struct runtimec_frame_state_table {
    runtimec_allocator_t allocator;
    runtimec_frame_state_t **entries;
    size_t count;
    size_t capacity;
} runtimec_frame_state_table_t;

/* session 创建期初始化：只置空，不分配。 */
void runtimec_frame_state_table_initialize(runtimec_frame_state_table_t *table,
                                           runtimec_allocator_t allocator);

/*
 * 取该帧的状态：有则复用、无则新建。`output_result` 给出三态。
 *
 * `storage_generation` 是本次 execute 观测到的帧存储代次（调用方必须先建帧、
 * 再取代次）：
 *   - 表里没有该路径 ⇒ CREATED，记下代次；
 *   - 同路径同函数同代次 ⇒ REUSED；
 *   - 同路径同函数、代次不同 ⇒ REBUILT，销毁旧表新建。
 *
 * 失败语义：
 *   - 同 `frame_path` 但 `function_path` 不同 ⇒ `INVALID` + 具名消息（**不覆盖**）；
 *   - 分配失败 ⇒ `OUT_OF_MEMORY`，**既有表不受影响**（先分配成功再插入）。
 */
runtimec_status_t runtimec_frame_state_open(
    runtimec_frame_state_table_t *table, runtimec_key_view_t frame_path,
    runtimec_string_view_t function_path, uint64_t storage_generation,
    size_t binding_capacity, runtimec_frame_state_t **output_state,
    runtimec_frame_state_result_t *output_result, runtimec_error_t *error);

/*
 * 结束该帧：`DONE` / `ERROR` 时调用。销毁 bindings、移除表项。
 * 帧本来就不在表里 ⇒ 成功（销毁不是幂等性检查，是「让它不存在」）。
 */
runtimec_status_t runtimec_frame_state_close(
    runtimec_frame_state_table_t *table, runtimec_key_view_t frame_path,
    runtimec_error_t *error);

/* 当前存活的帧数。销毁后必须为 0——测试据此断言泄漏。 */
size_t runtimec_frame_state_count(const runtimec_frame_state_table_t *table);

/* session 销毁时兜底清理全部帧状态（含表自身的条目数组）。 */
void runtimec_frame_state_table_release(runtimec_frame_state_table_t *table);

#ifdef __cplusplus
}
#endif

#endif
