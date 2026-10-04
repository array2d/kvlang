#include "runtime_internal.h"

static char *load_def_reads(kvlangKv_t *kv, const char *key, int *out_nr,
                            int *out_dyn);

/* 找末个成员分隔符（·，多字节），对齐 strrchr('.') 的单字节旧语义。 */
static const char *rfind_sep(const char *s) {
    const char *found = NULL, *p = s;
    while ((p = strstr(p, MEMBER_SEP)) != NULL) {
        found = p;
        p += MEMBER_SEP_LEN;
    }
    return found;
}

/* goto/br 目标：layout 已把 label 解析为 int64 irseq（≥1）。非 int64 / 越界返回 -1。 */
static int irseq_of(const kvlangParam_t *p) {
    if (kvlangXvalueNone(&p->val) ||
        !kvlangXvalueKindIs(&p->val, KVSPACE_KIND_INT64))
        return -1;
    int64_t n = kvlangScalarI64(kvlangXvalueScalar(&p->val));
    if (n < 1 || n > 0x7fffffff)
        return -1;
    return (int)n;
}

/* 函数内跳转：只改 PC 的 [irseq]，帧不变。目标非法 → RuntimeError，返回 -1。 */
static int jump_to(kvlangFrame_t *f, const kvlangParam_t *target, const char *op) {
    int irseq = irseq_of(target);
    if (irseq < 0) {
        char msg[256];
        snprintf(msg, sizeof msg,
                 "RuntimeError: %s target is not an int64 irseq: %s (kind=%s)",
                 op, target->name ? target->name : "",
                 kvlangXvalueKind(&target->val));
        kvlangVthreadSetError(f->kv, f->vtid, f->pc, msg);
        return -1;
    }
    /* Reuse this instruction's frame root. */
    char *owned = NULL;
    const char *fr = f->frame_root;
    if (!fr) { owned = kvlangKeytreeFrameRoot(f->pc); fr = owned; }
    char *np = kvlangKeytreeIrseqPc(fr, irseq);
    free(owned);
    kvlangVthreadAdvance(f, np, "running");
    kvlangLogDebug("[%s] %s → %s", f->vtid, op, np);
    free(np);
    return 0;
}

static bool is_literal(const char *s) {
    if (!s || !s[0])
        return false;
    return s[0] == '"' || strcmp(s, "true") == 0 ||
           strcmp(s, "false") == 0 || strcmp(s, "null") == 0 ||
           (s[0] >= '0' && s[0] <= '9') ||
           (s[0] == '-' && s[1] >= '0' && s[1] <= '9');
}

/* 派发期读参类型校验（runtime篇-07 第八节）：把每个实参的 kind 逐一匹配
 * rwir/rwfunc 定义的读参 kindexp。def_sig 为读参 kindexp 在前的 \n 分隔列表，
 * def_nr 为定义读参数，dynamic=1 表末读参变参吸收其后全部实参。空 kindexp / any 跳过。
 * XValue 头只携带 array_len 不含多维 shape，故仅校验 kind 层。
 * 不匹配 → 置 TypeError，返回 -1；通过返回 0。 */
static int check_read_types(kvlangKv_t *kv, const char *vtid, const char *pc,
                            const char *opcode, const char *def_sig, int def_nr,
                            int dynamic, kvlangParam_t *args, int nargs) {
    if (def_nr <= 0 || !def_sig || !*def_sig)
        return 0;
    char *dup = strdup(def_sig);
    char *reads[128];
    int rn = 0;
    for (char *s = dup; rn < def_nr && rn < 128;) {
        reads[rn++] = s;
        char *nl = strchr(s, '\n');
        if (!nl)
            break;
        *nl = 0;
        s = nl + 1;
    }
    bool var_last = rn > 0 && dynamic;
    int min_args = var_last ? rn - 1 : rn;
    char *fr = kvlangKeytreeFrameRoot(pc);
    int rc = 0;
    if (nargs < min_args) {
        char msg[256];
        snprintf(msg, sizeof msg, "TypeError: %s expects %d args, got %d",
                 opcode, min_args, nargs);
        kvlangVthreadSetError(kv, vtid, pc, msg);
        rc = -1;
    }
    for (int i = 0; rc == 0 && i < nargs; i++) {
        const char *exp = i < rn ? reads[i] : (var_last ? reads[rn - 1] : NULL);
        if (!exp) {
            char msg[256];
            snprintf(msg, sizeof msg, "TypeError: %s expects %d args, got %d",
                     opcode, rn, nargs);
            kvlangVthreadSetError(kv, vtid, pc, msg);
            rc = -1;
            break;
        }
        if (!exp[0] || !kvlangLangtypeValid(exp))
            continue; /* 动态/非法 kindexp 跳过 */
        kvlangXvalue_t v;
        kvlangXvalueZero(&v);
        kvlangBuiltinResolveReadValue(kv, fr, args[i].name, &args[i].val, &v);
        const char *k = kvlangXvalueKind(&v);
        kvspaceHead_t h;
        kvlangXvalueHead(&v, &h);
        kvlangLangtype kx;
        kvlangLangtypeParse(h.langtype, &kx);
        bool ok = kvlangLangtypeMatch(exp, k, kx.ndim, kx.dims);
        char kbuf[40];
        snprintf(kbuf, sizeof kbuf, "%s", k[0] ? k : "None");
        kvlangXvalueFree(&v);
        if (!ok) {
            char msg[256];
            snprintf(msg, sizeof msg,
                     "TypeError: %s arg %d: expected %s, got %s", opcode, i + 1,
                     exp, kbuf);
            kvlangVthreadSetError(kv, vtid, pc, msg);
            rc = -1;
        }
    }
    free(fr);
    free(dup);
    return rc;
}

/* 读签名行 [0,x] 槽（def langtype）的 body 为 langtype 串（malloc；无槽返 NULL）。
 * dir 带尾 /；x<0 读参、x>0 写参。 */
