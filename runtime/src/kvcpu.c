#include "func_code.h"
#include "xvaluebodyview.h"
#include "kvcpu_internal.h"

/* P0-2：换行时刷新 inst 与参数数组的静态字段（name/type/is_address/write_offs）。
 * 冷路径：每帧每行一次；热循环里只判 bind/row 是否变化。 */
__attribute__((noinline)) static void row_template(
    kvlangRwirInst_t *inst, kvlangParam_t *reads, kvlangParam_t *writes,
    kvspaceKeyOff **write_offs, kvlangCodeRow_t *row, kvlangRowBind_t *bind) {
    for (int i = 0; i < row->nr; i++) {
        kvlangParam_t *p = &reads[i];
        kvlangBoundCell_t *c = &bind->cell[i];
        p->name = c->key;
        p->type = c->type;
        p->is_address = !c->literal;
        kvlangXvalueZero(&p->value);
    }
    for (int j = 0; j < row->nw; j++) {
        kvlangParam_t *p = &writes[j];
        kvlangBoundCell_t *c = &bind->cell[row->nr + j];
        p->name = c->key;
        p->type = c->type;
        p->is_address = true;
        kvlangXvalueZero(&p->value);
        write_offs[j] = &c->off;
    }
    inst->opcode = row->opcode;
    inst->opcode_id = row->opcode_id;
    inst->typed_core = row->typed_core;
    inst->read_count = row->nr;
    inst->write_count = row->nw;
    inst->write_offs = row->nw ? write_offs : NULL;
}

/* 统一错误出口：置 vthread error 并返回 -1。 */
static int execute_error(kvspace_t *kv, const char *vtid, const char *pc,
                         const char *fmt, ...) {
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    kvlangVthreadSetError(kv, vtid, pc, msg);
    return -1;
}

/* KVMODE_RETURN 收尾：把下一个 PC 的所有权交给调用方（或释放）。 */
static int execute_yield(char **out_pc, char *next_pc) {
    if (out_pc)
        *out_pc = next_pc;
    else
        free(next_pc);
    return 1;
}

/* v2 SHORT inline 标量 wire 判定（rview 快路径）。 */
static inline bool rview_scalar(const uint8_t *d) {
    return d && xh_storetype(d) == KVSPACE_XVALUE_ST_SHORT &&
           xh_ptr(d) == KVSPACE_XVALUE_PTR_INLINE;
}

/* 读一个操作数目标值；None 合法。地址未解析时按 key 读并补一次绑定。 */
static int operand_load(kvspace_t *kv, kvlangBoundCell_t *cell, uint32_t pack,
                        kvlangXvalue_t *out) {
    kvlangXvalueZero(out);
    /* T-120 A：lazy operand view；pack=0（非 typed 行）直接跳过。 */
    if (pack != 0 && kvlangReadViewTry(kv, cell, pack, out))
        return 0;
    kvspaceKeyOff *ko = &cell->off;
    /* v2：rview 是 SHORT inline 标量视图（langtype 在值 attr 区，kind 取
     * cell 静态声明）。 */
    if (cell->rview_state == 1) {
        if ((++cell->rview_hits & 127u) != 0 &&
            rview_scalar(cell->rview_bytes)) {
            out->bytes = cell->rview_bytes;
            out->length = cell->rview_len;
            if (cell->type && cell->type[0])
                kvlangXvalueSetLangtype(out, cell->type,
                                        (uint32_t)strlen(cell->type));
        } else {
            uint8_t *d = NULL;
            uint32_t len = 0;
            if (kvspaceGetByRef(kv->handle, &ko->off, cell->key, &d, &len) != 0)
                return -1;
            if (d == cell->rview_bytes && len == cell->rview_len &&
                rview_scalar(d)) {
                out->bytes = d;
                out->length = len;
                if (cell->type && cell->type[0])
                    kvlangXvalueSetLangtype(out, cell->type,
                                            (uint32_t)strlen(cell->type));
            } else {
                cell->rview_bytes = NULL;
                cell->rview_len = 0;
                cell->rview_hits = 0;
                cell->rview_state = 2;
                if (d && len > 0) {
                    out->bytes = d;
                    out->length = len;
                }
            }
        }
    } else if (ko->resolved) {
        uint8_t *d = NULL;
        uint32_t len = 0;
        if (kvspaceGetByRef(kv->handle, &ko->off, cell->key, &d, &len) != 0)
            return -1;
        if (d && len > 0) {
            out->bytes = d;
            out->length = len;
            if (cell->type && cell->type[0])
                kvlangXvalueSetLangtype(out, cell->type,
                                        (uint32_t)strlen(cell->type));
            if (cell->rview_state == 0 && ++cell->rview_hits >= 8)
                kvlangOperandViewPromote(kv, cell, cell->key, d, len);
        }
    } else {
        if (kvlangKvspaceGetOne(kv, cell->key, out) != 0)
            return -1;
        /* 变量可能在加载后才创建：补一次绑定，成功即切 ByRef。 */
        if (kvspaceResolveRef(kv->handle, cell->key, &ko->off) == 0)
            ko->resolved = true;
    }
    /* typed 读视图：声明 kind 与实际不同 → 转到 cell 复用缓冲（绑定期已备好文本）。
     * pack != 0 表示 typed 行：union 存的是 view 计数，不能当 coerce 读。 */
    if (pack == 0 && cell->coerce.tid && cell->type && out->bytes &&
        out->length &&
        (xh_langtype_len_of(out) != cell->coerce.len ||
         memcmp(xh_langtype_of(out), cell->type, cell->coerce.len) != 0)) {
        if (!cell->coerce_buf)
            cell->coerce_buf = malloc(64);
        if (!cell->coerce_buf)
            kvlangDie("out of memory coercing typed operand");
        kvlangXvalue_t cv;
        if (kvlangXvalueCoerce(cell->type, out, cell->coerce_buf, 64, &cv) != 0)
            return -2; /* 不可转换：值保持原样，由调用方报 TypeError */
        *out = cv;
    }
    return 0;
}

