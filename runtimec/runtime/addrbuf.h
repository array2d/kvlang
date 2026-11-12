#ifndef RUNTIMEC_RUNTIME_ADDRBUF_H
#define RUNTIMEC_RUNTIME_ADDRBUF_H

/*
 * 组件：runtime/addrbuf
 * 作用：vthread 的**地址缓冲**——解析点（帧局部 key / Ptr 目标）的 64 位地址。
 *
 * 它不是调用栈：调用栈在 kvspace 的 `/vthread/<vid>/[d]` 里，这是 runtimec
 * 进程内的一块地址数组。**帧 = 缓冲上的一段区间** `[mark, count)`：调用时记下
 * mark，帧内逐个 push 解析结果；返回时 release(mark)，整段丢弃。递归各一段。
 *
 * 为什么不需要字符串缓冲（keybuf）：Ptr 的路径文本已经由 **kvspace** 付款
 * （`*T` 的 XValue body），key 文本的落点不是 runtimec 的事；runtimec 只保存
 * 解析出来的地址。
 *
 * 作用域（已裁决）：
 *   帧局部解析点（`i`、`_1`、`*[0,1]` 槽本身）→ 本缓冲；
 *   静态/全局 key（anchor=0 的绝对路径）→ 所有帧共享，放独立缓存；
 *   Ptr 的目标（`*p`）→ 不缓存，每次解引用现算。
 *
 * 容量**先定死**（后续再优化成动态/池化）：固定数组 + 越界 abort（p0：不截断、
 * 不兜底）。`index` 一律相对帧基（mark）计数。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 16 层 × 每层 64 个解析点。当前语料 iops 每帧 6 个，留了 10 倍余量。 */
#define RUNTIMEC_ADDRBUF_CAPACITY 1024U

typedef struct runtimec_addrbuf {
    uint32_t count;                  /* 当前顶（已写入的地址数） */
    uint64_t addresses[RUNTIMEC_ADDRBUF_CAPACITY];
} runtimec_addrbuf_t;

/* 清零。 */
void runtimec_addrbuf_initialize(runtimec_addrbuf_t *buffer);

/* 帧基：当前顶。调用时记下它，返回时传给 release。 */
uint32_t runtimec_addrbuf_mark(const runtimec_addrbuf_t *buffer);

/* 压入一个解析结果。满 → abort（p0：不覆盖、不丢弃）。 */
void runtimec_addrbuf_push(runtimec_addrbuf_t *buffer, uint64_t address);

/* 退帧：丢弃 mark 之后的全部地址。mark 大于顶 → abort。 */
void runtimec_addrbuf_release(runtimec_addrbuf_t *buffer, uint32_t mark);

/* 读帧内第 index 个地址（相对 mark）。越界 → abort（不留零值兜底）。 */
uint64_t runtimec_addrbuf_address(const runtimec_addrbuf_t *buffer,
                                  uint32_t mark, uint32_t index);

#ifdef __cplusplus
}
#endif

#endif
