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
    bool typed_core; /* P1b3：sig ∈ BIN/CMP/UN，row 装载期一次；typed 零检查路径 */
    /* T-120 A：typed 行的操作数 langtype（装载期固化；0=不可用 view）。
     * 低 8 位 kind、高 16 位元素宽度；放 row 而非 cell，递归重建 bind 不重算。 */
    uint32_t view_pack;
    bool is_def_rwir;
    int handler_id; /* myrwircaps 下标；非 native = kvlangRwirRouteId() */
    /* 定长标量写快路径：结果 type/宽度（来自同一张能力表）；NULL = 不快。 */
    const char *result_kind;
    uint32_t result_width;
    char *def_read_sig, *def_write_sig;
    int def_read_count, def_write_count, def_dynamic;
} kvlangCodeRow_t;

typedef struct {
    char *func_dir;
    kvlangCodeRow_t **rows; /* rows[s0]，按需增长 */
    size_t rows_cap;
} kvlangFuncCode_t;

/* 帧层：一个操作数的绑定结果 */
typedef struct kvlangBoundCell {
    char *key;  /* owned */
    char *type; /* owned（细化后） */
    /* 读 cell 的 off.fast 存放 lazy operand view（owned，T-120 A）；写 cell
     * 的 off.fast 仍是 body slot —— bind_free 只对 view_state==2 的 cell 释放。 */
    kvspaceKeyOff off;
    kvlangXvalue_t *literal; /* 指向行内字面量（借用）；非字面量 NULL */
    bool pin; /* 本行写槽覆盖此 key：读视图须在写前固化 */
    uint8_t view_state; /* 0 未判定 / 1 计数 / 2 已提升 / 3 禁用；仅 typed 读 cell */
    union {
        struct { int tid; uint16_t len; } coerce; /* 非 typed 行 */
        struct { uint16_t hits; uint16_t width; uint8_t kind; } view; /* typed 行 */
    };
    uint8_t *coerce_buf; /* typed 读转换的 64B 临时视图（惰性分配，帧内复用） */
} kvlangBoundCell_t;
_Static_assert(sizeof(kvlangBoundCell_t) == 96,
               "kvlangBoundCell_t must stay 96B (operand view fields reuse existing bytes)");

/* 定长标量写的绑定状态：只给候选行分配一个，不放大每个操作数 cell。 */
typedef struct kvlangBodySlot {
    const char *key;        /* borrowed：cell 的 key */
    const char *kind;       /* 静态结果类型 */
    uint32_t width;
    uint8_t *head;          /* T-120 A：XValue wire head（借用后端常驻空间） */
    uint32_t headlen;
    uint8_t *body;
    uint32_t len;
    kvspaceKeyOff *off;     /* borrowed：cell 的 keyoff */
    kvspaceRef_t ref;       /* 提升时的 ref：revalidate 只比对它 */
    bool ok;                /* body 指针有效 */
    bool dead;              /* 上次捕获失败；等该 key 被写后重试 */
    bool reg;               /* 已挂进全局失效表 */
    uint32_t bucket;
    struct kvlangBodySlot *next;
} kvlangBodySlot;

/* 读 view：body slot + 版本化 token（只在提升时分配，写候选行不背 token）。 */
typedef struct {
    kvlangBodySlot slot;            /* 首成员：cell->off.fast 指向它 */
    kvspaceViewToken_t token;
} kvlangReadViewSlot;

typedef struct {
    kvlangBoundCell_t *cell; /* [n] */
    int n;
    bool bound;
    bool has_pin;          /* 本行有读 cell 落在写槽之下：执行后需释放 pin 副本 */
    kvlangBodySlot *body; /* 定长标量写槽（候选行才有，owned） */
    /* goto/br 目标 irseq：绑定期从 int64 字面量固化（0 = 不可用，执行期回退）。
     * goto 用 [0]；br 用 [false]=[0]、[true]=[1]。不分配字符串：目标 PC 在
     * 跳转时用帧根 + irseq 格式化到栈缓冲，避免每次函数调用都 malloc。 */
    int jump_irseq[2];
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

/* 定长标量写槽捕获：head 完全匹配才成功；失败置 dead（等该 key 被写后重试）。
 * vthread_root 形如 /vthread/<vid>，写 key 必须在其子树内。 */
__attribute__((visibility("hidden"))) bool
kvlangBodyCapture(kvspace_t *kv, kvlangBodySlot *slot, const char *vthread_root,
                  kvspaceViewToken_t *tok);   /* tok 非 NULL 时用版本化 view */
/* 校验 wire 形状并填充 slot；tok 的 head/headlen/value_len 由调用方保证已就绪。 */
__attribute__((visibility("hidden"))) bool
kvlangBodyAdopt(kvlangBodySlot *slot, const uint8_t *val, uint32_t len);
/* 全局失效所有 body slot（不 free；下次使用惰性重捕）。 */
__attribute__((visibility("hidden"))) void kvlangBodyInvalidateAll(void);
/* 由 key 显式取 /vthread/<vid> 前缀（key 必须在 vthread 子树内）。 */
__attribute__((visibility("hidden"))) bool
kvlangVthreadRootOfKey(const char *key, char *out, size_t cap);
/* 只解注册，不释放内存（调用方决定是 free slot 还是整个容器）。 */
__attribute__((visibility("hidden"))) void
kvlangBodySlotRelease(kvlangBodySlot *slot);
/* 释放读 view 容器（slot + token）。 */
__attribute__((visibility("hidden"))) void
kvlangReadViewSlotFree(kvlangReadViewSlot *rs);
/* 取本帧 s0 行的绑定。行不存在返回 NULL 且 err[0]=0；出错返回 NULL 且 err 有消息。 */
kvlangRowBind_t *kvlangFrameRow(kvspace_t *kv, kvlangFrameCtx_t *ctx, int s0,
                                kvlangCodeRow_t **row_out, char *err,
                                uint32_t err_cap);
