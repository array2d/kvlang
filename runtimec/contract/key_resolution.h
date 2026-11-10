#ifndef RUNTIMEC_CONTRACT_KEY_RESOLUTION_H
#define RUNTIMEC_CONTRACT_KEY_RESOLUTION_H

/*
 * 组件：contract/key_resolution
 * 作用：`list` 的 **resolve=1** 语义。存储层第二阶段的 `kvspaceList` 明确「不做 Ptr
 *       解析」，而旧 `kvspaceListLen`/`kvspaceListAt` 的 `resolve=1` 是后端在列举
 *       内部做的。上移之后，这一步必须由调用方补上，且只能有一份实现。
 *
 * 后端原本怎么做的（`kvspace-c/src/kvspace.c:resolve_fetch`，逐行对照）：
 *   ```
 *   out = path
 *   for (depth = 0; depth < 16; depth++):
 *       h = art_search(out);  if (!h) return h          // 键不存在 → 用当前 out
 *       if (引用种类 != PTR)  return h                   // 不是指针 → 用当前 out
 *       out = body(out)                                 // 是指针 → body 就是目标路径
 *   return NULL                                        // 超过 16 跳 → 失败
 *   ```
 *   关键三点：**只跟 PTR**（`@` 扩展引用不跟）、**跳数上限 16**、**键不存在时用当前
 *   路径而不是失败**。
 *
 * 本模块的语义一句话：**跟随 Ptr 链直到非 Ptr 或 16 跳上限**（不是「跟一跳」）。
 * Ptr 的 body 本身可能又是一个 Ptr，所以必须循环；单步跟会在链中段停下，把仍是
 * 指针的路径当目录去列举，得到空列表而不报错——正是「以奇怪的方式成功」。
 *
 * 联系：不依赖 kvspace 任何符号。读值这一动作由调用方以函数指针传入
 *       （`runtimec_key_reader_t`），于是本模块在旧 ABI（`kvspaceGet`）与新 ABI
 *       （`kvspaceResolveOffset` + base+offset）上都能跑，读写路径切换时**不用改本文件**。
 * 约束：
 *   - **判据是 langtype 首字符 `*`**，不是 head 里的某个位——head 早已没有 ptr 字段。
 *   - 不做 `@`（扩展）展开：后端也不做，这里多做一个就会与旧行为分叉。
 *   - 跳数上限 16 与后端同值；超限是**错误**，不是「用最后那个路径」。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 与后端 `resolve_fetch` 的 16 跳上限同值。改这里必须同时问一句「后端改了吗」。 */
#define RUNTIMEC_KEY_RESOLUTION_HOP_LIMIT 16U

/*
 * 读一个 key 的值。成功时 `output_langtype` 指向该值的 langtype 文本（借用，
 * 生命周期由 reader 保证到下一次调用或本函数返回为止），`output_body` 是 body。
 *
 * 键不存在与「读到了但为空」必须区分：前者返回 `RUNTIMEC_STATUS_INVALID`
 * （对应后端 `art_search` 返回空），后者返回 SUCCESS + 空的 body。
 */
typedef runtimec_status_t (*runtimec_key_reader_t)(
    void *context, const char *key, const char **output_langtype,
    size_t *output_langtype_size, const char **output_body,
    size_t *output_body_size);

/*
 * 把 `key` 解析到它最终指向的目录前缀：跟随 Ptr 链直到非 Ptr 或 16 跳上限。
 *
 * `output_key` 由调用方提供（容量 `output_capacity`，含结尾 NUL）。返回时写入
 * 解析结果：非 Ptr 就是 `key` 本身；Ptr 则逐跳跟到目标。
 *
 * 失败语义（与后端一致，不是「尽力而为」）：
 *   - 超过 16 跳 → `RUNTIMEC_STATUS_ERROR`（后端此处返回 NULL，列举随即失败）；
 *   - 目标路径放不进 `output_capacity` → `RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY`；
 *   - reader 报「读值失败」（非「键不存在」）→ 透传它的状态；
 *   - 中间某一跳键不存在 → **成功**，`output_key` 是那一跳的路径（后端同语义：
 *     列举一个不存在的路径得到空列表，不是错误）。
 */
runtimec_status_t runtimec_key_resolve_prefix(
    const runtimec_key_reader_t reader, void *reader_context, const char *key,
    char *output_key, size_t output_capacity);

#ifdef __cplusplus
}
#endif

#endif
