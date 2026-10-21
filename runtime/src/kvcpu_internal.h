#pragma once
#include "runtime_internal.h"

/* kvcpu 拆分后的跨文件内部接口：签名加载与派发期类型校验。 */

char *load_def_reads(kvspace_t *kv, const char *key, int *out_nr, int *out_dyn);
char *load_def_writes(kvspace_t *kv, const char *key, int *out_nw);

int check_read_types(kvspace_t *kv, const char *vtid, const char *pc,
                     const char *opcode, const char *def_sig, int def_nr,
                     int dynamic, kvlangParam_t *args, int nargs);
int check_write_types(kvspace_t *kv, const char *vtid, const char *pc,
                      const char *opcode, const char *def_sig, int def_nw);

/* 以伪 OP_CALL 进入被调函数（执行循环的 user_call 与 vthread·call 共用）。 */
int kvlangKvcpuCallOpcode(kvspace_t *kv, const char *vtid, const char *pc,
                          const char *status_known, const char *opcode,
                          kvlangParam_t *reads, int nr,
                          kvlangParam_t *writes, int nw, char *pc_out,
                          size_t pc_out_cap, bool *pc_feedback,
                          struct kvspaceKeyOff *pc_off,
                          struct kvspaceKeyOff *status_off);
