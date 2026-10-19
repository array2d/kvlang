#pragma once
#include "runtime_internal.h"

/* kvcpu 拆分后的跨文件内部接口：签名加载与派发期类型校验。 */

char *load_def_reads(kvlangKv_t *kv, const char *key, int *out_nr, int *out_dyn);
char *load_def_writes(kvlangKv_t *kv, const char *key, int *out_nw);

int check_read_types(kvlangKv_t *kv, const char *vtid, const char *pc,
                     const char *opcode, const char *def_sig, int def_nr,
                     int dynamic, kvlangParam_t *args, int nargs);
int check_write_types(kvlangKv_t *kv, const char *vtid, const char *pc,
                      const char *opcode, const char *def_sig, int def_nw);
