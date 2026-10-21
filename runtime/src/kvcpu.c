#include "func_code.h"
#include "kvcpu_internal.h"

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
    return 0;
}

/* pin 副本是本指令为「会被写槽覆盖」的读值做的显式 copy，执行完即释放。 */
static void pinned_free(kvlangRowBind_t *bind, int nr, kvlangParam_t *reads) {
    if (!bind->has_pin)
        return;
    for (int i = 0; i < nr; i++)
        if (bind->cell[i].pin && !bind->cell[i].literal)
            kvlangXvalueFree(&reads[i].value);
}

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

int kvlangKvcpuExecuteMode(kvspace_t *kv, const char *pc, kvmode_t mode,
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

    int rc = 0;
    for (;;) {
        /* Release borrowed values from the previous instruction. */
        kvlangKvspaceReadReset(kv);
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

        int depth = kvlangKeytreeFrameNum(cur);
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
        kvlangFrameCtxSync(fctx[depth], addr_epoch);

        /* 函数二维表：PC 末坐标即 irseq，直接索引；行/绑定按需加载。 */
        const char *lastc = NULL;
        for (const char *p = cur; (p = strstr(p, "/[")) != NULL; p += 2)
            lastc = p;
        int irseq = lastc ? kvlangRwirExtractAddress(lastc + 1) : 0;
        char err[256];
        kvlangCodeRow_t *row = NULL;
        kvlangRowBind_t *bind =
            kvlangFrameRow(kv, fctx[depth], irseq, &row, err, sizeof err);
        if (!bind || !row->opcode || !row->opcode[0]) {
            /* layout 对每条路径都补了 return；走到空槽只能是 /lib 损坏或 goto/br 越界。 */
            if (err[0])
                rc = execute_error(kv, vtid, cur, "decode: %.247s", err);
            else
                rc = execute_error(kv, vtid, cur,
                                   "RuntimeError: no instruction at %s", cur);
            break;
        }

        int exec_err = 0;
        for (int i = 0; i < row->nr; i++)
            reads[i] = (kvlangParam_t){0};
        for (int j = 0; j < row->nw; j++)
            writes[j] = (kvlangParam_t){0};
        for (int i = 0; i < row->nr; i++) {
            reads[i].name = bind->cell[i].key;
            reads[i].type = bind->cell[i].type;
            reads[i].is_address = true;
            if (bind->cell[i].literal) {
                /* 字面量：值已在行内（帧绑定时已落回 operand 槽，路径语义不变），
                 * 直接借用，省掉每步一次 Get。 */
                reads[i].value = *bind->cell[i].literal;
                continue;
            }
            if (operand_load(kv, &bind->cell[i], &reads[i].value) != 0) {
                exec_err = execute_error(
                    kv, vtid, cur, "RuntimeError: cannot read operand %d", i);
                break;
            }
            if (bind->cell[i].pin) /* 写槽会覆盖此 key：显式 copy 读视图 */
                kvlangXvalueCopy(&reads[i].value);
        }
        for (int j = 0; exec_err == 0 && j < row->nw; j++) {
            writes[j].name = bind->cell[row->nr + j].key;
            writes[j].type = bind->cell[row->nr + j].type;
            writes[j].is_address = true;
            if (row->load_write_value &&
                operand_load(kv, &bind->cell[row->nr + j], &writes[j].value) !=
                    0) {
                exec_err =
                    execute_error(kv, vtid, cur,
                                  "RuntimeError: cannot read write slot %d", j);
                break;
            }
        }
        if (exec_err != 0) {
            rc = -1;
            break;
        }

        for (int j = 0; j < row->nw; j++)
            write_offs[j] = &bind->cell[row->nr + j].off;
        kvlangRwirInst_t inst = {row->opcode, row->opcode_id, reads,
                                 row->nr,     writes,         row->nw,
                                 row->nw ? write_offs : NULL};
        kvlangLogDebug("[%s] PC=%s OP=%s R=%d W=%d", vtid, cur,
                       inst.opcode ? inst.opcode : "(empty)", inst.read_count,
                       inst.write_count);

        char *yield = NULL;
        bool pc_fb = false;
        if (inst.opcode_id >= 0) {
            /* 单表派发：native 算子与 control/copy 同居 myrwircaps，opcode_id 直查一跳到底。 */
            kvlangFrame_t f = {kv, vtid, cur, &inst, &yield};
            f.frame_root = cur_frame;
            f.addr_epoch = &addr_epoch;
            f.pc_out = fb_b[fb_i ^ 1];
            f.pc_out_cap = sizeof fb_b[0];
            f.status_known =
                status; /* 本步开始前读到的 ‥status（源值，借用） */
            exec_err = kvlangBuiltinNative(&f);
            if (f.persist_failed)
                exec_err = -1;
            pc_fb = f.pc_feedback;
            if (exec_err == 0 && yield) {
                /* native（vthread·run return 模式）冒泡一个子 vthread 的 rwir pc 给上层驱动。
                 * 本 vthread（主）pc 未推进，驱动派发子 rwir 并推进子 pc 后重入即续跑。 */
                pinned_free(bind, row->nr, reads);
                free(reads);
                free(writes);
                for (int d = 0; d <= MAX_STACK_DEPTH; d++)
                    kvlangFrameCtxFree(fctx[d]);
                kvlangFuncCacheFree(&fcache);
                return execute_yield(out_pc, yield);
            }
        } else if (kvlangDispatchIsDelegatedOp(kv, inst.opcode)) {
            exec_err = kvlangDispatchDelegate(kv, vtid, cur, &inst);
            if (exec_err == KVLANG_DELEGATE_LOCAL) {
                exec_err = 0;
                goto user_call;
            }
            addr_epoch++; /* 委托会删除输出槽 */
        } else if (row->is_def_rwir) {
            if (row->def_read_sig)
                exec_err = check_read_types(
                    kv, vtid, cur, inst.opcode, row->def_read_sig,
                    row->def_read_count, row->def_dynamic, inst.reads,
                    inst.read_count);
            if (exec_err == 0 && row->def_write_sig)
                exec_err =
                    check_write_types(kv, vtid, cur, inst.opcode,
                                      row->def_write_sig, row->def_write_count);
            if (exec_err == 0 && mode == KVMODE_RETURN) {
                pinned_free(bind, row->nr, reads);
                free(reads);
                free(writes);
                for (int d = 0; d <= MAX_STACK_DEPTH; d++)
                    kvlangFrameCtxFree(fctx[d]);
                kvlangFuncCacheFree(&fcache);
                return execute_yield(out_pc, out_pc ? strdup(cur) : NULL);
            }
            if (exec_err == 0)
                exec_err = kvlangHandoffExternalRwir(kv, vtid, cur, &inst);
            addr_epoch++; /* 外部执行器可能删除/重建 key */
        } else {
        user_call:
            exec_err = kvlangKvcpuCallOpcode(
                kv, vtid, cur, status, inst.opcode, inst.reads,
                inst.read_count, inst.writes, inst.write_count, fb_b[fb_i ^ 1],
                sizeof fb_b[0], &pc_fb);
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
    free(reads);
    free(writes);
    return rc;
}

int kvlangKvcpuExecute(kvspace_t *kv, const char *pc) {
    int rc = kvlangKvcpuExecuteMode(kv, pc, KVMODE_WATCH, NULL);
    return rc == 1 ? 0 : rc; /* WATCH 模式不返回 1，防御性归一 */
}
