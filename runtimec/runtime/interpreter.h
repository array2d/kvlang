#ifndef RUNTIMEC_RUNTIME_INTERPRETER_H
#define RUNTIMEC_RUNTIME_INTERPRETER_H

/*
 * 组件：runtime/interpreter
 * 作用：解释器——runtimec 的执行大循环。
 *
 * 形态只有一种：**被上层 runtime 当作函数调用**。给一个 vthread，从它的
 * `‥pc` 开始逐条执行，遇不属于本 runtime 的 rwir 就返回，把控制权交回上层。
 * 位置、状态、错误消息全部在 kvspace 里，由 vthread 的三个指针直接读写；
 * 解释器不携带第二份状态，也不返回错误码。
 *
 * 对外 ABI 见 `include/kvlang/runtimec.h`：
 *   `kvlang_runtime_create`  → `runtimec_runtime_create`
 *   `kvlang_runtime_execute` → `runtimec_interpreter_run`
 */

#include <stdint.h>

#include "runtime/addrbuf.h"
#include "vthread/vthread.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 运行时：持有 kvspace 句柄与地址缓冲。句柄不解释，只透传；addrbuf 是帧局部
 * 解析结果的落点（帧 = 缓冲上 [mark, count) 一段）。 */
typedef struct runtimec_runtime {
    void *kvspace_handler;
    runtimec_addrbuf_t addrbuf;
} runtimec_runtime_t;

/* 建一个运行时。句柄为空是调用方的错，直接终止（p0）。 */
void runtimec_runtime_create(runtimec_runtime_t *runtime,
                             void *kvspace_handler);

/*
 * 解释器主循环。
 *
 * 输入只有 `vthread`：它自带 vid、`‥pc`/`‥status`/`‥msg` 三处进程内地址。
 * 返回即交出控制权——结束（`‥status = done/error`）或撞上外部 rwir
 * （`‥status = wait`）。上层处理完用同一个 vid 再进一次，从 `‥pc` 续跑。
 */
void runtimec_interpreter_run(runtimec_runtime_t *runtime,
                              const char *vthread_identifier);

#ifdef __cplusplus
}
#endif

#endif