/* pin 副本是本指令为「会被写槽覆盖」的读值做的显式 copy，执行完即释放。 */
static void pinned_free(kvlangRowBind_t *bind, int nr, kvlangParam_t *reads) {
    if (!bind->has_pin)
        return;
    for (int i = 0; i < nr; i++)
        if (bind->cell[i].pin) /* 字面量不可能 pin（无槽可覆盖） */
            kvlangXvalueFree(&reads[i].value);
}

#ifdef KV_COUNT
static void count_report(void) {
    if (!getenv("KV_COUNT"))
        return;
    fprintf(stderr,
            "COUNT spec=%lu gen=%lu narrow=%lu cmp=%lu body_fast=%lu "
            "body_fallback=%lu body_cells_max=%lu cap_try=%lu cap_ok=%lu "
            "rv_prom=%lu rv_demote=%lu rv_hit=%lu\n",
            kvCSpec, kvCGen, kvCNarrow, kvCCompare, kvCBodyFast,
            kvCBodyFallback, kvCBodyCellsMax, kvCBodyCapAttempt,
            kvCBodyCapOk, kvCReadViewProm, kvCReadViewDemote,
            kvCReadViewHit);
}
#define COUNT_REPORT() count_report()
#else
#define COUNT_REPORT() do {} while (0)
#endif

__attribute__((noinline)) static bool
frame_func_dir_buf(kvspace_t *kv, const char *frame_root, char *out,
                   size_t cap) {
    char stk[KV_KEY_MAX], key[KV_KEY_MAX];
    if (!kvlangKeytreeStackBuffer(frame_root, stk, sizeof stk))
        return false;
    kvlangKeyFormat(key, sizeof key, "%s%s", stk, SEG_LIB);
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    kvlangKvspaceGetOne(kv, key, &v);
    if (kvlangXvalueNone(&v) || !kvlangXvalueKindIs(&v, KVSPACE_KIND_CHAR_UTF8))
        return false;
    const char *src = (const char *)xh_body(v.bytes);
    size_t n = (size_t)xh_content_len(v.bytes);
    while (n > 0 && src[n - 1] == '/')
        n--;
    if (n == 0 || n + 2 > cap)
        return false;
    memcpy(out, src, n);
    out[n] = '/';
    out[n + 1] = 0;
    return true;
}

#define FCTX_VARIANTS 4 /* ctx variants per depth, by park target set */

