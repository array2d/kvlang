/*
 * 组件：binrwir/key
 * 作用：key 组的 handler。尚未实现——调用即终止，不留静默成功路径。
 *
 * 动态 key（key·resolve）的落法——决策 B（2026-11-12）：
 *   一条指令，handler 内部三段 memcpy；不在 v9 里展开成多条指令。
 *   空间由 Ptr 槽付：一次 kvspace·realloc 到 len(pre)+len(mid)+len(end)，
 *   再按偏移写三段，不产生中间串；mid 由 key·tostr 产出（32 字节栈缓冲）。
 */

#include <stdlib.h>

#include "binrwir/handler.h"

void runtimec_binrwir_key_execute(struct runtimec_runtime *runtime,
                                  const binrwir_instruction_t *instruction) {
    (void)runtime;
    (void)instruction;
    abort();
}