static char *read_sig_slot(kvlangKv_t *kv, const char *dir, int x) {
    kvlangStrbuf_t sk;
    kvlangStrbufInit(&sk);
    kvlangStrbufPrintf(&sk, "%s[0,%d]", dir, x);
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    kvlangKvGetOne(kv, sk.p, &v);
    kvlangStrbufFree(&sk);
    char *s = NULL;
    if (!kvlangXvalueNone(&v)) {
        kvspaceHead_t h;
        kvlangXvalueHead(&v, &h);
        int32_t bl;
        const uint8_t *b = kvlangXvalueBody(&v, &h, &bl);
        s = malloc((size_t)bl + 1);
        memcpy(s, b, (size_t)bl);
        s[bl] = 0;
    }
    kvlangXvalueFree(&v);
    return s;
}

/* 拼读参签名（\n 连接 [0,-1..-nr] 各 def langtype 槽）。dir 带尾 /。malloc 返回。 */
static char *join_read_sig(kvlangKv_t *kv, const char *dir, int nr) {
    kvlangStrbuf_t b;
    kvlangStrbufInit(&b);
    for (int i = 1; i <= nr; i++) {
        if (i > 1)
            kvlangStrbufPutc(&b, '\n');
        char *s = read_sig_slot(kv, dir, -i);
        kvlangStrbufPuts(&b, s ? s : "");
        free(s);
    }
    return kvlangStrbufDetach(&b);
}

/* 读取 rwir/rwfunc 定义的读参签名：从主槽计数头取 nr/dynamic，读参 langtype 逐条
 * 落在签名行 [0,-i] 槽（def langtype）。返回 \n 连接的 kindexp-list（调用方 free）
 * 并置 *out_nr；无定义返回 NULL。 */
static char *load_def_reads(kvlangKv_t *kv, const char *key, int *out_nr,
                            int *out_dyn) {
    *out_nr = 0;
    *out_dyn = 0;
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    kvlangKvGetOne(kv, key, &v);
    if (kvlangXvalueNone(&v)) {
        kvlangXvalueFree(&v);
        return NULL;
    }
    kvspaceHead_t h;
    kvlangXvalueHead(&v, &h);
    int32_t bl;
    const uint8_t *b = kvlangXvalueBody(&v, &h, &bl);
    if (bl < 5) {
        kvlangXvalueFree(&v);
        return NULL;
    }
    *out_nr = b[0] | (b[1] << 8);
    *out_dyn = b[4];
    kvlangXvalueFree(&v);
    kvlangStrbuf_t dir;
    kvlangStrbufInit(&dir);
    kvlangStrbufPrintf(&dir, "%s/", key);
    char *sig = join_read_sig(kv, dir.p, *out_nr);
    kvlangStrbufFree(&dir);
    return sig;
}

static char *frame_slot_key(const char *frame_root, const char *slot) {
    if (!slot || !slot[0])
        return NULL;
    if (slot[0] == '/')
        return strdup(slot);
    if (strncmp(slot, MEMBER_SEP, MEMBER_SEP_LEN) == 0)
        return NULL;
    kvlangStrbuf_t b;
    kvlangStrbufInit(&b);
    char *stk = kvlangKeytreeStack(frame_root);
    kvlangStrbufPuts(&b, stk);
    free(stk);
    kvlangStrbufPuts(&b, slot);
    return kvlangStrbufDetach(&b);
}

/* Resolve a caller operand to its KVSpace key. */
static char *resolve_read_path(kvlangKv_t *kv, const char *frame_root,
                               const char *name) {
    if (is_literal(name))
        return NULL;
    if (name[0] == '*')
        return kvlangBuiltinResolveWriteSlot(kv, frame_root, name);
    return frame_slot_key(frame_root, name);
}

static int handle_return(kvlangKv_t *kv, const char *vtid, const char *pc,
                         char **out_next) {
    *out_next = NULL;
    char *fr = kvlangKeytreeFrameRoot(pc);
    int d = kvlangKeytreeFrameNum(fr);
    char *next = NULL;
    if (d > 1) {
        kvlangStrbuf_t rk;
        kvlangStrbufInit(&rk);
        kvlangKeytreeFrameReturnpc(fr, &rk);
        kvlangXvalue_t v;
        kvlangXvalueZero(&v);
        kvlangKvGetOne(kv, rk.p, &v);
        if (!kvlangXvalueNone(&v))
            next = kvlangXvalueValueString(&v);
        kvlangXvalueFree(&v);
        kvlangStrbufFree(&rk);
        if (!next || !next[0]) {
            char msg[512];
            snprintf(msg, sizeof msg,
                     "RuntimeError: broken return chain: frame %s has no "
                     "returnpc (pc=%s)",
                     fr, pc);
            kvlangVthreadSetError(kv, vtid, pc, msg);
            free(next);
            free(fr);
            return -1;
        }
    }
    free(fr);
    *out_next = next;
    return 0;
}

static void param_decl_type(kvlangKv_t *kv, const char *func_key, int x,
                            char *lt, size_t cap);

#define MAX_BIND_PAIRS (MAX_PARAMS * 5)

static int add_param_binding(kvlangKvPair_t *pairs, int *np,
                             const char *frame_root, int x, const char *type,
                             const char *value_key) {
    if (!type[0] || !value_key || *np > MAX_BIND_PAIRS - 2)
        return -1;
    while (*type == '*' || *type == '@')
        type++;
    if (!type[0])
        return -1;
    kvlangStrbuf_t arg;
    kvlangStrbufInit(&arg);
    kvlangStrbufPrintf(&arg, "%s/%sarg[0,%d]", frame_root, RUNTIME_MEMBER_SEP, x);
    char *arg_key = kvlangStrbufDetach(&arg);
    kvlangStrbuf_t slot;
    kvlangStrbufInit(&slot);
    kvlangStrbufPrintf(&slot, "%s/[0,%d]", frame_root, x);
    char *slot_key = kvlangStrbufDetach(&slot);
    kvlangXvalue_t arg_ptr, frame_ptr;
    kvlangXvalueNewPtr(&arg_ptr, type, value_key);
    kvlangXvalueNewPtr(&frame_ptr, type, arg_key);
    if (kvlangXvalueNone(&arg_ptr) || kvlangXvalueNone(&frame_ptr)) {
        kvlangXvalueFree(&arg_ptr);
        kvlangXvalueFree(&frame_ptr);
        free(arg_key);
        free(slot_key);
        return -1;
    }
    pairs[(*np)++] = (kvlangKvPair_t){arg_key, arg_ptr};
    pairs[(*np)++] = (kvlangKvPair_t){slot_key, frame_ptr};
    return 0;
}

