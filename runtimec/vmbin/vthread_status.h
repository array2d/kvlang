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

#include "binrwir/execution_frame.h"
#include "contract/status.h"

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
#define RUNTIMEC_VTHREAD_STATUS_CONTENT_LIMIT 16U
#define RUNTIMEC_VTHREAD_STATUS_CAPACITY 80U
#define RUNTIMEC_VTHREAD_STATUS_LANGTYPE "[16]char/utf8"

/*
 * 取本帧可用的 kvspace 句柄。
 *
 * 返回 `RUNTIMEC_STATUS_NOT_IMPLEMENTED` 表示这个 session 不接存储——那是
 * 「没有能力」，不是「参数错了」；调用方据此失败封闭，不要与 INVALID 混用。
 */
runtimec_status_t runtimec_vthread_status_handle(
    const binrwir_execution_frame_t *frame, void **output_handle);

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

#ifdef __cplusplus
}
#endif

#endif
