#include "func_code.h"
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

/* 读一个操作数目标值；None 合法。地址未解析时按 key 读并补一次绑定。 */
static int operand_load(kvspace_t *kv, kvlangBoundCell_t *cell,
                        kvlangXvalue_t *out) {
    kvlangXvalueZero(out);
    kvspaceKeyOff *ko = &cell->off;
    if (ko->resolved) {
        uint8_t *d = NULL;
        uint32_t len = 0;
        if (kvspaceGetByRef(kv->handle, &ko->off, cell->key, &d, &len) != 0)
            return -1;
        if (d && len > 0) {
            out->bytes = d;
            out->length = len;
        }
    } else {
        if (kvlangKvspaceGetOne(kv, cell->key, out) != 0)
            return -1;
        /* 变量可能在加载后才创建：补一次绑定，成功即切 ByRef。 */
        if (kvspaceResolveRef(kv->handle, cell->key, &ko->off) == 0)
            ko->resolved = true;
    }
    /* typed 读视图：声明 kind 与实际不同 → 转到 cell 复用缓冲（绑定期已备好文本）。 */
    if (cell->coerce_tid && out->bytes && out->length &&
        (xh_langtype_len_of(out) != cell->coerce_len ||
         memcmp(xh_langtype_of(out), cell->type, cell->coerce_len) != 0)) {
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
            "body_fallback=%lu body_cells_max=%lu cap_try=%lu cap_ok=%lu\n",
            kvCSpec, kvCGen, kvCNarrow, kvCCompare, kvCBodyFast,
            kvCBodyFallback, kvCBodyCellsMax, kvCBodyCapAttempt,
            kvCBodyCapOk);
}
#define COUNT_REPORT() count_report()
#else
#define COUNT_REPORT() do {} while (0)
#endif

/* 帧的 ‥lib：函数目录路径（owned）。 */
static char *frame_func_dir(kvspace_t *kv, const char *frame_root) {
    char *stack = kvlangKeytreeStack(frame_root);
    char key[KV_KEY_MAX];
    kvlangKeyFormat(key, sizeof key, "%s%s", stack, SEG_LIB);
    free(stack);
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    kvlangKvspaceGetOne(kv, key, &v);
    char *dir = kvlangXvalueNone(&v) ? NULL : kvlangXvalueValueString(&v);
    return dir;
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

    const kvlangRwirCap *caps = kvlangRwirCaps();
    int rc = 0;
    for (;;) {
        if (!have_fb &&
            (!kvspaceKeyOffRead(&pc_off, cur_b, sizeof cur_b) || !cur_b[0])) {
            rc = -1;
            break;
        }
        const char *cur = have_fb ? fb_b[fb_i] : cur_b;
        have_fb = false;
        /* Observe external status changes on every step. */
        if (!kvspaceKeyOffRead(&status_off, status_b, sizeof status_b))
            break;
        const char *status = status_b;
        if (strcmp(status, "init") != 0 && strcmp(status, "running") != 0 &&
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
                /* 帧变：变深=call（新帧）；变浅=return（更深的帧已删，本帧可复用）。 */
                if (!kvlangKeytreeFrameRootBuffer(cur, fr_stk, sizeof fr_stk)) {
                    rc = execute_error(kv, vtid, cur,
                                       "RuntimeError: invalid pc: %s", cur);
                    break;
                }
                cur_frame = fr_stk;
                for (int d = depth + 1; d <= last_depth && d <= MAX_STACK_DEPTH;
                     d++) {
                    kvlangFrameCtxFree(fctx[d]);
                    fctx[d] = NULL;
                }
                if (fctx[depth] &&
                    strcmp(fctx[depth]->frame_root, cur_frame) != 0) {
                    kvlangFrameCtxFree(fctx[depth]);
                    fctx[depth] = NULL;
                }
                /* 模板里的 name/type/off 借用自 ctx 的 bind：ctx 释放即作废。 */
                tpl_bind = NULL;
                tpl_row = NULL;
                if (!fctx[depth]) {
                    char *func_dir = frame_func_dir(kv, cur_frame);
                    if (!func_dir) {
                        rc = execute_error(kv, vtid, cur,
                                           "RuntimeError: frame has no ‥lib: %s",
                                           cur_frame);
                        break;
                    }
                    fctx[depth] = kvlangFrameCtxNew(&fcache, func_dir, cur_frame);
                    free(func_dir);
                }
                last_depth = depth;
            }
            cursor_depth = depth;
            const char *lastc = NULL;
            for (const char *p = cur; (p = strstr(p, "/[")) != NULL; p += 2)
                lastc = p;
            cursor.irseq = lastc ? kvlangRwirExtractAddress(lastc + 1) : 0;
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
        /* P0-2：目标行已加载/已绑定 → 直接复用 row/bind，不再走
         * kvlangFrameRow（省一次函数调用与双重边界检查）。 */
        if ((size_t)irseq < ctx->code->rows_cap &&
            (size_t)irseq < ctx->binds_cap &&
            (row = ctx->code->rows[irseq]) != NULL &&
            ctx->binds[irseq].bound) {
            bind = &ctx->binds[irseq];
        } else {
            row = NULL;
            bind = kvlangFrameRow(kv, ctx, irseq, &row, err, sizeof err);
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
            int lr = operand_load(kv, c, &reads[i].value);
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
                int lr = operand_load(kv, &bind->cell[row->nr + j],
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
        f.yield_pc = &yield;
        f.persist_failed = false;
        f.pc_feedback = false;
        /* 默认 dirty；只有 kvlangBuiltinNextPc 的顺序推进清位。 */
        cursor.dirty = true;
        /* 一次表调用：native 与 route（委托/def rwir/user call）共用同一出口。 */
        exec_err = caps[row->handler_id].fn(&f);
        if (f.persist_failed)
            exec_err = -1;
        if (cursor.dirty)
            cursor_ok = false; /* 帧/irseq 结构可能变化：下步重新解析 */
        else
            cursor.irseq++; /* 顺序推进：只允许 BuiltinNextPc 走到这里 */
        bool pc_fb = f.pc_feedback;
        if (exec_err == 0 && yield) {
            /* vthread·run return 模式 / def rwir RETURN：冒泡子 rwir pc 给上层驱动。
             * 本 vthread（主）pc 未推进，驱动派发子 rwir 并推进子 pc 后重入即续跑。 */
            pinned_free(bind, row->nr, reads);
            free(reads);
            free(writes);
            for (int d = 0; d <= MAX_STACK_DEPTH; d++)
                kvlangFrameCtxFree(fctx[d]);
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

    for (int d = 0; d <= MAX_STACK_DEPTH; d++)
        kvlangFrameCtxFree(fctx[d]);
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