/* Create the callee frame and return its entry PC. */
static char *handle_call(kvlangKv_t *kv, const char *pc,
                         kvlangRwirInst_t *inst) {
    kvlangStrbuf_t vtid_b;
    kvlangStrbufInit(&vtid_b);
    const char *vtid = kvlangKeytreeVtidFromPc(pc, &vtid_b);
    const char *fn = inst->reads[0].name;
    char *pkg = strdup("");
    char *name = strdup(fn);
    const char *lp = "/lib/";
    if (strncmp(fn, lp, 5) == 0) {
        const char *rest = fn + 5;
        const char *dot = rfind_sep(rest);
        if (dot) {
            free(pkg);
            pkg = strndup(rest, (size_t)(dot - rest));
            free(name);
            name = strdup(dot + MEMBER_SEP_LEN);
        } else {
            free(name);
            name = strdup(rest);
        }
    } else {
        const char *dot = rfind_sep(fn);
        if (dot) {
            free(pkg);
            pkg = strndup(fn, (size_t)(dot - fn));
            free(name);
            name = strdup(dot + MEMBER_SEP_LEN);
        } else {
            /* 裸名调用（无 /lib/ 无 ·）：同 pkg 优先——当前函数所在 lib 下有同名 rwfunc 就用之
             * （lib aaa/bbb/math 内 sum(A,A) → /lib/aaa/bbb/math·sum），否则退回根 /lib/<fn>。 */
            char *ff = kvlangKeytreeFrameRoot(pc);
            if (ff) {
                /* 函数目录在帧的 ‥lib 槽（/lib/aaa/bbb/math·double/）：由此取调用者 pkg。 */
                kvlangStrbuf_t lk;
                kvlangStrbufInit(&lk);
                char *stk = kvlangKeytreeStack(ff);
                kvlangStrbufPuts(&lk, stk);
                free(stk);
                kvlangStrbufPuts(&lk, SEG_LIB);
                kvlangXvalue_t lv;
                kvlangXvalueZero(&lv);
                kvlangKvGetOne(kv, lk.p, &lv);
                kvlangStrbufFree(&lk);
                char *funcdir =
                    kvlangXvalueNone(&lv) ? NULL : kvlangXvalueValueString(&lv);
                kvlangXvalueFree(&lv);
                if (funcdir) {
                    char *rel = funcdir + 5; // 剥 /lib/
                    size_t rl = strlen(rel);
                    if (rl > 0 && rel[rl - 1] == '/')
                        rel[rl - 1] = '\0'; // 剥尾 /
                    const char *sep = rfind_sep(rel);
                    if (sep) {
                        char *cand_pkg = strndup(rel, (size_t)(sep - rel));
                        char *cand = kvlangKeytreeLibFunc(cand_pkg, fn);
                        kvlangStrbuf_t sk;
                        kvlangStrbufInit(&sk);
                        kvlangStrbufPrintf(&sk, "%s/[0,0]", cand);
                        kvlangXvalue_t sv;
                        kvlangXvalueZero(&sv);
                        kvlangKvGetOne(kv, sk.p, &sv);
                        bool ok = !kvlangXvalueNone(&sv) &&
                                  kvlangXvalueKindIs(&sv, KVSPACE_KIND_RWFUNC);
                        kvlangXvalueFree(&sv);
                        kvlangStrbufFree(&sk);
                        if (ok) {
                            free(pkg);
                            pkg = cand_pkg;
                        } else
                            free(cand_pkg);
                        free(cand);
                    }
                    free(funcdir);
                }
                free(ff);
            }
        }
    }
    char *func_key = kvlangKeytreeLibFunc(pkg, name);
    kvlangStrbuf_t func_dir;
    kvlangStrbufInit(&func_dir);
    kvlangStrbufPuts(&func_dir, func_key);
    kvlangStrbufPutc(&func_dir, '/');

    kvlangStrbuf_t sig_key;
    kvlangStrbufInit(&sig_key);
    kvlangStrbufPrintf(&sig_key, "%s[0,0]", func_dir.p);
    kvlangXvalue_t sig;
    kvlangXvalueZero(&sig);
    kvlangKvGetOne(kv, sig_key.p, &sig);
    if (kvlangXvalueNone(&sig) ||
        !kvlangXvalueKindIs(&sig, KVSPACE_KIND_RWFUNC)) {
        /* 按 xvalue 的 kind 精确区分缺 rwir 还是缺 rwfunc：
         * 到这里说明 opcode 已被 notinmyrwircaps 判否（/lib/<op> 非 def rwir 路由头）。 */
        char *rk = kvlangKeytreeRwir(fn);
        kvlangXvalue_t rv;
        kvlangXvalueZero(&rv);
        kvlangKvGetOne(kv, rk, &rv);
        char msg[256];
        if (!kvlangXvalueNone(&rv) &&
            kvlangXvalueKindIs(&rv, KVSPACE_KIND_DEF_RWIR))
            snprintf(msg, sizeof msg, "NameError: rwir 未注册/签名不匹配: %s",
                     fn);
        else if (!kvlangXvalueNone(&sig))
            snprintf(msg, sizeof msg, "NameError: %s 不是 rwfunc (kind=%s)", fn,
                     kvlangXvalueKind(&sig));
        else
            snprintf(msg, sizeof msg, "NameError: rwfunc not found: %s", fn);
        kvlangXvalueFree(&rv);
        free(rk);
        kvlangVthreadSetError(kv, vtid, pc, msg);
        goto fail;
    }
    kvspaceHead_t h;
    kvspaceDecodeHead(sig.data, sig.len, &h);
    const uint8_t *sbody = sig.data + h.body_offset;
    int nr = sbody[0] | (sbody[1] << 8);
    int nw = sbody[2] | (sbody[3] << 8);
    if (nr > MAX_PARAMS || nw > MAX_PARAMS) {
        kvlangVthreadSetError(kv, vtid, pc, "TypeError: too many parameters");
        goto fail;
    }

    /* rwfunc 不做派发期位置化读参校验：类型随命名参数键 funcDir/<name>（Ptr 的
     * target_langtype）承载，函数体在帧内按名解析、各 native 算子在使用点自校验操作数
     * kind。位置化的有序签名校验（join_read_sig+check_read_types）是 rwir 的需求——
     * 跨 runtime 队列按位置收参，故仅 def rwir 路由路径保留（见本文件 m->def_sig）。 */

    char *caller_fr = kvlangKeytreeFrameRoot(pc);
    int d = kvlangKeytreeFrameNum(pc);
    char *frame_root = kvlangKeytreeFrameAt(vtid, d + 1);
    char err[256];
    kvlangKvDelTree(kv, frame_root, err, sizeof err);
    char *stack_fr = kvlangKeytreeStack(frame_root);
    if (kvlangKvMkindex(kv, stack_fr, 0, err, sizeof err) != 0 ||
        kvlangKvExtIndex(kv, stack_fr, func_dir.p, err, sizeof err) != 0) {
        kvlangVthreadSetError(kv, vtid, pc, err);
        free(caller_fr);
        free(stack_fr);
        free(frame_root);
        goto fail;
    }

    /* 系统变量 */
    kvlangStrbuf_t npc;
    kvlangStrbufInit(&npc);
    kvlangRwirNextPc(pc, &npc);
    kvlangStrbuf_t retpc;
    kvlangStrbufInit(&retpc);
    kvlangKeytreeFrameReturnpc(frame_root, &retpc);
    kvlangStrbuf_t callpc;
    kvlangStrbufInit(&callpc);
    kvlangKeytreeFrameCallpc(frame_root, &callpc);
    char *ep = kvlangKeytreeEntryPc(frame_root);
    kvlangStrbuf_t seglib;
    kvlangStrbufInit(&seglib);
    kvlangStrbufPuts(&seglib, stack_fr);
    kvlangStrbufPuts(&seglib, SEG_LIB);
    kvlangXvalue_t v_npc, v_ep, v_fn;
    kvlangXvalueZero(&v_npc);
    kvlangXvalueZero(&v_ep);
    kvlangXvalueZero(&v_fn);
    kvlangXvalueNewCharUtf8(&v_npc, npc.p);
    kvlangXvalueNewCharUtf8(&v_ep, ep);
    kvlangXvalueNewCharUtf8(&v_fn, func_key);
    kvlangKvPair_t sys[3] = {
        {retpc.p, v_npc}, {callpc.p, v_ep}, {seglib.p, v_fn}};
    int sys_rc = kvlangXvalueNone(&v_npc) || kvlangXvalueNone(&v_ep) ||
                 kvlangXvalueNone(&v_fn) ? -1 :
                 kvlangKvSet(kv, sys, 3, err, sizeof err);
    kvlangXvalueFree(&v_npc);
    kvlangXvalueFree(&v_ep);
    kvlangXvalueFree(&v_fn);
    if (sys_rc != 0) {
        kvlangVthreadSetError(kv, vtid, pc, "RuntimeError: cannot persist call frame");
        goto fail_frame;
    }

    kvlangKvPair_t pairs[MAX_BIND_PAIRS];
    int np = 0;
    int bind_error = 0;
    for (int i = 0; i < nr && !bind_error; i++) {
        if (i + 1 >= inst->nr) {
            snprintf(err, sizeof err, "TypeError: missing read argument %d", i + 1);
            bind_error = 1;
            break;
        }
        kvlangParam_t *arg = &inst->reads[i + 1];
        char lt[256] = {0};
        param_decl_type(kv, func_key, -(i + 1), lt, sizeof lt);
        bool concrete = !arg->address && !kvlangXvalueNone(&arg->val) &&
                        !kvlangXvalueKindIs(&arg->val, KVSPACE_KIND_RWIR) &&
                        !kvlangXvalueKindIs(&arg->val, KVSPACE_KIND_RWFUNC);
        bool literal = concrete || is_literal(arg->name);
        char *rk = literal ? NULL : resolve_read_path(kv, caller_fr, arg->name);
        if (literal) {
            kvlangStrbuf_t lit_path;
            kvlangStrbufInit(&lit_path);
            kvlangStrbufPrintf(&lit_path, "%s/._lit%d", frame_root, i);
            rk = kvlangStrbufDetach(&lit_path);
            if (concrete) {
                if (np >= MAX_BIND_PAIRS) {
                    free(rk);
                    snprintf(err, sizeof err, "TypeError: too many arguments");
                    bind_error = 1;
                    break;
                }
                kvlangXvalue_t copy;
                kvlangXvalueZero(&copy);
                copy.data = malloc(arg->val.len);
                if (!copy.data) {
                    free(rk);
                    snprintf(err, sizeof err, "OutOfMemory: argument %d", i + 1);
                    bind_error = 1;
                    break;
                }
                memcpy(copy.data, arg->val.data, arg->val.len);
                copy.len = arg->val.len;
                pairs[np++] = (kvlangKvPair_t){strdup(rk), copy};
            }
        }
        if (add_param_binding(pairs, &np, frame_root, -(i + 1), lt, rk) != 0) {
            snprintf(err, sizeof err, "TypeError: cannot bind read argument %d", i + 1);
            bind_error = 1;
        }
        free(rk);
    }
    for (int i = 0; i < nw && !bind_error; i++) {
        char lt[256] = {0};
        param_decl_type(kv, func_key, i + 1, lt, sizeof lt);
        char *wk = i < inst->nw ?
            resolve_read_path(kv, caller_fr, inst->writes[i].name) : NULL;
        if (add_param_binding(pairs, &np, frame_root, i + 1, lt, wk) != 0) {
            snprintf(err, sizeof err, "TypeError: cannot bind write argument %d", i + 1);
            bind_error = 1;
        }
        free(wk);
    }
    int wrc = bind_error ? -1 : np > 0 ? kvlangKvSet(kv, pairs, np, err, sizeof err) : 0;
    for (int i = 0; i < np; i++) {
        free(pairs[i].key);
        kvlangXvalueFree(&pairs[i].val);
    }
    if (wrc != 0) {
        kvlangVthreadSetError(kv, vtid, pc, err);
        goto fail_frame;
    }

    free(caller_fr);
    free(stack_fr);
    kvlangStrbufFree(&npc);
    kvlangStrbufFree(&retpc);
    kvlangStrbufFree(&callpc);
    kvlangStrbufFree(&seglib);
    kvlangStrbufFree(&func_dir);
    kvlangStrbufFree(&sig_key);
    kvlangStrbufFree(&vtid_b);
    kvlangXvalueFree(&sig);
    free(func_key);
    free(pkg);
    free(name);
    free(frame_root);
    return ep;

fail_frame:
    kvlangKvDelTree(kv, frame_root, err, sizeof err);
    free(caller_fr);
    free(stack_fr);
    kvlangStrbufFree(&npc);
    kvlangStrbufFree(&retpc);
    kvlangStrbufFree(&callpc);
    kvlangStrbufFree(&seglib);
    free(ep);
    free(frame_root);
fail:
    kvlangStrbufFree(&func_dir);
    kvlangStrbufFree(&sig_key);
    kvlangStrbufFree(&vtid_b);
    kvlangXvalueFree(&sig);
    free(func_key);
    free(pkg);
    free(name);
    return NULL;
}

