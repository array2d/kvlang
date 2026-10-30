/* kvcpu_fast —— 快核：typed 标量行的连续执行内核（#474 P3 大优化 F1）。
 *
 * 通用执行循环每步都要：读 ‥pc 串 → 读 ‥status 串 → 刷 inst 模板 → 逐操作数
 * operand_load → 表派发 → handler 内 ReadInputs/arg_* → Emit → WriteBody →
 * NextPc（拼完整 PC 串 → cp_count → 62B 头 + 整串 memcpy → 回传缓冲 memcpy）。
 * 单步十几层调用，iops 每步约 100ns。
 *
 * 快核只接管「形状在绑定期即可判定」的行：int64/float64 typed 算术与比较、
 * 定长标量 copy、goto、br。前提全部复用通用路径已建立的借用视图：
 *   - 读：cell 的 rview（kvlangOperandViewPromote 建立，失效表/世代统一清理），
 *         或行内字面量；
 *   - 写：行的 body 槽（kvlangBodyCapture 建立，同一失效表）；
 *   - ‥pc / ‥status：kvspaceXvalueBodyView 槽视图（通用路径首次写/读时捕获）。
 * 任何前提不满足（视图未建、kind/宽度不符、None、除零、status 非 running、
 * rview 周期复核点……）立即停下，把这一行原样交还通用路径——语义不分叉。
 *
 * 持久化语义不变：每步先写值槽，再写 ‥pc（串值与通用路径逐字节一致），并在
 * 每步开始前检查 ‥status == "running"，pause/kill/crash 恢复行为与通用路径相同。 */
#include "func_code.h"

enum {
    FK_NONE = 0,
    FK_GOTO,
    FK_BR,
    FK_COPY,
    /* int64 二元 → int64 */
    FK_I_ADD, FK_I_SUB, FK_I_MUL, FK_I_DIV, FK_I_MOD,
    FK_I_AND, FK_I_OR, FK_I_XOR, FK_I_SHL, FK_I_SHR, FK_I_MAX, FK_I_MIN,
    /* int64 一元 → int64 */
    FK_I_NEG,
    /* int64 比较 → bool */
    FK_I_EQ, FK_I_NE, FK_I_LT, FK_I_LE, FK_I_GT, FK_I_GE,
    /* float64 二元 → float64 */
    FK_F_ADD, FK_F_SUB, FK_F_MUL, FK_F_DIV,
    /* float64 比较 → bool */
    FK_F_EQ, FK_F_NE, FK_F_LT, FK_F_LE, FK_F_GT, FK_F_GE,
};

#define FK_ST_UNKNOWN 0
#define FK_ST_NO 1
#define FK_ST_YES 2

typedef struct {
    const char *name;
    uint8_t op;
} fk_name_t;

static const fk_name_t fk_names[] = {
    {"int64" MEMBER_SEP "add", FK_I_ADD},   {"int64" MEMBER_SEP "sub", FK_I_SUB},
    {"int64" MEMBER_SEP "mul", FK_I_MUL},   {"int64" MEMBER_SEP "div", FK_I_DIV},
    {"int64" MEMBER_SEP "mod", FK_I_MOD},   {"int64" MEMBER_SEP "bitand", FK_I_AND},
    {"int64" MEMBER_SEP "bitor", FK_I_OR},  {"int64" MEMBER_SEP "bitxor", FK_I_XOR},
    {"int64" MEMBER_SEP "shl", FK_I_SHL},   {"int64" MEMBER_SEP "shr", FK_I_SHR},
    {"int64" MEMBER_SEP "max", FK_I_MAX},   {"int64" MEMBER_SEP "min", FK_I_MIN},
    {"int64" MEMBER_SEP "neg", FK_I_NEG},
    {"int64" MEMBER_SEP "eq", FK_I_EQ},     {"int64" MEMBER_SEP "neq", FK_I_NE},
    {"int64" MEMBER_SEP "lt", FK_I_LT},     {"int64" MEMBER_SEP "le", FK_I_LE},
    {"int64" MEMBER_SEP "gt", FK_I_GT},     {"int64" MEMBER_SEP "ge", FK_I_GE},
    {"float64" MEMBER_SEP "add", FK_F_ADD}, {"float64" MEMBER_SEP "sub", FK_F_SUB},
    {"float64" MEMBER_SEP "mul", FK_F_MUL}, {"float64" MEMBER_SEP "div", FK_F_DIV},
    {"float64" MEMBER_SEP "eq", FK_F_EQ},   {"float64" MEMBER_SEP "neq", FK_F_NE},
    {"float64" MEMBER_SEP "lt", FK_F_LT},   {"float64" MEMBER_SEP "le", FK_F_LE},
    {"float64" MEMBER_SEP "gt", FK_F_GT},   {"float64" MEMBER_SEP "ge", FK_F_GE},
};