static void fctx_pool_free(kvlangFrameCtx_t **pool) {
    for (size_t i = 0; i < (size_t)(MAX_STACK_DEPTH + 1) * FCTX_VARIANTS; i++)
        kvlangFrameCtxFree(pool[i]);
    free(pool);
}

static bool park_vkey(const kvlangParkedFrame_t *pf, const char *fdir,
                      char *out, size_t cap) {
    size_t n = 0;
    const char *parts[1 + 2 * KVLANG_PARK_MAX_PARAMS];
    int np = 0;
    parts[np++] = fdir;
    for (int i = 0; i < pf->nr && i < KVLANG_PARK_MAX_PARAMS; i++)
        parts[np++] = pf->r_target[i];
    for (int i = 0; i < pf->nw && i < KVLANG_PARK_MAX_PARAMS; i++)
        parts[np++] = pf->w_target[i];
    for (int k = 0; k < np; k++) {
        size_t l = strlen(parts[k]);
        if (n + l + 2 > cap)
            return false;
        memcpy(out + n, parts[k], l);
        n += l;
        out[n++] = '\x1f';
    }
    out[n] = 0;
    return true;
}

/* fctx[d]: active ctx (borrowed); pool holds all variants per depth. */
__attribute__((noinline)) static int
switch_frame_ctx(kvspace_t *kv, const char *vtid, const char *cur,
                 const char *cur_frame, int depth, int last_depth,
                 uint32_t addr_epoch, kvlangFuncCache_t *fcache,
                 kvlangFrameCtx_t **fctx, kvlangFrameCtx_t **pool) {
    static __thread uint32_t lru_clock;
    kvlangParkedFrame_t *pf = kvlangParkedFrameGet(kv, vtid, depth);
    if (depth <= last_depth && fctx[depth] &&
        strcmp(fctx[depth]->frame_root, cur_frame) == 0) {
        fctx[depth]->park = pf;
        return 0;
    }
    char fdir[KV_KEY_MAX];
    const char *fdir_p = (pf && pf->func_dir[0]) ? pf->func_dir : NULL;
    if (!fdir_p) {
        if (!frame_func_dir_buf(kv, cur_frame, fdir, sizeof fdir))
            return execute_error(kv, vtid, cur,
                                 "RuntimeError: frame has no ‥lib: %s",
                                 cur_frame);
        fdir_p = fdir;
    }
    kvlangFrameCtx_t **slots = &pool[(size_t)depth * FCTX_VARIANTS];
    char vk[2048];
    const bool use_vk = pf && pf->warm && park_vkey(pf, fdir_p, vk, sizeof vk);
    int victim = 0;
    for (int i = 0; i < FCTX_VARIANTS; i++) {
        kvlangFrameCtx_t *c = slots[i];
        if (!c) {
            if (slots[victim])
                victim = i;
            continue;
        }
        if (slots[victim] && c->lru < slots[victim]->lru)
            victim = i;
        if (strcmp(c->frame_root, cur_frame) != 0 ||
            strcmp(c->code->func_dir, fdir_p) != 0)
            continue;
        if (use_vk ? (!c->vkey || strcmp(c->vkey, vk) != 0) : c->vkey != NULL)
            continue;
        c->park = pf;
        if (depth > last_depth || fctx[depth] != c)
            kvlangFrameCtxReenter(kv, c, addr_epoch);
        c->lru = ++lru_clock;
        fctx[depth] = c;
        return 0;
    }
    kvlangFrameCtxFree(slots[victim]);
    kvlangFrameCtx_t *c = kvlangFrameCtxNew(fcache, fdir_p, cur_frame);
    c->park = pf;
    c->vkey = use_vk ? strdup(vk) : NULL;
    c->lru = ++lru_clock;
    slots[victim] = c;
    fctx[depth] = c;
    return 0;
}