/* Read the declared target type; the argument value may be None. */
static void param_decl_type(kvlangKv_t *kv, const char *func_key, int x,
                            char *lt, size_t cap) {
    kvlangStrbuf_t pk;
    kvlangStrbufInit(&pk);
    kvlangStrbufPrintf(&pk, "%s.[0,%d]", func_key, x);
    kvlangXvalue_t dv;
    kvlangXvalueZero(&dv);
    kvlangKvGetOne(kv, pk.p, &dv);
    kvlangStrbufFree(&pk);
    if (!kvlangXvalueNone(&dv)) {
        kvspaceHead_t ah;
        if (kvlangXvalueHead(&dv, &ah) == 0) {
            int32_t al;
            const uint8_t *ab = kvlangXvalueBody(&dv, &ah, &al);
            for (int bi = 0; ab && bi < al; bi++) {
                if (ab[bi] != 0)
                    continue;
                int tl = al - (bi + 1);
                if (tl > 0 && tl < (int)cap) {
                    memcpy(lt, ab + bi + 1, (size_t)tl);
                    lt[tl] = 0;
                }
                break;
            }
        }
    }
    kvlangXvalueFree(&dv);
}

int kvlangCtlCall(kvlangFrame_t *f) {
    char *sub = handle_call(f->kv, f->pc, f->inst);
    if (!sub)
        return -1;
    kvlangVthreadAdvance(f, sub, "running");
    free(sub);
    return 0;
}