static bool fk_is_cmp(uint8_t op) {
    return (op >= FK_I_EQ && op <= FK_I_GE) || (op >= FK_F_EQ && op <= FK_F_GE);
}
static bool fk_is_float(uint8_t op) { return op >= FK_F_ADD; }

/* 行形状分类：只在本 bind 首次进入快核时做一次。 */
__attribute__((noinline)) static void fk_classify(const kvlangCodeRow_t *row,
                                                  kvlangRowBind_t *b) {
    b->fk_state = FK_ST_NO;
    b->fk_op = FK_NONE;
    if (!row->opcode || row->is_def_rwir)
        return;
    if (row->nw == 0) {
        if (strcmp(row->opcode, OP_GOTO) == 0 && row->nr == 1 &&
            b->jump_irseq[0] > 0) {
            b->fk_op = FK_GOTO;
            b->fk_state = FK_ST_YES;
        } else if (strcmp(row->opcode, OP_BR) == 0 && row->nr == 3 &&
                   b->jump_irseq[0] > 0 && b->jump_irseq[1] > 0) {
            b->fk_op = FK_BR;
            b->fk_state = FK_ST_YES;
        }
        return;
    }
    if (row->nw != 1 || row->load_write_value || !b->body)
        return;
    if (strcmp(row->opcode, OP_COPY) == 0) {
        if (row->nr == 1 && b->copy_plain) {
            b->fk_op = FK_COPY;
            b->fk_state = FK_ST_YES;
        }
        return;
    }
    if (!row->typed_core || !row->result_kind)
        return;
    for (size_t i = 0; i < sizeof fk_names / sizeof fk_names[0]; i++) {
        if (strcmp(row->opcode, fk_names[i].name) != 0)
            continue;
        uint8_t op = fk_names[i].op;
        int want_nr = op == FK_I_NEG ? 1 : 2;
        if (row->nr != want_nr)
            return;
        uint32_t want_w = fk_is_cmp(op) ? 1u : 8u;
        if (row->result_width != want_w)
            return;
        b->fk_op = op;
        b->fk_state = FK_ST_YES;
        return;
    }
}

/* SHORT inline 标量值（v2 固定头、body 宽 w）→ body 指针。
 * langtype 在值 attr 区：kind 由调用方静态声明（fk_read 传参），
 * 只验 storetype/ptr/body 宽。 */
static inline const uint8_t *fk_scalar(const uint8_t *d, uint32_t len,
                                       const char *kind, uint32_t kl,
                                       uint32_t w) {
    (void)kind;
    (void)kl;
    if (len < KVSPACE_XVALUE_HEAD_LEN + w ||
        xh_len(d) != w ||
        xh_storetype(d) != KVSPACE_XVALUE_ST_SHORT ||
        xh_ptr(d) != KVSPACE_XVALUE_PTR_INLINE)
        return NULL;
    return d + KVSPACE_XVALUE_HEAD_LEN;
}

/* 读一个操作数为 kind 标量：字面量或已提升的 rview；其它情形 NULL（交还通用路径）。
 * rview 的周期复核点（每 128 次）不在快核里消费，留给 operand_load。 */
static inline const uint8_t *fk_read(kvlangBoundCell_t *c, const char *kind,
                                     uint32_t kl, uint32_t w) {
    if (c->literal)
        return c->literal->bytes
                   ? fk_scalar(c->literal->bytes, c->literal->length, kind, kl, w)
                   : NULL;
    if (c->rview_state != 1 || ((c->rview_hits + 1u) & 127u) == 0)
        return NULL;
    const uint8_t *p = fk_scalar(c->rview_bytes, c->rview_len, kind, kl, w);
    if (p)
        c->rview_hits++;
    return p;
}

