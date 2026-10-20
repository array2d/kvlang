#pragma once
#include "runtime_internal.h"

/* 指令派生数据缓存。设计说明见 rwir_cache.c 顶部。 */

typedef struct {
    char *pc;              /* NULL = 空槽 */
    int depth;
    kvlangRwirInst_t instruction; /* 自持 opcode/name/type；value 恒空 */
    bool load_write_value;
    bool is_def_rwir; /* kvlangNotInMyRwirCaps 判定，decode 期固化 */
    char *def_read_sig;  /* def rwir 读参签名（\n 连接），仅 is_def_rwir */
    int def_read_count;
    int def_dynamic;
    char *def_write_sig; /* def rwir 写参签名 */
    int def_write_count;
} rwir_cache_entry_t;

typedef struct {
    rwir_cache_entry_t *slots;
    size_t capacity; /* 2 的幂；0 = 未分配 */
    size_t count;
} rwir_cache_t;

/* 查：命中返回条目，未命中 NULL。 */
rwir_cache_entry_t *kvlangRwirCacheGet(rwir_cache_t *c, const char *pc);

/* 拷入派生数据（不外借 opcode/name/type）；值不入缓存。调用方须先 get。 */
rwir_cache_entry_t *kvlangRwirCachePut(kvspace_t *kv, rwir_cache_t *c,
                                   const char *pc, int depth,
                                   const kvlangRwirInst_t *inst);

/* 帧深变化后按帧作废：丢 depth >= floor（变深）或 depth > floor（变浅）。 */
void kvlangRwirCacheDrop(rwir_cache_t *c, int floor, bool inclusive);

void kvlangRwirCacheFree(rwir_cache_t *c);

/* 命中后按 name 现读值：读参恒取值；写参仅 load_write_value 时取值。 */
int kvlangRwirCacheLoadValues(kvspace_t *kv, kvlangRwirInst_t *inst,
                         bool load_write_value, char *err, uint32_t err_cap);

/* 命中视角释放：opcode/name/type 借用缓存（不得 free），val 本步自持。 */
void rwir_instruction_release(kvlangRwirInst_t *inst, bool from_cache);
