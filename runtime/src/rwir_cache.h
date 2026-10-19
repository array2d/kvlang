#pragma once
#include "runtime_internal.h"

/* 指令派生数据缓存。设计说明见 rwir_cache.c 顶部。 */

typedef struct {
    char *pc;              /* NULL = 空槽 */
    int depth;
    kvlangRwirInst_t inst; /* 自持 opcode/name/type；val 恒空 */
    bool load_write_value;
    bool is_def_rwir; /* notinmyrwircaps 判定，decode 期固化 */
    char *def_sig;    /* def rwir 读参签名（\n 连接），仅 is_def_rwir */
    int def_nr;
    int def_dyn;
    char *def_wsig;   /* def rwir 写参签名 */
    int def_nw;
} rwir_cache_entry_t;

typedef struct {
    rwir_cache_entry_t *slot;
    size_t cap; /* 2 的幂；0 = 未分配 */
    size_t n;
} rwir_cache_t;

/* 查：命中返回条目，未命中 NULL。 */
rwir_cache_entry_t *rwir_cache_get(rwir_cache_t *c, const char *pc);

/* 拷入派生数据（不外借 opcode/name/type）；值不入缓存。调用方须先 get。 */
rwir_cache_entry_t *rwir_cache_put(kvlangKv_t *kv, rwir_cache_t *c,
                                   const char *pc, int depth,
                                   const kvlangRwirInst_t *inst);

/* 帧深变化后按帧作废：丢 depth >= floor（变深）或 depth > floor（变浅）。 */
void rwir_cache_drop(rwir_cache_t *c, int floor, bool inclusive);

void rwir_cache_free(rwir_cache_t *c);

/* 命中后按 name 现读值：读参恒取值；写参仅 load_write_value 时取值。 */
int rwir_cache_load_vals(kvlangKv_t *kv, kvlangRwirInst_t *inst,
                         bool load_write_value, char *err, uint32_t err_cap);

/* 命中视角释放：opcode/name/type 借用缓存（不得 free），val 本步自持。 */
void rwir_inst_release(kvlangRwirInst_t *inst, bool from_cache);
