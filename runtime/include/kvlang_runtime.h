#pragma once
#include <stdint.h>

typedef struct kvlang_rt kvlang_rt;

kvlang_rt *kvlang_rt_connect(const char *dsn);
void kvlang_rt_disconnect(kvlang_rt *rt);
void *kvlang_rt_kv(kvlang_rt *rt);   /* 内部 kv 句柄，供 rwirext 扩展用 */

int kvlang_rt_execute_pc(kvlang_rt *rt, const char *pc);

int kvlang_rt_execute(kvlang_rt *rt, const char *funcname,
                      const char *const *args, int nargs,
                      char **ret, char *err, uint32_t err_cap);
