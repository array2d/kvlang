#pragma once
#include <stdint.h>

typedef struct kvlang_rt kvlang_rt;

kvlang_rt *kvlang_rt_connect(const char *dsn);
void kvlang_rt_disconnect(kvlang_rt *rt);
void *kvlang_rt_kv(kvlang_rt *rt);   /* 内部 kv 句柄，供 rwirext 扩展用 */

int kvlang_rt_execute_pc(kvlang_rt *rt, const char *pc);

/* 模式2（runtime 主导 + term 嵌入）：分配 vthread 并 bootstrap，返回 vid（malloc）。
 * term 专注这一个 vid 的 ext rwir 处理。 */
char *kvlang_rt_bootstrap(kvlang_rt *rt, const char *funcname,
                          const char *const *args, int nargs);

/* 模式2：从 vid 的当前 pc 执行 vthread，遇 ext rwir 不再 handoff/watch，直接返回。
 * 返回值：1=遇 ext rwir（*out_pc=该 PC，malloc，调用方 free）；0=vthread done；-1=错误。 */
int kvlang_rt_execute_vthread(kvlang_rt *rt, const char *vid, char **out_pc);

int kvlang_rt_execute(kvlang_rt *rt, const char *funcname,
                      const char *const *args, int nargs,
                      char **ret, char *err, uint32_t err_cap);