int kvlangCtlReturn(kvlangFrame_t *f) {
    char *parent = NULL;
    if (handle_return(f->kv, f->vtid, f->pc, &parent) != 0)
        return -1;
    if (!parent) {
        f->persist_failed = kvlangVthreadSetDone(f->kv, f->vtid, "ok") != 0;
    } else {
        kvlangVthreadAdvance(f, parent, "running");
    }
    free(parent);
    if (f->persist_failed)
        return -1;
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *stk = kvlangKeytreeStack(fr);
    char err[256];
    kvlangKvDelExtIndex(f->kv, stk, err, sizeof err);
    kvlangKvDelTree(f->kv, fr, err, sizeof err);
    free(stk);
    free(fr);
    return 0;
}

int kvlangCtlGoto(kvlangFrame_t *f) {
    kvlangRwirInst_t *inst = f->inst;
    if (inst->nr != 1) {
        char msg[128];
        snprintf(msg, sizeof msg, "RuntimeError: goto expects 1 irseq, got %d",
                 inst->nr);
        kvlangVthreadSetError(f->kv, f->vtid, f->pc, msg);
        return -1;
    }
    return jump_to(f, &inst->reads[0], OP_GOTO);
}

int kvlangCtlBr(kvlangFrame_t *f) {
    kvlangRwirInst_t *inst = f->inst;
    if (inst->nr != 3) {
        char msg[128];
        snprintf(msg, sizeof msg,
                 "RuntimeError: br expects cond trueIrseq falseIrseq, got %d",
                 inst->nr);
        kvlangVthreadSetError(f->kv, f->vtid, f->pc, msg);
        return -1;
    }
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    kvlangXvalue_t cond;
    kvlangXvalueZero(&cond);
    kvlangBuiltinResolveReadValue(f->kv, fr, inst->reads[0].name,
                                  &inst->reads[0].val, &cond);
    free(fr);
    if (kvlangXvalueNone(&cond)) {
        kvlangVthreadSetError(f->kv, f->vtid, f->pc,
                              "TypeError: None in branch condition");
        kvlangXvalueFree(&cond);
        return -1;
    }
    if (!kvlangXvalueKindIs(&cond, KVSPACE_KIND_BOOL)) {
        char msg[128];
        snprintf(msg, sizeof msg,
                 "TypeError: branch condition must be bool, got %s",
                 kvlangXvalueKind(&cond));
        kvlangVthreadSetError(f->kv, f->vtid, f->pc, msg);
        kvlangXvalueFree(&cond);
        return -1;
    }
    bool taken = kvlangScalarI64(kvlangXvalueScalar(&cond)) != 0;
    kvlangXvalueFree(&cond);
    return jump_to(f, &inst->reads[taken ? 1 : 2], OP_BR);
}