static inline int64_t rd_i64(const uint8_t *p) {
    uint64_t u = 0;
    for (int i = 0; i < 8; i++)
        u |= (uint64_t)p[i] << (8 * i);
    return (int64_t)u;
}
static inline void wr_i64(uint8_t *p, int64_t v) {
    uint64_t u = (uint64_t)v;
    for (int i = 0; i < 8; i++)
        p[i] = (uint8_t)(u >> (8 * i));
}
static inline double rd_f64(const uint8_t *p) {
    uint64_t u = (uint64_t)rd_i64(p);
    double d;
    memcpy(&d, &u, 8);
    return d;
}
static inline void wr_f64(uint8_t *p, double d) {
    uint64_t u;
    memcpy(&u, &d, 8);
    wr_i64(p, (int64_t)u);
}

/* 写槽：body 已捕获且 kind/宽度一致才可写。 */
static inline uint8_t *fk_dst(kvlangRowBind_t *b, const char *kind,
                              uint32_t kl, uint32_t w) {
    kvlangBodySlot *s = b->body;
    if (!s->ok || s->len != w || !s->kind || s->kind_len != kl ||
        memcmp(s->kind, kind, kl) != 0)
        return NULL;
    return s->body;
}

/* ‥pc 槽视图上的原地写状态。串 = prefix + "[<irseq>,0]"，与
 * kvlangRwirNextPcFromPrefix 逐字节一致；头部 a 与 langtype 码点数只在尾长变化时改。 */
typedef struct {
    uint8_t *head;
    uint8_t *body;
    uint64_t cap; /* min(view.body_cap, 头部 b) */
    uint32_t plen;
    uint32_t prefix_cp;
    uint32_t cur_len;
} fk_pc_t;

static uint32_t utf8_cps(const char *s, uint32_t n) {
    uint32_t c = 0;
    for (uint32_t i = 0; i < n; i++)
        if (((uint8_t)s[i] & 0xC0) != 0x80)
            c++;
    return c;
}

