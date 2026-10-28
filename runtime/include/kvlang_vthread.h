#pragma once
#include <stdint.h>

typedef struct kvlangRuntime_t kvlangRuntime_t;

kvlangRuntime_t *kvlangRuntimeConnect(const char *dsn);
void kvlangRuntimeDisconnect(kvlangRuntime_t *rt);

int kvlangRuntimeExecutePc(kvlangRuntime_t *rt, const char *pc);

/* runtime 内部 kvspace 句柄——runtime-rs 须复用它而非另开连接（durable 惰性
 * flush 只在同句柄内相干）。返回句柄生命周期同 rt，调用方不得 close。 */
void *kvlangRuntimeKvspaceHandle(kvlangRuntime_t *rt);

/* 模式2（runtime 主导 + term 嵌入）：分配 vthread 并 bootstrap，返回
 * vid（malloc）。 term 专注这一个 vid 的 ext rwir 处理。 */
char *kvlangRuntimeBootstrap(kvlangRuntime_t *rt, const char *funcname,
                             const char *const *args, int nargs);

/* 模式2：从 vid 的当前 pc 执行 vthread，遇 ext rwir 不再
 * handoff/watch，直接返回。 返回值：1=遇 ext rwir（*out_pc=该
 * PC，malloc，调用方 free）；0=vthread done；-1=错误。 */
int kvlangRuntimeExecuteVthread(kvlangRuntime_t *rt, const char *vid,
                                char **out_pc);

int kvlangRuntimeExecute(kvlangRuntime_t *rt, const char *funcname,
                         const char *const *args, int nargs, char **ret,
                         char *err, uint32_t err_cap);