/* 动态调用：以运行时得到的 funckey 在当前 vthread 造一次 OP_CALL（不新开 vid），
 * pc 落在被调入口，帧结束回到本指令 NextPc。供 native vthread·call 用。 */
int kvlangKvcpuDynCall(kvlangKv_t *kv, const char *vtid, const char *pc,
                       const char *funckey) {
    kvlangRwirInst_t ci;
    ci.opcode = strdup(OP_CALL);
    ci.op_id = 0;
    ci.reads = malloc(sizeof(kvlangParam_t));
    ci.reads[0].name = strdup(funckey);
    ci.reads[0].type = NULL;
    ci.reads[0].address = 0;
    kvlangXvalueZero(&ci.reads[0].val);
    ci.nr = 1;
    ci.writes = NULL;
    ci.nw = 0;
    kvlangFrame_t f = {kv, vtid, pc, &ci, NULL};
    int rc = kvlangCtlCall(&f);
    if (f.persist_failed)
        rc = -1;
    free(ci.opcode);
    free(ci.reads[0].name);
    free(ci.reads);
    return rc;
}

int handoff_external_rwir(kvlangKv_t *kv, const char *vtid, const char *pc,
                          kvlangRwirInst_t *inst) {
    /* handoff：把 pc 挂到共享队列 /lib/<opcode>/vids/<vtid>（各 rwir 的 vids 已 Ptr 统一到
     * 第一个 rwir 的 vids 下，Set 经路径穿透落到同一 strkeymap）。外部执行器认领并驱动该 vthread，
     * 完成后删除该条目。本端 watch 同一 key 直至变 None（== 认领方已完成），单键交接、无 id。 */
    char *base = kvlangKeytreeRwir(inst->opcode);
    kvlangStrbuf_t vids;
    kvlangStrbufInit(&vids);
    kvlangStrbufPrintf(&vids, "%s/vids/%s", base, vtid);
    kvlangXvalue_t pv;
    kvlangXvalueNewCharUtf8(&pv, pc);
    kvlangKvPair_t p = {vids.p, pv};
    char err[256];
    int set_rc = kvlangKvSet(kv, &p, 1, err, sizeof err);
    kvlangXvalueFree(&pv);
    if (set_rc != 0) {
        kvlangStrbufFree(&vids);
        free(base);
        return -1;
    }

    kvlangXvalue_t none;
    kvlangXvalueZero(&none); /* 目标 None：等条目被删除 */
    kvlangXvalue_t got;
    kvlangXvalueZero(&got);
    int rc = kvlangKvWatch(kv, vids.p, &none, 30000000000ULL, &got);
    kvlangXvalueFree(&got);
    kvlangStrbufFree(&vids);
    free(base);
    if (rc != 0) {
        char msg[256];
        snprintf(msg, sizeof msg,
                 "RuntimeError: external rwir %s handoff failed", inst->opcode);
        kvlangVthreadSetError(kv, vtid, pc, msg);
        return -1;
    }
    return 0;
}