static size_t fk_tail(char *out, int irseq) {
    char tmp[12];
    int d = 0;
    unsigned v = (unsigned)irseq;
    do {
        tmp[d++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    size_t p = 0;
    out[p++] = '[';
    while (d)
        out[p++] = tmp[--d];
    out[p++] = ',';
    out[p++] = '0';
    out[p++] = ']';
    return p;
}

static bool fk_pc_open(fk_pc_t *pc, kvspaceKeyOff *ko, const char *prefix,
                       int plen) {
    if (!ko->view_ok || plen <= 0)
        return false;
    uint8_t *h = ko->view.head;
    /* v2 固定头：SLACK inline，body = PC 串，len = 有效字节。 */
    if (ko->view.headlen != KVSPACE_XVALUE_HEAD_LEN ||
        xh_storetype(h) != KVSPACE_XVALUE_ST_SLACK ||
        xh_ptr(h) != KVSPACE_XVALUE_PTR_INLINE)
        return false;
    uint64_t a = xh_len(h);
    uint64_t cap = ko->view.body_cap;
    if (a > cap || a < (uint64_t)plen ||
        memcmp(ko->view.body, prefix, (size_t)plen) != 0)
        return false;
    pc->head = h;
    pc->body = ko->view.body;
    pc->cap = cap;
    pc->plen = (uint32_t)plen;
    pc->prefix_cp = utf8_cps(prefix, (uint32_t)plen);
    pc->cur_len = (uint32_t)a;
    return true;
}

/* 原地写 PC 尾 [irseq,0]；容量不足返回 false（尚未写任何字节）。 */
static inline bool fk_pc_write(fk_pc_t *pc, int irseq) {
    char tail[16];
    size_t tl = fk_tail(tail, irseq);
    uint32_t len = pc->plen + (uint32_t)tl;
    if (len > pc->cap)
        return false;
    memcpy(pc->body + pc->plen, tail, tl);
    if (len != pc->cur_len) {
        kvspaceXvalueHead h = {
            .storetype = KVSPACE_XVALUE_ST_SLACK,
            .ptr = KVSPACE_XVALUE_PTR_INLINE,
        };
        kvspaceXvalueHeadSetLen(&h, len);
        memcpy(pc->head, &h, sizeof h);
        pc->cur_len = len;
    }
    return true;
}

static inline bool fk_status_running(kvspaceKeyOff *ko) {
    if (!ko->view_ok)
        return false;
    const uint8_t *h = ko->view.head;
    return ko->view.headlen == KVSPACE_XVALUE_HEAD_LEN &&
           xh_storetype(h) == KVSPACE_XVALUE_ST_SLACK &&
           xh_ptr(h) == KVSPACE_XVALUE_PTR_INLINE && xh_len(h) == 7 &&
           ko->view.body_cap >= 7 && memcmp(ko->view.body, "running", 7) == 0;
}

/* 单步：成功返回下一 irseq（>0）；不可快返回 0（未产生任何副作用）。 */
static inline int fk_step(kvlangRowBind_t *b, int irseq, uint8_t **dst_out,
                          uint8_t *val, uint32_t *w_out) {
    kvlangBoundCell_t *c = b->cell;
    switch (b->fk_op) {
    case FK_GOTO:
        *w_out = 0;
        return b->jump_irseq[0];
    case FK_BR: {
        const uint8_t *p = fk_read(&c[0], KVSPACE_KIND_BOOL, 4, 1);
        if (!p)
            return 0;
        *w_out = 0;
        return p[0] ? b->jump_irseq[1] : b->jump_irseq[0];
    }
    case FK_COPY: {
        kvlangBodySlot *s = b->body;
        if (!s->ok || !s->kind || (c[1].type && c[1].type[0]))
            return 0;
        if (c[0].coerce_tid &&
            (c[0].coerce_len != s->kind_len ||
             memcmp(c[0].type, s->kind, s->kind_len) != 0))
            return 0;
        const uint8_t *p = fk_read(&c[0], s->kind, s->kind_len, s->len);
        if (!p)
            return 0;
        memcpy(val, p, s->len);
        *dst_out = s->body;
        *w_out = s->len;
        return irseq + 1;
    }
    default:
        break;
    }
    uint8_t op = b->fk_op;
    bool fl = fk_is_float(op);
    const char *ik = fl ? KVSPACE_KIND_FLOAT64 : KVSPACE_KIND_INT64;
    uint32_t ikl = fl ? 7 : 5;
    const uint8_t *pa = fk_read(&c[0], ik, ikl, 8);
    if (!pa)
        return 0;
    const uint8_t *pb = NULL;
    if (op != FK_I_NEG) {
        pb = fk_read(&c[1], ik, ikl, 8);
        if (!pb)
            return 0;
    }
    bool cmp = fk_is_cmp(op);
    uint8_t *dst = cmp ? fk_dst(b, KVSPACE_KIND_BOOL, 4, 1)
                       : fk_dst(b, ik, ikl, 8);
    if (!dst)
        return 0;
    if (!fl) {
        int64_t x = rd_i64(pa), y = pb ? rd_i64(pb) : 0, r = 0;
        bool br = false;
        switch (op) {
        case FK_I_ADD: r = (int64_t)((uint64_t)x + (uint64_t)y); break;
        case FK_I_SUB: r = (int64_t)((uint64_t)x - (uint64_t)y); break;
        case FK_I_MUL: r = (int64_t)((uint64_t)x * (uint64_t)y); break;
        case FK_I_DIV:
            if (y == 0 || (y == -1 && x == INT64_MIN))
                return 0;
            r = x / y;
            break;
        case FK_I_MOD:
            if (y == 0 || (y == -1 && x == INT64_MIN))
                return 0;
            r = x % y;
            break;
        case FK_I_AND: r = x & y; break;
        case FK_I_OR: r = x | y; break;
        case FK_I_XOR: r = x ^ y; break;
        case FK_I_SHL:
            if ((uint64_t)y >= 64 || x < 0)
                return 0;
            r = x << y;
            break;
        case FK_I_SHR:
            if ((uint64_t)y >= 64)
                return 0;
            r = x >> y;
            break;
        case FK_I_MAX: r = x >= y ? x : y; break;
        case FK_I_MIN: r = x <= y ? x : y; break;
        case FK_I_NEG: r = (int64_t)(0 - (uint64_t)x); break;
        case FK_I_EQ: br = x == y; break;
        case FK_I_NE: br = x != y; break;
        case FK_I_LT: br = x < y; break;
        case FK_I_LE: br = x <= y; break;
        case FK_I_GT: br = x > y; break;
        case FK_I_GE: br = x >= y; break;
        default: return 0;
        }
        if (cmp) {
            val[0] = br ? 1 : 0;
            *w_out = 1;
        } else {
            wr_i64(val, r);
            *w_out = 8;
        }
    } else {
        double x = rd_f64(pa), y = rd_f64(pb), r = 0;
        bool br = false;
        switch (op) {
        case FK_F_ADD: r = x + y; break;
        case FK_F_SUB: r = x - y; break;
        case FK_F_MUL: r = x * y; break;
        case FK_F_DIV:
            if (y == 0)
                return 0;
            r = x / y;
            break;
        case FK_F_EQ: br = x == y; break;
        case FK_F_NE: br = x != y; break;
        case FK_F_LT: br = x < y; break;
        case FK_F_LE: br = x <= y; break;
        case FK_F_GT: br = x > y; break;
        case FK_F_GE: br = x >= y; break;
        default: return 0;
        }
        if (cmp) {
            val[0] = br ? 1 : 0;
            *w_out = 1;
        } else {
            wr_f64(val, r);
            *w_out = 8;
        }
    }
    *dst_out = dst;
    return irseq + 1;
}

int kvlangFastRun(kvlangFrameCtx_t *ctx, int *irseq_io, kvspaceKeyOff *pc_off,
                  kvspaceKeyOff *status_off, const char *prefix, int prefix_len,
                  char *pc_out, size_t pc_out_cap) {
    int irseq = *irseq_io;
    fk_pc_t pc;
    if (!fk_pc_open(&pc, pc_off, prefix, prefix_len))
        return 0;
    int steps = 0;
    kvlangCodeRow_t **rows = ctx->code->rows;
    for (;;) {
        if ((size_t)irseq >= ctx->code->rows_cap ||
            (size_t)irseq >= ctx->binds_cap)
            break;
        kvlangCodeRow_t *row = rows[irseq];
        kvlangRowBind_t *b = &ctx->binds[irseq];
        if (!row || !b->bound || b->reenter_gen != ctx->reenter_gen)
            break;
        if (b->fk_state != FK_ST_YES) {
            if (b->fk_state != FK_ST_UNKNOWN)
                break;
            fk_classify(row, b);
            if (b->fk_state != FK_ST_YES)
                break;
        }
        /* 每步开始前读 ‥status（与通用循环相同的检查点）。 */
        if (!fk_status_running(status_off))
            break;
        uint8_t val[8];
        uint8_t *dst = NULL;
        uint32_t w = 0;
        int next = fk_step(b, irseq, &dst, val, &w);
        if (next <= 0)
            break;
        /* 先值后 PC：与通用路径（WriteBody → NextPc）的落盘顺序一致。
         * PC 容量不足时值也不写，整行交还通用路径。 */
        char tail[16];
        if (pc.plen + fk_tail(tail, next) > pc.cap)
            break;
        if (w)
            memcpy(dst, val, w);
        if (!fk_pc_write(&pc, next))
            break; /* 不可达：容量已预检 */
        irseq = next;
        steps++;
    }
    if (steps > 0) {
        size_t n = pc.plen;
        char tail[16];
        size_t tl = fk_tail(tail, irseq);
        if (n + tl + 1 > pc_out_cap)
            kvlangDie("feedback pc exceeds %zu bytes", pc_out_cap);
        memcpy(pc_out, prefix, n);
        memcpy(pc_out + n, tail, tl);
        pc_out[n + tl] = 0;
    }
    *irseq_io = irseq;
    return steps;
}
