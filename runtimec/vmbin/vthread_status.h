#ifndef VMBIN_VTHREAD_STATUS_H
#define VMBIN_VTHREAD_STATUS_H

/*
 * 组件：vmbin/vthread_status
 * 作用：写 `/vthread/{vid}/‥status` 这一件事的唯一实现。
 * 联系：`vmbin/handlers/host.c` 的 `vthread·setstatus` 用它；
 *       `debugger()`（coding5 的 `scalar.c`）语义上等价于
 *       `vthread·setstatus("paused")`，是同一件事的第二处使用——也用它。
 * 约束：
 *   - 只经 `frame->kvspace_handle` 直调 kvspace C ABI，不封装、不转发；
 *   - 键的形状由 spec 钉死（见下），本组件是它唯一的编码点。
 */

#include <stddef.h>
#include <stdint.h>

#include "binrwir/binvthread_frame.h"
#include "contract/error.h"
#include "contract/status.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 键的形状，来自 spec `02-kvspace模型/04-CABI.kv`（normative）：
 * storetype 固定 SLACK、headlen 恒 8、合法内容长度上限 16、物理容量下限 80；
 * 写入必须原地完成，长度超限或形状不符必须硬错——不得 realloc、不得截断、
 * 不得换 box。
 *
 * 数字是规范定的，不是这里定的。改它们要改 spec。
 */
/*
 * status 槽的 headlen。**用上游的 XValue 头长度，不写 8。**
 *
 * spec `02-kvspace模型/04-CABI.kv:24` 写「固定头 8 字节，headlen 恒 8」，
 * 但同一节 `:26` 又说「实际 `headlen`/`body_cap` 以 slot view 返回为准」，
 * 而 shm 后端 slot view 返回的是 `KVSPACE_XVALUE_HEAD_LEN`（**16**）——
 * 同一文件里三处不一致。整棵树其它地方（`03-类型系统/16-ptr.kv`「固定 16 字节 head」）
 * 与后端实现都是 16，所以 `:24` 的 8 是漏改的过期数字。
 *
 * 这里跟后端与树的主流走：直接用上游常量，**不复制数字**——复制了就会在
 * 上游改动时静默错。
 */
#define RUNTIMEC_VTHREAD_STATUS_HEAD_LENGTH KVSPACE_XVALUE_HEAD_LEN
#define RUNTIMEC_VTHREAD_STATUS_CONTENT_LIMIT 16U
#define RUNTIMEC_VTHREAD_STATUS_CAPACITY 80U
/* 变长文本：上限由 CONTENT_LIMIT 管，不由类型形状表达（形状 16 会与短内容矛盾）。 */
#define RUNTIMEC_VTHREAD_STATUS_LANGTYPE "[]char/utf8"

/*
 * 取本帧可用的 kvspace 句柄。
 *
 * 返回 `RUNTIMEC_STATUS_NOT_IMPLEMENTED` 表示这个 session 不接存储——那是
 * 「没有能力」，不是「参数错了」；调用方据此失败封闭，不要与 INVALID 混用。
 */
runtimec_status_t runtimec_vthread_status_handle(
    const binrwir_binvthread_frame_t *frame, void **output_handle);

/*
 * 由帧路径拼出系统变量键。
 *
 * `suffix` 是 `/‥status`、`/‥pc` 这类**带前导斜杠**的后缀；帧路径取到
 * `/vthread/{vid}` 为止（第二个斜杠之前），帧内坐标 `[d]` 不参与。
 * 形如 `/vthread/{vid}/...` 之外的路径返回 INVALID——不猜。
 */
runtimec_status_t runtimec_vthread_status_key(
    runtimec_key_view_t frame_path, runtimec_string_view_t suffix, char *buffer,
    size_t capacity);

/*
 * 把状态文本写进 `status_key`。
 *
 * 超过内容上限返回 INVALID（不截断）；形状不符由后端硬错。
 * 这是本文件与 `vthread·setstatus` 共用的唯一写入点。
 */
runtimec_status_t runtimec_vthread_status_write(
    void *kvspace_handle, const char *status_key, runtimec_bytes_view_t status);

/*
 * 读 `status_key` 的值，按 spec 形状校验后给出**借用**的内容视图。
 *
 * `output_text` 指向后端存储（shm mmap / durable 读缓存），在该 key 被写、被删
 * 或句柄关闭之前有效；本函数不分配、调用方不得释放。
 *
 * ── 为什么要单独一条读入口 ──────────────────────────────────────────────
 *
 * 状态是**控制信号**，解释器要靠它判「继续还是停」。所以两件事必须能分开：
 *
 *   空状态（`len == 0`）  → **成功**，返回空视图。它表示「还没写状态」，
 *                          不是错误，更不是「没有这个键」。
 *   形状不符            → INVALID + error。storetype ≠ SLACK、headlen ≠ 8、
 *                          内容 > 16、物理容量 < 80 都属于这一类。
 *
 * 用同一个返回值表达这两种情况，调用方就只能靠哨兵去猜——而 `‥status` 的合法
 * 内容里任何字符串都可能是真状态（`"run"`、`"done"`、`"error"`…），没有任何
 * 一个串能当代替「坏槽」的哨兵。所以这里用**状态码**分开，不用哨兵值。
 *
 * ── volatile ────────────────────────────────────────────────────────────
 *
 * `‥status` 是 volatile key（spec `02-kvspace模型/04-CABI.kv`）：`kvspaceGet`
 * 对它**每次重读后端**，不走快照。所以外部暂停/恢复/终止对本进程立即可见——
 * 这正是解释器每步都该重读、而不能缓存的原因。
 *
 * 键由调用方给（与 `write` 同形），本函数**不拼键**。
 */
runtimec_status_t runtimec_vthread_status_read(
    void *kvspace_handle, const char *status_key,
    runtimec_string_view_t *output_text, runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
