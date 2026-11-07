#ifndef VMBIN_HANDLERS_HOST_H
#define VMBIN_HANDLERS_HOST_H

/*
 * 组件：vmbin/handlers/host
 * 作用：host 服务算子（kvspace / vthread / random 共 21 条）的 handler 表。
 * 联系：由 engine/session 的装配流程取走条目并装进 `binrwir_registry_t`；
 *       存储经 `frame->kvspace_handle` 直接调 kvspace C ABI——**没有封装层、
 *       没有转发函数**（用户裁决：不对 kvspace ABI 二次封装）。
 * 约束：
 *   - 每个参数是**一个具体类型**，不写 union 分支：handler 拿到什么类型就按那个
 *     类型处理，类型不符即失败封闭。
 *   - 本文件只做「读参 → ABI 调用 → 写参」的直译，不做成员路径推导、不做 Ptr
 *     解引用、不做容器下钻——那些是 layout/vmkv 在编码期就该定下来的事。
 *   - 条目数组是静态存储，生命周期是整个进程；调用方不得释放。
 */

#include <stddef.h>

#include "binrwir/registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 返回本模块的静态条目数组，并把条目数写入 output_count。
 * output_count 为 NULL 时返回 NULL（调用方错用，不猜）。
 */
const binrwir_registry_entry_t *runtimec_host_handler_entries(
    size_t *output_count);

#ifdef __cplusplus
}
#endif

#endif