char *kvlangKvcpuBootstrap(kvlangKv_t *kv, const char *vtid,
                           const char *funcname, const char *const *args,
                           int nargs) {
    char *pkg = strdup("");
    char *name = strdup(funcname);
    const char *dot = rfind_sep(funcname);
    if (dot) {
        free(pkg);
        pkg = strndup(funcname, (size_t)(dot - funcname));
        free(name);
        name = strdup(dot + MEMBER_SEP_LEN);
    }
    char *func_key = kvlangKeytreeLibFunc(pkg, name);
    kvlangStrbuf_t func_dir;
    kvlangStrbufInit(&func_dir);
    kvlangStrbufPuts(&func_dir, func_key);
    kvlangStrbufPutc(&func_dir, '/');

    kvlangStrbuf_t sig_key;
    kvlangStrbufInit(&sig_key);
    kvlangStrbufPrintf(&sig_key, "%s[0,0]", func_dir.p);
    kvlangXvalue_t sig;
    kvlangXvalueZero(&sig);
    kvlangKvGetOne(kv, sig_key.p, &sig);
    if (kvlangXvalueNone(&sig) ||
        !kvlangXvalueKindIs(&sig, KVSPACE_KIND_RWFUNC)) {
        char msg[256];
        snprintf(msg, sizeof msg, "Bootstrap: rwir/rwfunc not found: %s",
                 funcname);
        kvlangVthreadSetError(kv, vtid, "", msg);
        kvlangXvalueFree(&sig);
        kvlangStrbufFree(&sig_key);
        kvlangStrbufFree(&func_dir);
        free(func_key);
        free(pkg);
        free(name);
        return NULL;
    }
    kvspaceHead_t h;
    kvspaceDecodeHead(sig.data, sig.len, &h);
    const uint8_t *sbody = sig.data + h.body_offset;
    int nr = sbody[0] | (sbody[1] << 8);

    char *frame_root = kvlangKeytreeFrameAt(vtid, 1);
    char *stack_fr = kvlangKeytreeStack(frame_root);
    char err[256];
    if (kvlangKvMkindex(kv, stack_fr, 0, err, sizeof err) != 0 ||
        kvlangKvExtIndex(kv, stack_fr, func_dir.p, err, sizeof err) != 0) {
        kvlangVthreadSetError(kv, vtid, "", err);
        kvlangXvalueFree(&sig);
        kvlangStrbufFree(&sig_key);
        kvlangStrbufFree(&func_dir);
        free(stack_fr);
        free(frame_root);
        free(func_key);
        free(pkg);
        free(name);
        return NULL;
    }

    char *ep = kvlangKeytreeEntryPc(frame_root);
    kvlangStrbuf_t callpc;
    kvlangStrbufInit(&callpc);
    kvlangKeytreeFrameCallpc(frame_root, &callpc);
    kvlangStrbuf_t seglib;
    kvlangStrbufInit(&seglib);
    kvlangStrbufPuts(&seglib, stack_fr);
    kvlangStrbufPuts(&seglib, SEG_LIB);
    kvlangXvalue_t v_ep, v_fn;
    kvlangXvalueZero(&v_ep);
    kvlangXvalueZero(&v_fn);
    kvlangXvalueNewCharUtf8(&v_ep, ep);
    kvlangXvalueNewCharUtf8(&v_fn, func_key);
    kvlangKvPair_t sys[2] = {{callpc.p, v_ep}, {seglib.p, v_fn}};
    int sys_rc = kvlangXvalueNone(&v_ep) || kvlangXvalueNone(&v_fn) ? -1 :
                 kvlangKvSet(kv, sys, 2, err, sizeof err);
    if (sys_rc != 0)
        snprintf(err, sizeof err, "RuntimeError: cannot persist entry frame");
    kvlangXvalueFree(&v_ep);
    kvlangXvalueFree(&v_fn);

    int bind_error = sys_rc != 0;
    if (!bind_error && (nargs < 0 || nargs > MAX_PARAMS || nr > MAX_PARAMS)) {
        snprintf(err, sizeof err, "TypeError: too many entry parameters");
        bind_error = 1;
    }
    if (nargs > 0 && !bind_error) {
        kvlangKvPair_t pairs[MAX_BIND_PAIRS];
        int np = 0;
        for (int i = 0; i < nr && i < nargs; i++) {
            kvlangStrbuf_t literal;
            kvlangStrbufInit(&literal);
            kvlangStrbufPrintf(&literal, "%s/._arglit%d", frame_root, i);
            char *value_key = kvlangStrbufDetach(&literal);
            kvlangXvalue_t av;
            kvlangXvalueZero(&av);
            kvlangBuiltinResolveReadValue(kv, "", args[i], NULL, &av);
            if (!kvlangXvalueNone(&av))
                pairs[np++] = (kvlangKvPair_t){strdup(value_key), av};
            else
                kvlangXvalueFree(&av);
            char lt[256] = {0};
            param_decl_type(kv, func_key, -(i + 1), lt, sizeof lt);
            if (add_param_binding(pairs, &np, frame_root, -(i + 1), lt, value_key) != 0) {
                snprintf(err, sizeof err, "TypeError: cannot bind entry argument %d", i + 1);
                bind_error = 1;
                free(value_key);
                break;
            }
            free(value_key);
        }
        if (!bind_error && np > 0 && kvlangKvSet(kv, pairs, np, err, sizeof err) != 0)
            bind_error = 1;
        for (int i = 0; i < np; i++) {
            free(pairs[i].key);
            kvlangXvalueFree(&pairs[i].val);
        }
    }
    if (bind_error) {
        kvlangVthreadSetError(kv, vtid, "", err);
        kvlangKvDelTree(kv, frame_root, err, sizeof err);
        free(ep);
        ep = NULL;
    }

    kvlangXvalueFree(&sig);
    kvlangStrbufFree(&sig_key);
    kvlangStrbufFree(&func_dir);
    kvlangStrbufFree(&callpc);
    kvlangStrbufFree(&seglib);
    free(stack_fr);
    free(frame_root);
    free(func_key);
    free(pkg);
    free(name);
    return ep;
}

