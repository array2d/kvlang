#pragma once
#include "runtime_internal.h"

/* 函数级二维指令表（s0 = irseq，s1 = 参数轴），按需加载，执行循环内复用。
 *
 * key 与 value 的唯一事实源仍在 kvspace；这里只缓存「key -> 64 位地址」这一派生量。
 * 函数层只存与帧无关的部分：opcode/arity/描述符/字面量/类型。
 * 帧相对操作数的绝对 key 与 64 位地址属于帧，由 kvlangFrameRow 在本帧首次执行该行时绑定；
 * 删除/委托/handoff 使地址世代前进后，帧内地址全部失效并按 key 重绑。 */

typedef struct {
    char *
        descriptor; /* 描述符（变量名 / *[0,±k] / 绝对路径）；literal 时 NULL */
    kvlangXvalue_t literal; /* 字面量副本（owned，code_free 释放）；descriptor 时 None */
    char *type;             /* 声明类型（owned） */
    int s1;                 /* 参数轴：-nr..-1 读参，+1..+nw 写参 */
    bool is_literal;
    bool key_absolute; /* Ptr 操作数：descriptor 已是绝对 key，不再按帧解析 */
} kvlangOperand_t;

typedef struct {
    char *opcode; /* owned */
    int opcode_id;
    int nr, nw;
    kvlangOperand_t *cell; /* nr+nw：读参在前，写参在后 */
    bool load_write_value;
    bool is_def_rwir;
    int handler_id; /* myrwircaps 下标；非 native = kvlangRwirRouteId() */
    char *def_read_sig, *def_write_sig;
    int def_read_count, def_write_count, def_dynamic;
} kvlangCodeRow_t;

typedef struct {
    char *func_dir;
    kvlangCodeRow_t **rows; /* rows[s0]，按需增长 */
    size_t rows_cap;
} kvlangFuncCode_t;

/* 帧层：一个操作数的绑定结果 */
typedef struct {
    char *key;  /* owned */
    char *type; /* owned（细化后） */
    kvspaceKeyOff off;
    kvlangXvalue_t *literal; /* 指向行内字面量（借用）；非字面量 NULL */
    bool pin; /* 本行写槽覆盖此 key：读视图须在写前固化 */
} kvlangBoundCell_t;

typedef struct {
    kvlangBoundCell_t *cell; /* [n] */
    int n;
    bool bound;
    bool has_pin; /* 本行有读 cell 落在写槽之下：执行后需释放 pin 副本 */
} kvlangRowBind_t;

typedef struct {
    char *frame_root;
    kvlangFuncCode_t *code;
    kvlangRowBind_t *binds; /* 与 code->rows 平行 */
    size_t binds_cap;
    uint32_t epoch; /* 已同步的地址世代 */
} kvlangFrameCtx_t;

/* ExecuteMode 内的函数缓存：func_dir -> 二维表 */
typedef struct {
    char **keys;
    kvlangFuncCode_t **vals;
    size_t cap, count;
} kvlangFuncCache_t;

void kvlangFuncCacheInit(kvlangFuncCache_t *c);
void kvlangFuncCacheFree(kvlangFuncCache_t *c);

kvlangFrameCtx_t *kvlangFrameCtxNew(kvlangFuncCache_t *c, const char *func_dir,
                                    const char *frame_root);
void kvlangFrameCtxFree(kvlangFrameCtx_t *ctx);
/* 地址世代变化（删除/委托/handoff）后，帧内所有 keyoff 失效，执行时按 key 重绑。 */
void kvlangFrameCtxSync(kvlangFrameCtx_t *ctx, uint32_t epoch);

/* 取本帧 s0 行的绑定。行不存在返回 NULL 且 err[0]=0；出错返回 NULL 且 err 有消息。 */
kvlangRowBind_t *kvlangFrameRow(kvspace_t *kv, kvlangFrameCtx_t *ctx, int s0,
                                kvlangCodeRow_t **row_out, char *err,
                                uint32_t err_cap);