__attribute__((aligned(64))) int kvlangKvcpuExecuteMode(kvspace_t *kv, const char *pc, kvmode_t mode,
                           char **out_pc) {
    if (out_pc)
        *out_pc = NULL;
    char vtid_b[KV_KEY_MAX];
    const char *vtid = kvlangKeytreeVthreadIdFromPc(pc, vtid_b, sizeof vtid_b);
    if (vtid[0] == 0) {
        return -1;
    }

    /* 每步复用的缓冲：pc/status 键与值、帧根全部跨步保留，免每步 malloc/free。
     * pc/status 值缓冲按页对齐：shm 常驻映射是页对齐的，未对齐时实测 k_nucleotide 有约
     * 12% 的栈布局敏感退化（换 64/128B 对齐都复现，只有 4K 对齐消失）。 */
    char pc_key[KV_KEY_MAX], st_key[KV_KEY_MAX];
    char cur_b[KV_KEY_MAX] __attribute__((aligned(4096)));
    char status_b[KV_KEY_MAX] __attribute__((aligned(4096)));
    /* PC 回传双缓冲：NextPc 写另一块，绝不覆盖本步 f->pc 指向的串
     * （ControlReturn 在 NextPc 之后还要用 f->pc 清理返回帧）。 */
    char fb_b[2][KV_KEY_MAX] __attribute__((aligned(4096)));
    int fb_i = 0;
    bool have_fb = false;
    kvlangKeytreeVthreadPc(vtid, pc_key, sizeof pc_key);
    kvlangKeytreeVthreadStatus(vtid, st_key, sizeof st_key);
    /* 执行期这两个 key 位置固定：keyoff 算一次，每步只读值（#329）。 */
    kvspaceKeyOff pc_off, status_off;
    kvspaceKeyOffBind(&pc_off, kv, pc_key);
    kvspaceKeyOffBind(&status_off, kv, st_key);
    /* status 只读 xvalue body view：绑定期取一次；-2 每句柄永久回退；-1 等首次读到
     * 非 None status 后仅重试一次。 */
    kvspaceXvalueBodyView_t status_view;
    bool status_view_ok = false;
    bool status_view_final = false;
    /* 每 128 步重新解析 keyoff 并比对 view 地址：识别 status 槽被删除/重建，
     * 同时把探测成本摊到 <2 cycles/step（不是每步重试）。 */
    unsigned status_recheck = 0;
    if (!kv->xvaluebodyview_unsupported) {
        int svr = kvlangXvalueBodyViewBind(kv, &status_off, &status_view);
        if (svr == 0)
            status_view_ok = true;
        else if (svr == -2)
            kv->xvaluebodyview_unsupported = true;
    }
    char fr_stk[KV_KEY_MAX];
    const char *cur_frame = NULL;
    /* 参数数组放堆上：ExecuteMode 只分配一次，避免每步栈帧过大加剧布局敏感。 */
    kvlangParam_t *reads = malloc(sizeof *reads * MAX_PARAMS);
    kvlangParam_t *writes = malloc(sizeof *writes * MAX_PARAMS);
    kvspaceKeyOff *write_offs[MAX_PARAMS];
    if (!reads || !writes)
        kvlangDie("out of memory allocating operand views");

    /* /lib 函数二维缓存（ExecuteMode 级）+ 每个活跃帧的绑定上下文。 */
    kvlangFuncCache_t fcache;
    kvlangFuncCacheInit(&fcache);
    kvlangFrameCtx_t *fctx[MAX_STACK_DEPTH + 1] = {0};
    kvlangFrameCtx_t **fpool =
        calloc((size_t)(MAX_STACK_DEPTH + 1) * FCTX_VARIANTS, sizeof *fpool);
    if (!fpool)
        kvlangDie("out of memory allocating frame contexts");
    uint32_t addr_epoch = 0; /* 删除/委托/handoff 后自增，使帧层 keyoff 失效 */
    int last_depth = 0; /* 0 = 尚未进入任何帧 */
    /* C-iii 游标：帧深/帧根/irseq 的派生缓存，仅在 PC 结构变化时重解析。 */
    int cursor_depth = 0;
    bool cursor_ok = false;
    kvlangExecCursor cursor = {0, true};

    /* P0-2：row/bind/inst/frame 模板跨步保留。参数数组只在本行首次（或换行）
     * 填静态字段 name/type/is_address，每步只刷新 value。 */
    kvlangCodeRow_t *row = NULL;
    kvlangRowBind_t *bind = NULL;
    kvlangCodeRow_t *tpl_row = NULL;
    kvlangRowBind_t *tpl_bind = NULL;
    kvlangRwirInst_t inst;
    memset(&inst, 0, sizeof inst);
    inst.reads = reads;
    inst.writes = writes;
    inst.write_offs = write_offs;
    kvlangPcViewState pc_view;
    kvlangPcViewInit(&pc_view);
    kvlangFrame_t f;
    memset(&f, 0, sizeof f);
    f.kvspace = kv;
    f.vthread_id = vtid;
    f.instruction = &inst;
    f.addr_epoch = &addr_epoch;
    f.pc_out_cap = sizeof fb_b[0];
    f.pc_off = &pc_off;
    f.status_off = &status_off;
    f.mode = (int)mode;
    f.out_pc = out_pc;
    f.cursor = &cursor;
    f.pc_view_state = &pc_view;

    const kvlangRwirCap *caps = kvlangRwirCaps();
    /* 快核开关：debug 日志需要逐步 trace，KVLANG_NOFAST=1 供 A/B。 */
    const char *nofast = getenv("KVLANG_NOFAST");
    const bool fast_on =
        kv->shm && !kvlangLogDebugOn && !(nofast && nofast[0] && nofast[0] != '0');
    int rc = 0;
    uint32_t qops = 0;
    for (;;) {
        /* 每 64 条指令一个静默点（handler 已返回，处于 op 边界）。 */
        if (kv->shm && !(++qops & 63))
            kvlangViewQuiesce(kv);
        if (!have_fb &&
            (!kvspaceKeyOffRead(&pc_off, cur_b, sizeof cur_b) || !cur_b[0])) {
            rc = -1;
            break;
        }
        const char *cur = have_fb ? fb_b[fb_i] : cur_b;
        have_fb = false;
        /* Observe external status changes on every step. */
        bool have_status = false;
        if (status_view_ok && ((++status_recheck & 127u) == 0)) {
            /* 周期重验：删除 → 丢弃 view 并安全回退；重建 → 采用新地址。 */
            kvspaceKeyOffBind(&status_off, kv, status_off.key);
            kvspaceXvalueBodyView_t nv;
            int nr = kvlangXvalueBodyViewBind(kv, &status_off, &nv);
            if (nr == 0)
                status_view = nv;
            else {
                status_view_ok = false;
                if (nr == -2)
                    kv->xvaluebodyview_unsupported = true;
            }
        }
        if (status_view_ok) {
            have_status =
                kvlangStatusXvalueBodyViewRead(&status_view, status_b, sizeof status_b);
            if (!have_status) {
                /* view 失效（槽被删/重建或 head 不自洽）：丢弃并仅重绑一次。 */
                status_view_ok = false;
                if (!status_view_final) {
                    status_view_final = true;
                    /* 槽可能已删除/重建：先重解析 keyoff，再取新 view。 */
                    kvspaceKeyOffBind(&status_off, kv, status_off.key);
                    int r = kvlangXvalueBodyViewBind(kv, &status_off, &status_view);
                    if (r == 0) {
                        status_view_ok = true;
                        have_status = kvlangStatusXvalueBodyViewRead(
                            &status_view, status_b, sizeof status_b);
                    } else if (r == -2) {
                        kv->xvaluebodyview_unsupported = true;
                    }
                }
            }
        }
        if (!have_status) {
            if (!kvspaceKeyOffRead(&status_off, status_b, sizeof status_b))
                break;
            /* 首次读到非 None status 后只重试一次绑定；成功/失败都定局。 */
            if (!status_view_final && !kv->xvaluebodyview_unsupported) {
                status_view_final = true;
                /* 槽已存在：强制重解析 keyoff（可能刚被删除/重建），再取 view。 */
                kvspaceKeyOffBind(&status_off, kv, status_off.key);
                int r = kvlangXvalueBodyViewBind(kv, &status_off, &status_view);
                if (r == 0)
                    status_view_ok = true;
                else if (r == -2)
                    kv->xvaluebodyview_unsupported = true;
            }
        }
        const char *status = status_b;
        if (strcmp(status, "running") != 0 && strcmp(status, "init") != 0 &&
            strcmp(status, "wait") != 0) {
            break;
        }

        int depth;
        int irseq;
        if (!cursor_ok) {
            depth = kvlangKeytreeFrameNum(cur);
            if (depth > MAX_STACK_DEPTH) {
                rc = execute_error(kv, vtid, cur,
                                   "RecursionError: stack overflow: depth=%d pc=%s",
                                   depth, cur);
                break;
            }
            if (depth != last_depth) {
                if (!kvlangKeytreeFrameRootBuffer(cur, fr_stk, sizeof fr_stk)) {
                    rc = execute_error(kv, vtid, cur,
                                       "RuntimeError: invalid pc: %s", cur);
                    break;
                }
                cur_frame = fr_stk;
                tpl_bind = NULL;
                tpl_row = NULL;
                if (switch_frame_ctx(kv, vtid, cur, cur_frame, depth,
                                     last_depth, addr_epoch, &fcache,
                                     fctx, fpool) != 0) {
                    rc = -1;
                    break;
                }
                last_depth = depth;
            }
            cursor_depth = depth;
            const char *lastc = NULL;
            for (const char *p = cur; (p = strstr(p, "/[")) != NULL; p += 2)
                lastc = p;
            cursor.irseq = lastc ? kvlangRwirExtractAddress(lastc + 1) : 0;
            /* 前缀（到末个 '/'，含）随游标缓存：顺序推进只重写数字尾。 */
            size_t plen = lastc ? (size_t)(lastc - cur) + 1 : 0;
            if (plen < sizeof cursor.prefix) {
                memcpy(cursor.prefix, cur, plen);
                cursor.prefix_len = (int)plen;
                cursor.prefix_ok = true;
            } else {
                cursor.prefix_ok = false;
            }
            cursor_ok = true;
        }
        depth = cursor_depth;
        irseq = cursor.irseq;
        kvlangFrameCtx_t *ctx = fctx[depth];
        /* P0-2：epoch 比较内联，只有世代变化时才做整帧失效。 */
        if (ctx->epoch != addr_epoch)
            kvlangFrameCtxSync(ctx, addr_epoch);

        char err[256];
        err[0] = 0;
        if ((size_t)irseq < ctx->code->rows_cap &&
            (size_t)irseq < ctx->binds_cap &&
            (row = ctx->code->rows[irseq]) != NULL &&
            ctx->binds[irseq].bound &&
            ctx->binds[irseq].reenter_gen == ctx->reenter_gen) {
            bind = &ctx->binds[irseq];
        } else {
            row = NULL;
            bind = kvlangFrameRow(kv, ctx, irseq, &row, err, sizeof err);
            tpl_bind = NULL;
        }
        if (!bind || !row->opcode || !row->opcode[0]) {
            /* layout 对每条路径都补了 return；走到空槽只能是 /lib 损坏或 goto/br 越界。 */
            if (err[0])
                rc = execute_error(kv, vtid, cur, "decode: %.247s", err);
            else
                rc = execute_error(kv, vtid, cur,
                                   "RuntimeError: no instruction at %s", cur);
            break;
        }

#ifdef KV_COUNT
        if (row->result_kind)
            kvCSpec++;
        else if (row->opcode_id >= 0)
            kvCGen++;
#endif
        /* 快核：本行及其后的可快行连续原地执行（每步仍持久化值/PC、查 status）。
         * 停在第一条不可快行；本步若一行都没执行则落回下方通用路径。 */
        if (fast_on && bind->fk_state != 1 && cursor.prefix_ok) {
            int fi = irseq;
            int ns = kvlangFastRun(ctx, &fi, &pc_off, &status_off,
                                   cursor.prefix, cursor.prefix_len,
                                   fb_b[fb_i ^ 1], sizeof fb_b[0]);
            if (ns > 0) {
                cursor.irseq = fi;
                fb_i ^= 1;
                have_fb = true;
                continue;
            }
        }
        int exec_err = 0;
        if (bind != tpl_bind || row != tpl_row) {
            row_template(&inst, reads, writes, write_offs, row, bind);
            tpl_bind = bind;
            tpl_row = row;
        }
        for (int i = 0; i < row->nr; i++) {
            kvlangBoundCell_t *c = &bind->cell[i];
            if (c->literal) {
                /* 字面量：值只在行内，无槽、无地址——name 为 NULL、
                 * is_address 保持 false，handler 按值参数处理。 */
                reads[i].value = *c->literal;
                continue;
            }
            int lr = operand_load(kv, c, row->view_pack, &reads[i].value);
            if (lr != 0) {
                exec_err =
                    lr == -2
                        ? execute_error(kv, vtid, cur,
                                        "TypeError: cannot read %s as %s",
                                        kvlangXvalueKind(&reads[i].value),
                                        c->type)
                        : execute_error(kv, vtid, cur,
                                        "RuntimeError: cannot read operand %d", i);
                break;
            }
            if (c->pin) /* 写槽会覆盖此 key：显式 copy 读视图 */
                kvlangXvalueCopy(&reads[i].value);
        }
        for (int j = 0; exec_err == 0 && j < row->nw; j++) {
            if (row->load_write_value) {
                int lr = operand_load(kv, &bind->cell[row->nr + j], 0,
                                      &writes[j].value);
                if (lr != 0) {
                    exec_err =
                        lr == -2
                            ? execute_error(
                                  kv, vtid, cur,
                                  "TypeError: cannot read %s as %s",
                                  kvlangXvalueKind(&writes[j].value),
                                  bind->cell[row->nr + j].type)
                            : execute_error(
                                  kv, vtid, cur,
                                  "RuntimeError: cannot read write slot %d", j);
                    break;
                }
            } else {
                kvlangXvalueZero(&writes[j].value); /* 不继承上一行的残留值 */
            }
        }
        if (exec_err != 0) {
            pinned_free(bind, row->nr, reads);
            rc = -1;
            break;
        }

        KV_LOG_DEBUG("[%s] PC=%s OP=%s R=%d W=%d", vtid, cur,
                       inst.opcode ? inst.opcode : "(empty)", inst.read_count,
                       inst.write_count);

        char *yield = NULL;
        f.pc = cur;
        f.frame_root = cur_frame;
        f.pc_out = fb_b[fb_i ^ 1];
        f.status_known = status; /* 本步开始前读到的 ‥status（源值，借用） */
        f.row = row;
        f.bind = bind;
        f.yield_pc = &yield;
        f.persist_failed = false;
        f.pc_feedback = false;
        /* 默认 dirty；只有 kvlangBuiltinNextPc 的顺序推进清位。 */
        cursor.dirty = true;
        /* 一次表调用：native 与 route（委托/def rwir/user call）共用同一出口。 */
        exec_err = caps[row->handler_id].fn(&f);
        if (f.persist_failed)
            exec_err = -1;
        if (cursor.jump)
            cursor.jump = false; /* goto/br 已把游标设为目标：不自增、不重解析 */
        else if (cursor.dirty)
            cursor_ok = false; /* 帧/irseq 结构可能变化：下步重新解析 */
        else
            cursor.irseq++; /* 顺序推进：只允许 BuiltinNextPc 走到这里 */
        bool pc_fb = f.pc_feedback;
        if (exec_err == 0 && yield) {
            /* vthread·run return 模式 / def rwir RETURN：冒泡子 rwir pc 给上层驱动。
             * 本 vthread（主）pc 未推进，驱动派发子 rwir 并推进子 pc 后重入即续跑。 */
            kvlangParkedFramesCleanupAbove(kv, vtid, depth);
            pinned_free(bind, row->nr, reads);
            free(reads);
            free(writes);
            fctx_pool_free(fpool);
            kvlangFuncCacheFree(&fcache);
            COUNT_REPORT();
            return execute_yield(out_pc, yield);
        }

        if (pc_fb) {
            fb_i ^= 1;
            have_fb = true;
        }
        pinned_free(bind, row->nr, reads);
        if (exec_err != 0) {
            rc = -1;
            break;
        }
    }

    kvlangParkedFramesCleanupAbove(kv, vtid, last_depth > 0 ? last_depth : 1);
    fctx_pool_free(fpool);
    kvlangFuncCacheFree(&fcache);
    COUNT_REPORT();
    free(reads);
    free(writes);
    return rc;
}

int kvlangKvcpuExecute(kvspace_t *kv, const char *pc) {
    int rc = kvlangKvcpuExecuteMode(kv, pc, KVMODE_WATCH, NULL);
    return rc == 1 ? 0 : rc; /* WATCH 模式不返回 1，防御性归一 */
}