int kvlangKvcpuExecuteMode(kvlangKv_t *kv, const char *pc, kvmode_t mode,
                           char **out_pc) {
    if (out_pc)
        *out_pc = NULL;
    kvlangStrbuf_t vtid_b;
    kvlangStrbufInit(&vtid_b);
    const char *vtid = kvlangKeytreeVtidFromPc(pc, &vtid_b);
    if (vtid[0] == 0) {
        kvlangStrbufFree(&vtid_b);
        return -1;
    }

    char *cur = NULL;
    char *cur_frame = NULL;
    int rc = 0;
    /* Reload status each step. */
    char *status = NULL;
    for (;;) {
        /* Release borrowed values from the previous instruction. */
        kvlangKvReadReset(kv);
        free(cur);
        cur = NULL;
        kvlangVthreadPcGet(kv, vtid, &cur);
        if (!cur || !cur[0]) {
            rc = -1;
            break;
        }
        /* Observe external status changes on every step. */
        free(status);
        status = NULL;
        kvlangVthreadStatusGet(kv, vtid, &status);
        if (!status ||
            (strcmp(status, "init") != 0 && strcmp(status, "running") != 0 &&
             strcmp(status, "wait") != 0)) {
            break;
        }

        cur_frame = kvlangKeytreeFrameRoot(cur);
        int depth = kvlangKeytreeFrameNum(cur);
        if (depth > MAX_STACK_DEPTH) {
            char msg[256];
            snprintf(msg, sizeof msg,
                     "RecursionError: stack overflow: depth=%d pc=%s", depth,
                     cur);
            kvlangVthreadSetError(kv, vtid, cur, msg);
            rc = -1;
            break;
        }

        const char *fr = cur_frame;
        kvlangRwirInst_t tmp;
        char *link_base = kvlangKeytreeStack(fr);
        char err[256];
        if (kvlangRwirDecode(kv, link_base, cur, &tmp, err, sizeof err) != 0) {
            char msg[256];
            snprintf(msg, sizeof msg, "decode: %.247s", err);
            kvlangVthreadSetError(kv, vtid, cur, msg);
            free(link_base);
            rc = -1;
            break;
        }
        free(link_base);
        kvlangRwirInst_t *inst = &tmp;

        kvlangLogDebug("[%s] PC=%s OP=%s R=%d W=%d", vtid, cur,
                       inst->opcode ? inst->opcode : "(empty)", inst->nr,
                       inst->nw);

        if (!inst->opcode || !inst->opcode[0]) {
            /* layout 对每条路径都补了 return（lower::terminate），走到空槽只能是 /lib 损坏
             * 或 goto/br 越界；报 RuntimeError 让该 vthread 停下，不拖垮整个进程。 */
            char msg[512];
            snprintf(msg, sizeof msg, "RuntimeError: no instruction at %s",
                     cur);
            kvlangVthreadSetError(kv, vtid, cur, msg);
            kvlangRwirInstFree(&tmp);
            rc = -1;
            break;
        }

        int exec_err = 0;
        char *yield = NULL;
        if (inst->op_id >= 0) {
            /* 单表派发：native 算子与 control/copy 同居 myrwircaps，op_id 直查一跳到底。 */
            kvlangFrame_t f = {kv, vtid, cur, inst, &yield};
            f.frame_root = cur_frame;
            f.status_known = status;   /* 本步开始前读到的 ‥status（源值，借用） */
            exec_err = kvlangBuiltinNative(&f);
            if (f.persist_failed)
                exec_err = -1;
            if (exec_err == 0 && yield) {
                /* native（vthread·run return 模式）冒泡一个子 vthread 的 rwir pc 给上层驱动。
                 * 本 vthread（主）pc 未推进，驱动派发子 rwir 并推进子 pc 后重入即续跑。 */
                if (out_pc)
                    *out_pc = yield;
                else
                    free(yield);
                kvlangRwirInstFree(&tmp);
                free(cur);
                free(cur_frame);
                free(status);
                kvlangStrbufFree(&vtid_b);
                return 1;
            }
        } else if (notinmyrwircaps(kv, inst->opcode)) {
            int def_nr = 0, def_dyn = 0;
            char *rk = kvlangKeytreeRwir(inst->opcode);
            char *def_sig = load_def_reads(kv, rk, &def_nr, &def_dyn);
            free(rk);
            if (def_sig)
                exec_err = check_read_types(kv, vtid, cur, inst->opcode,
                                            def_sig, def_nr, def_dyn,
                                            inst->reads, inst->nr);
            free(def_sig);
            if (exec_err == 0 && mode == KVMODE_RETURN) {
                if (out_pc)
                    *out_pc = strdup(cur);
                kvlangRwirInstFree(&tmp);
                free(cur);
                free(cur_frame);
                free(status);
                kvlangStrbufFree(&vtid_b);
                return 1;
            }
            if (exec_err == 0)
                exec_err = handoff_external_rwir(kv, vtid, cur, inst);
        } else {
            /* 用户函数 → call */
            kvlangRwirInst_t ci;
            ci.opcode = strdup(OP_CALL);
            ci.op_id = 0;
            ci.nr = inst->nr + 1;
            ci.nw = inst->nw;
            ci.reads = malloc(sizeof(kvlangParam_t) * (size_t)ci.nr);
            ci.reads[0].name = strdup(inst->opcode);
            ci.reads[0].type = NULL;
            ci.reads[0].address = 0;
            ci.reads[0].val.data = NULL;
            ci.reads[0].val.len = 0;
            for (int i = 0; i < inst->nr; i++) {
                ci.reads[i + 1] = inst->reads[i];
            }
            ci.writes = inst->writes;
            kvlangFrame_t cf = {kv, vtid, cur, &ci, NULL};
            cf.status_known = status;
            exec_err = kvlangCtlCall(&cf);
            if (cf.persist_failed)
                exec_err = -1;
            free(ci.opcode);
            free(ci.reads[0].name);
            free(ci.reads);
        }

        if (exec_err != 0) {
            kvlangRwirInstFree(&tmp);
            rc = -1;
            break;
        }

        kvlangRwirInstFree(&tmp);
        free(cur_frame);
        cur_frame = NULL;
        free(status);
        status = NULL;
    }

    free(cur);
    free(cur_frame);
    free(status);
    kvlangStrbufFree(&vtid_b);
    return rc;
}

int kvlangKvcpuExecute(kvlangKv_t *kv, const char *pc) {
    int rc = kvlangKvcpuExecuteMode(kv, pc, KVMODE_WATCH, NULL);
    return rc == 1 ? 0 : rc; /* WATCH 模式不返回 1，防御性归一 */
}

/* ── 路由判定 + 扩展 handoff（自 rwirext.c 迁入）────────────── */

/* notinmyrwircaps：opcode 是一条不在本 runtime myrwircaps 内、须经 def rwir 路由给
 * 能兑现它的其它 runtime 的 rwir。判据是 /lib/<opcode> 存在 def rwir 路由头
 * （langtype=def rwir，storetype=index）。kvspace 是能力唯一事实源；本判定在独立
 * kvlang 进程内发生，进程内 myrwircaps 恒不含它，故只有 /lib 路由头可信。 */
bool notinmyrwircaps(kvlangKv_t *k, const char *opcode) {
    if (opcode[0] == '/')
        return false;
    char *key = kvlangKeytreeRwir(opcode);
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    kvlangKvGetOne(k, key, &v);
    bool yes =
        !kvlangXvalueNone(&v) && kvlangXvalueKindIs(&v, KVSPACE_KIND_DEF_RWIR);
    kvlangXvalueFree(&v);
    free(key);
    return yes;
}

int kvlangRwirextHandoff(void *kvspace, const char *vtid, const char *pc) {
    kvlangKv_t k = {kvspace};
    char *fr = kvlangKeytreeFrameRoot(pc);
    if (!fr)
        return -1;
    char *lb = kvlangKeytreeStack(fr);
    kvlangRwirInst_t inst;
    char err[256];
    if (kvlangRwirDecode(&k, lb, pc, &inst, err, sizeof err) != 0) {
        free(fr);
        free(lb);
        return -1;
    }
    free(lb);
    int rc = handoff_external_rwir(&k, vtid, pc, &inst);
    free(fr);
    kvlangRwirInstFree(&inst);
    return rc;
}
