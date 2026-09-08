#include "runtime_internal.h"

/* decode-once 进程内缓存（IV-0：进程私有，绝不入 kvspace）。指令 XValue layout 后冻结，
 * 一条冻结指令由 (funcdir, addr0) 唯一标识（funcdir=/lib/pkg·fn，不同函数不同 funcdir，
 * 递归/多帧同函数共享同一条），故缓存永不失效。命中即跳过 code 槽读 + decode + 分类。
 * runtime C 单线程驱动（全库无锁直改 kvspace），此缓存同样无锁。永不淘汰（≈代码大小上界）。 */
typedef struct rwir_cache_ent {
    char *funcdir;
    int addr0;
    kvlangRwirInst_t *inst;
    struct rwir_cache_ent *next;
} rwir_cache_ent_t;
#define RWIR_CACHE_BUCKETS 4096
static rwir_cache_ent_t *g_rwir_cache[RWIR_CACHE_BUCKETS];

/* 直接对 (funcdir, addr0) 做 FNV——不拼 "funcdir#addr0" 串（那要每步 malloc+格式化+free，
 * 在 shm 后端 decode 本已近乎免费的深递归热路径上纯属净开销）。 */
static size_t rwir_hash(const char *funcdir, int addr0) {
    size_t h = 1469598103934665603ULL;
    for (const char *s = funcdir; *s; s++) { h ^= (unsigned char)*s; h *= 1099511628211ULL; }
    h ^= (size_t)(unsigned)addr0; h *= 1099511628211ULL;
    return h & (RWIR_CACHE_BUCKETS - 1);
}
static kvlangRwirInst_t *rwir_cache_get(const char *funcdir, int addr0) {
    for (rwir_cache_ent_t *e = g_rwir_cache[rwir_hash(funcdir, addr0)]; e; e = e->next)
        if (e->addr0 == addr0 && strcmp(e->funcdir, funcdir) == 0) return e->inst;
    return NULL;
}
static void rwir_cache_put(const char *funcdir, int addr0, kvlangRwirInst_t *inst) {
    size_t b = rwir_hash(funcdir, addr0);
    rwir_cache_ent_t *e = malloc(sizeof *e);
    e->funcdir = strdup(funcdir);
    e->addr0 = addr0;
    e->inst = inst;
    e->next = g_rwir_cache[b];
    g_rwir_cache[b] = e;
}

/* /lib 元信息 intern（P2-c）：opcode → {是否 notinmyrwircaps（须经 def rwir 路由到其它
 * runtime）、读参 kindexpr 签名}。/lib 布局后冻结，同 decode 缓存同理永不失效。命中即免去
 * OPID_notinmyrwircaps 分支每步两次元信息往返（notinmyrwircaps + load_def_reads）与签名 kindexpr 重解析。
 * langtype 签名串在此按 opcode 驻留一次（IV-0：进程私有，绝不入 kvspace）。 */
typedef struct opmeta_ent {
    char *opcode;
    int notinmyrwircaps;   /* 1 = 不在本 runtime myrwircaps、须经 def rwir 路由；0 = 用户 rwfunc */
    char *def_sig;         /* notinmyrwircaps 时的读参 kindexpr 签名（owned，可 NULL） */
    int def_nr;
    struct opmeta_ent *next;
} opmeta_ent_t;
static opmeta_ent_t *g_opmeta_cache[RWIR_CACHE_BUCKETS];

static char *load_def_reads(kvlangKv_t *kv, const char *key, int *out_nr);

static opmeta_ent_t *opmeta_get(kvlangKv_t *kv, const char *opcode) {
    size_t b = rwir_hash(opcode, 0);
    for (opmeta_ent_t *e = g_opmeta_cache[b]; e; e = e->next)
        if (strcmp(e->opcode, opcode) == 0) return e;
    opmeta_ent_t *e = malloc(sizeof *e);
    e->opcode = strdup(opcode);
    e->notinmyrwircaps = notinmyrwircaps(kv, opcode) ? 1 : 0;
    e->def_sig = NULL;
    e->def_nr = 0;
    if (e->notinmyrwircaps) {
        char *rk = kvlangKeytreeRwir(opcode);
        e->def_sig = load_def_reads(kv, rk, &e->def_nr);
        free(rk);
    }
    e->next = g_opmeta_cache[b];
    g_opmeta_cache[b] = e;
    return e;
}

/* 读帧的 ‥lib 槽 → funcdir（/lib/pkg·fn），供缓存键用；无则 NULL（不缓存该指令）。 */
static char *read_seglib(kvlangKv_t *kv, const char *fr) {
    char *stk = kvlangKeytreeStack(fr);
    kvlangStrbuf_t k; kvlangStrbufInit(&k);
    kvlangStrbufPuts(&k, stk); kvlangStrbufPuts(&k, SEG_LIB);
    free(stk);
    kvlangXvalue_t v; kvlangXvalueZero(&v);
    kvlangKvGetOne(kv, k.p, &v);
    kvlangStrbufFree(&k);
    char *r = kvlangXvalueNone(&v) ? NULL : kvlangXvalueValueString(&v);
    kvlangXvalueFree(&v);
    return r;
}

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
    if (kvlangXvalueNone(&p->val) || !kvlangXvalueKindIs(&p->val, KVSPACE_KIND_INT64)) return -1;
    int64_t n = kvlangScalarI64(kvlangXvalueScalar(&p->val));
    if (n < 1 || n > 0x7fffffff) return -1;
    return (int)n;
}

/* 函数内跳转：只改 PC 的 [irseq]，帧不变。目标非法 → RuntimeError，返回 -1。 */
static int jump_to(kvlangKv_t *kv, const char *vtid, const char *pc, const kvlangParam_t *target, const char *op) {
    int irseq = irseq_of(target);
    if (irseq < 0) {
        char msg[256];
        snprintf(msg, sizeof msg, "RuntimeError: %s target is not an int64 irseq: %s (kind=%s)",
                 op, target->name ? target->name : "", kvlangXvalueKind(&target->val));
        kvlangVthreadSetError(kv, vtid, pc, msg);
        return -1;
    }
    char *fr = kvlangKeytreeFrameRoot(pc);
    char *np = kvlangKeytreeIrseqPc(fr, irseq);
    free(fr);
    kvlangVthreadSet(kv, vtid, np, "running");
    kvlangLogDebug("[%s] %s → %s", vtid, op, np);
    free(np);
    return 0;
}

static bool is_literal(const char *s) {
    if (!s || !s[0]) return false;
    return s[0] == '"' || s[0] == '/' || strcmp(s, "true") == 0 || strcmp(s, "false") == 0 ||
           strcmp(s, "null") == 0 || (s[0] >= '0' && s[0] <= '9') || (s[0] == '-' && s[1]);
}

/* 派发期读参类型校验（runtime篇-07 第八节）：把每个实参的 kind 逐一匹配
 * rwir/rwfunc 定义的读参 kindexp。def_sig 为读参 kindexp 在前的 \n 分隔列表，
 * def_nr 为定义读参数。空 kindexp / any 跳过；末读参 "..." 变参吸收其后全部实参。
 * XValue 头只携带 array_len 不含多维 shape，故仅校验 kind 层。
 * 不匹配 → 置 TypeError，返回 -1；通过返回 0。 */
static int check_read_types(kvlangKv_t *kv, const char *vtid, const char *pc,
                            const char *opcode, const char *def_sig, int def_nr,
                            kvlangParam_t *args, int nargs) {
    if (def_nr <= 0 || !def_sig || !*def_sig) return 0;
    char *dup = strdup(def_sig);
    char *reads[128];
    int rn = 0;
    for (char *s = dup; rn < def_nr && rn < 128; ) {
        reads[rn++] = s;
        char *nl = strchr(s, '\n');
        if (!nl) break;
        *nl = 0; s = nl + 1;
    }
    bool var_last = rn > 0 && kvlang_rwirextKindexprVariadic(reads[rn - 1]);
    int min_args = var_last ? rn - 1 : rn;
    char *fr = kvlangKeytreeFrameRoot(pc);
    int rc = 0;
    if (nargs < min_args) {
        char msg[256];
        snprintf(msg, sizeof msg, "TypeError: %s expects %d args, got %d", opcode, min_args, nargs);
        kvlangVthreadSetError(kv, vtid, pc, msg);
        rc = -1;
    }
    for (int i = 0; rc == 0 && i < nargs; i++) {
        const char *exp = i < rn ? reads[i] : (var_last ? reads[rn - 1] : NULL);
        if (!exp) {
            char msg[256];
            snprintf(msg, sizeof msg, "TypeError: %s expects %d args, got %d", opcode, rn, nargs);
            kvlangVthreadSetError(kv, vtid, pc, msg);
            rc = -1;
            break;
        }
        if (!exp[0] || !kvlang_rwirextKindexprValid(exp)) continue;   /* 动态/非法 kindexp 跳过 */
        kvlangXvalue_t v; kvlangXvalueZero(&v);
        kvlangBuiltinResolveReadValue(kv, fr, args[i].name, &args[i].val, &v);
        const char *k = kvlangXvalueKind(&v);
        kvspaceHead_t h; kvlangXvalueHead(&v, &h);
        kvlang_kindexpr_t kx; kvlang_kindexpr_parse(h.kindexpr, &kx);
        bool ok = kvlang_rwirextKindexprMatch(exp, k, kx.ndim, kx.dims);
        char kbuf[40]; snprintf(kbuf, sizeof kbuf, "%s", k[0] ? k : "None");
        kvlangXvalueFree(&v);
        if (!ok) {
            char msg[256];
            snprintf(msg, sizeof msg, "TypeError: %s arg %d: expected %s, got %s", opcode, i + 1, exp, kbuf);
            kvlangVthreadSetError(kv, vtid, pc, msg);
            rc = -1;
        }
    }
    free(fr); free(dup);
    return rc;
}

/* 读取 rwir/rwfunc 定义体的 kindexp-list（nr/nw 前缀后的 \n 分隔串）。
 * 返回 malloc 串（调用方 free）并置 *out_nr；无定义返回 NULL。 */
static char *load_def_reads(kvlangKv_t *kv, const char *key, int *out_nr) {
    *out_nr = 0;
    kvlangXvalue_t v; kvlangXvalueZero(&v);
    kvlangKvGetOne(kv, key, &v);
    if (kvlangXvalueNone(&v)) { kvlangXvalueFree(&v); return NULL; }
    kvspaceHead_t h; kvlangXvalueHead(&v, &h);
    int32_t bl; const uint8_t *b = kvlangXvalueBody(&v, &h, &bl);
    if (bl < 4) { kvlangXvalueFree(&v); return NULL; }
    *out_nr = b[0] | (b[1] << 8);
    size_t sl = (size_t)(bl - 4);
    char *sig = malloc(sl + 1);
    memcpy(sig, b + 4, sl); sig[sl] = 0;
    kvlangXvalueFree(&v);
    return sig;
}

static char *frame_slot_key(const char *frame_root, const char *slot) {
    if (!slot || !slot[0]) return NULL;
    if (slot[0] == '/') return strdup(slot);
    if (strncmp(slot, MEMBER_SEP, MEMBER_SEP_LEN) == 0) return NULL;
    kvlangStrbuf_t b; kvlangStrbufInit(&b);
    char *stk = kvlangKeytreeStack(frame_root);
    kvlangStrbufPuts(&b, stk); free(stk);
    kvlangStrbufPuts(&b, slot);
    return kvlangStrbufDetach(&b);
}

/* 实参名 → 其存储键（写入被调帧 [0,-k]/[0,k]）。字面量返回 NULL。
 * 命名参数经 Ptr 指到本帧 [0,±k]，槽内是上层 handle_call 已 resolve 好的最终键路径；
 * 之后只追显式 Ptr（ref==1）链，勿把 char 值当路径再追——否则字符串实参的内容会被
 * 误当键（穿两层调用即变 None）。写侧 kvlangBuiltinResolveWriteSlot 同此纪律（#124）。 */
static char *resolve_read_path(kvlangKv_t *kv, const char *frame_root, const char *name) {
    if (is_literal(name)) return NULL;
    char *stk = kvlangKeytreeStack(frame_root);
    kvlangXvalue_t v; kvlangXvalueZero(&v);
    kvlangKvGetMember(kv, stk, name, &v);
    char *result = NULL;
    if (kvlangXvalueIsPtr(&v)) {
        char *target = kvlangXvaluePtrTarget(&v);
        kvlangXvalue_t nv; kvlangXvalueZero(&nv);
        kvlangKvGetMember(kv, stk, target, &nv);
        if (kvlangXvalueNone(&nv) || !kvlangXvalueIsCharKind(kvlangXvalueKind(&nv))) {
            result = frame_slot_key(frame_root, target);
        } else {
            char *path = kvlangXvalueValueString(&nv);
            for (;;) {
                kvlangXvalue_t hop; kvlangXvalueZero(&hop);
                kvlangKvGetOne(kv, path, &hop);
                if (!kvlangXvalueIsPtr(&hop)) { kvlangXvalueFree(&hop); result = path; break; }
                char *p2 = kvlangXvaluePtrTarget(&hop);
                kvlangXvalueFree(&hop);
                free(path);
                path = p2;
            }
        }
        kvlangXvalueFree(&nv);
        free(target);
    } else {
        result = frame_slot_key(frame_root, name);
    }
    kvlangXvalueFree(&v); free(stk);
    return result;
}

/* return：弹出当前帧 [d]。d==1 → 顶层结束（*out_next=NULL）；否则 *out_next=‥returnpc。
 * 返回链断裂（帧无 ‥returnpc）→ RuntimeError（#109），保留该帧供排查，返回 -1。 */
static int handle_return(kvlangKv_t *kv, const char *vtid, const char *pc, char **out_next) {
    *out_next = NULL;
    char *fr = kvlangKeytreeFrameRoot(pc);
    int d = kvlangKeytreeFrameNum(fr);
    char *next = NULL;
    if (d > 1) {
        kvlangStrbuf_t rk; kvlangStrbufInit(&rk);
        kvlangKeytreeFrameReturnpc(fr, &rk);
        kvlangXvalue_t v; kvlangXvalueZero(&v);
        kvlangKvGetOne(kv, rk.p, &v);
        if (!kvlangXvalueNone(&v)) next = kvlangXvalueValueString(&v);
        kvlangXvalueFree(&v);
        kvlangStrbufFree(&rk);
        if (!next || !next[0]) {
            char msg[512];
            snprintf(msg, sizeof msg, "RuntimeError: broken return chain: frame %s has no returnpc (pc=%s)", fr, pc);
            kvlangVthreadSetError(kv, vtid, pc, msg);
            free(next); free(fr);
            return -1;
        }
    }
    char *stk = kvlangKeytreeStack(fr);
    char err[256];
    kvlangKvDelExtIndex(kv, stk, err, sizeof err);
    kvlangKvDelTree(kv, fr, err, sizeof err);
    free(stk); free(fr);
    *out_next = next;
    return 0;
}

/* HandleCall：创建子帧。返回 EntryPC(frameRoot)，失败 NULL */
static char *handle_call(kvlangKv_t *kv, const char *pc, kvlangRwirInst_t *inst) {
    kvlangStrbuf_t vtid_b; kvlangStrbufInit(&vtid_b);
    const char *vtid = kvlangKeytreeVtidFromPc(pc, &vtid_b);
    const char *fn = inst->reads[0].name;
    char *pkg = strdup("");
    char *name = strdup(fn);
    const char *lp = "/lib/";
    if (strncmp(fn, lp, 5) == 0) {
        const char *rest = fn + 5;
        const char *dot = rfind_sep(rest);
        if (dot) { free(pkg); pkg = strndup(rest, (size_t)(dot - rest)); free(name); name = strdup(dot + MEMBER_SEP_LEN); }
        else { free(name); name = strdup(rest); }
    } else {
        const char *dot = rfind_sep(fn);
        if (dot) { free(pkg); pkg = strndup(fn, (size_t)(dot - fn)); free(name); name = strdup(dot + MEMBER_SEP_LEN); }
        else {
            /* 裸名调用（无 /lib/ 无 ·）：同 pkg 优先——当前函数所在 lib 下有同名 rwfunc 就用之
             * （lib aaa/bbb/math 内 sum(A,A) → /lib/aaa/bbb/math·sum），否则退回根 /lib/<fn>。 */
            char *ff = kvlangKeytreeFrameRoot(pc);
            if (ff) {
                /* 函数目录在帧的 ‥lib 槽（/lib/aaa/bbb/math·double/）：由此取调用者 pkg。 */
                kvlangStrbuf_t lk; kvlangStrbufInit(&lk);
                char *stk = kvlangKeytreeStack(ff);
                kvlangStrbufPuts(&lk, stk); free(stk);
                kvlangStrbufPuts(&lk, SEG_LIB);
                kvlangXvalue_t lv; kvlangXvalueZero(&lv);
                kvlangKvGetOne(kv, lk.p, &lv);
                kvlangStrbufFree(&lk);
                char *funcdir = kvlangXvalueNone(&lv) ? NULL : kvlangXvalueValueString(&lv);
                kvlangXvalueFree(&lv);
                if (funcdir) {
                    char *rel = funcdir + 5; // 剥 /lib/
                    size_t rl = strlen(rel);
                    if (rl > 0 && rel[rl - 1] == '/') rel[rl - 1] = '\0'; // 剥尾 /
                    const char *sep = rfind_sep(rel);
                    if (sep) {
                        char *cand_pkg = strndup(rel, (size_t)(sep - rel));
                        char *cand = kvlangKeytreeLibFunc(cand_pkg, fn);
                        kvlangStrbuf_t sk; kvlangStrbufInit(&sk);
                        kvlangStrbufPrintf(&sk, "%s/[0,0]", cand);
                        kvlangXvalue_t sv; kvlangXvalueZero(&sv);
                        kvlangKvGetOne(kv, sk.p, &sv);
                        bool ok = !kvlangXvalueNone(&sv) && kvlangXvalueKindIs(&sv, KVSPACE_KIND_RWFUNC);
                        kvlangXvalueFree(&sv); kvlangStrbufFree(&sk);
                        if (ok) { free(pkg); pkg = cand_pkg; }
                        else free(cand_pkg);
                        free(cand);
                    }
                    free(funcdir);
                }
                free(ff);
            }
        }
    }
    char *func_key = kvlangKeytreeLibFunc(pkg, name);
    kvlangStrbuf_t func_dir; kvlangStrbufInit(&func_dir);
    kvlangStrbufPuts(&func_dir, func_key); kvlangStrbufPutc(&func_dir, '/');

    kvlangStrbuf_t sig_key; kvlangStrbufInit(&sig_key);
    kvlangStrbufPrintf(&sig_key, "%s[0,0]", func_dir.p);
    kvlangXvalue_t sig; kvlangXvalueZero(&sig);
    kvlangKvGetOne(kv, sig_key.p, &sig);
    if (kvlangXvalueNone(&sig) || !kvlangXvalueKindIs(&sig, KVSPACE_KIND_RWFUNC)) {
        /* 按 xvalue 的 kind 精确区分缺 rwir 还是缺 rwfunc：
         * 到这里说明 opcode 已被 notinmyrwircaps 判否（/lib/<op> 非 def rwir 路由头）。 */
        char *rk = kvlangKeytreeRwir(fn);
        kvlangXvalue_t rv; kvlangXvalueZero(&rv);
        kvlangKvGetOne(kv, rk, &rv);
        char msg[256];
        if (!kvlangXvalueNone(&rv) && kvlangXvalueKindIs(&rv, KVSPACE_KIND_DEF_RWIR))
            snprintf(msg, sizeof msg, "NameError: rwir 未注册/签名不匹配: %s", fn);
        else if (!kvlangXvalueNone(&sig))
            snprintf(msg, sizeof msg, "NameError: %s 不是 rwfunc (kind=%s)", fn, kvlangXvalueKind(&sig));
        else
            snprintf(msg, sizeof msg, "NameError: rwfunc not found: %s", fn);
        kvlangXvalueFree(&rv); free(rk);
        kvlangVthreadSetError(kv, vtid, pc, msg);
        goto fail;
    }
    kvspaceHead_t h; kvspaceDecodeHead(sig.data, sig.len, &h);
    const uint8_t *sbody = sig.data + h.body_offset;
    int nr = sbody[0] | (sbody[1] << 8);
    int nw = sbody[2] | (sbody[3] << 8);

    {   /* 读参类型校验：reads[0]=函数名，实参从 reads[1] 起 */
        size_t sl = h.body_len >= 4 ? (size_t)(h.body_len - 4) : 0;
        char *ds = malloc(sl + 1);
        memcpy(ds, sbody + 4, sl); ds[sl] = 0;
        int crc = check_read_types(kv, vtid, pc, fn, ds, nr, inst->reads + 1, inst->nr - 1);
        free(ds);
        if (crc != 0) goto fail;
    }

    char *caller_fr = kvlangKeytreeFrameRoot(pc);
    int d = kvlangKeytreeFrameNum(pc);
    char *frame_root = kvlangKeytreeFrameAt(vtid, d + 1);
    char err[256];
    kvlangKvDelTree(kv, frame_root, err, sizeof err);
    char *stack_fr = kvlangKeytreeStack(frame_root);
    kvlangKvMkindex(kv, stack_fr, 0, err, sizeof err);
    kvlangKvExtIndex(kv, stack_fr, func_dir.p, err, sizeof err);

    /* 系统变量 */
    kvlangStrbuf_t npc; kvlangStrbufInit(&npc); kvlangRwirNextPc(pc, &npc);
    kvlangStrbuf_t retpc; kvlangStrbufInit(&retpc); kvlangKeytreeFrameReturnpc(frame_root, &retpc);
    kvlangStrbuf_t callpc; kvlangStrbufInit(&callpc); kvlangKeytreeFrameCallpc(frame_root, &callpc);
    char *ep = kvlangKeytreeEntryPc(frame_root);
    kvlangStrbuf_t seglib; kvlangStrbufInit(&seglib); kvlangStrbufPuts(&seglib, stack_fr); kvlangStrbufPuts(&seglib, SEG_LIB);
    kvlangXvalue_t v_npc, v_ep, v_fn; kvlangXvalueZero(&v_npc); kvlangXvalueZero(&v_ep); kvlangXvalueZero(&v_fn);
    kvlangXvalueNewCharUtf8(&v_npc, npc.p);
    kvlangXvalueNewCharUtf8(&v_ep, ep);
    kvlangXvalueNewCharUtf8(&v_fn, func_key);
    kvlangKvPair_t sys[3] = { { retpc.p, v_npc }, { callpc.p, v_ep }, { seglib.p, v_fn } };
    kvlangKvSet(kv, sys, 3, err, sizeof err);
    kvlangXvalueFree(&v_npc); kvlangXvalueFree(&v_ep); kvlangXvalueFree(&v_fn);

    /* 读参 + 写参 */
    kvlangKvPair_t pairs[512]; int np = 0;
    int lit_seq = 0;
    for (int i = 0; i < nr; i++) {
        kvlangStrbuf_t slot; kvlangStrbufInit(&slot);
        kvlangStrbufPrintf(&slot, "%s/[0,-%d]", frame_root, i + 1);
        if (i + 1 < inst->nr) {
            kvlangParam_t *arg = &inst->reads[i + 1];
            char *rk = resolve_read_path(kv, caller_fr, arg->name);
            bool concrete = !kvlangXvalueNone(&arg->val) && !kvlangXvalueKindIs(&arg->val, KVSPACE_KIND_RWIR) && !kvlangXvalueKindIs(&arg->val, KVSPACE_KIND_RWFUNC);
            if (concrete) {
                /* 字面量无变量槽，一律写 ._litN；勿沿用 resolve_read_path 的返回值——
                 * 否则字面量内容（如 "https://x" 里的 //）会被当路径段，二次读回即丢。 */
                if (rk) free(rk);
                kvlangStrbuf_t lk; kvlangStrbufInit(&lk);
                kvlangStrbufPrintf(&lk, "%s/._lit%d", caller_fr, lit_seq++);
                rk = kvlangStrbufDetach(&lk);
                /* 写字面量到 rk（拷贝，避免 double-free） */
                kvspaceHead_t ah; kvspaceDecodeHead(arg->val.data, arg->val.len, &ah);
                int32_t abl; const uint8_t *ab = kvlangXvalueBody(&arg->val, &ah, &abl);
                kvlang_kindexpr_t akx; kvlang_kindexpr_parse(ah.kindexpr, &akx);
                pairs[np].key = strdup(rk);
                kvspaceTlvEncode(kvlangXvalueKind(&arg->val), ab, (uint32_t)abl, akx.dims, akx.ndim,
                                   &pairs[np].val.data, &pairs[np].val.len);
                np++;
            }
            if (rk) {
                kvlangXvalue_t rv; kvlangXvalueNewCharUtf8(&rv, rk);
                pairs[np].key = kvlangStrbufDetach(&slot);
                pairs[np].val = rv;
                np++;
                free(rk);
            }
        }
        kvlangStrbufFree(&slot);
    }
    for (int i = 0; i < nw; i++) {
        kvlangStrbuf_t slot; kvlangStrbufInit(&slot);
        kvlangStrbufPrintf(&slot, "%s/[0,%d]", frame_root, i + 1);
        if (i < inst->nw) {
            char *wk = resolve_read_path(kv, caller_fr, inst->writes[i].name);
            if (wk) {
                kvlangXvalue_t wv; kvlangXvalueNewCharUtf8(&wv, wk);
                pairs[np].key = kvlangStrbufDetach(&slot);
                pairs[np].val = wv;
                np++;
                free(wk);
            }
        }
        kvlangStrbufFree(&slot);
    }
    if (np > 0) kvlangKvSet(kv, pairs, np, err, sizeof err);
    for (int i = 0; i < np; i++) { free(pairs[i].key); kvlangXvalueFree(&pairs[i].val); }

    free(caller_fr); free(stack_fr);
    kvlangStrbufFree(&npc); kvlangStrbufFree(&retpc); kvlangStrbufFree(&callpc); kvlangStrbufFree(&seglib);
    kvlangStrbufFree(&func_dir); kvlangStrbufFree(&sig_key); kvlangStrbufFree(&vtid_b);
    kvlangXvalueFree(&sig); free(func_key); free(pkg); free(name);
    free(frame_root);
    return ep;

fail:
    kvlangStrbufFree(&func_dir); kvlangStrbufFree(&sig_key); kvlangStrbufFree(&vtid_b);
    kvlangXvalueFree(&sig); free(func_key); free(pkg); free(name);
    return NULL;
}

int kvlangCtlCall(kvlangFrame_t *f) {
    char *sub = handle_call(f->kv, f->pc, f->inst);
    if (!sub) return -1;
    kvlangVthreadSet(f->kv, f->vtid, sub, "running");
    free(sub);
    return 0;
}

int kvlangCtlReturn(kvlangFrame_t *f) {
    char *parent = NULL;
    if (handle_return(f->kv, f->vtid, f->pc, &parent) != 0) return -1;
    if (!parent) { kvlangVthreadSetDone(f->kv, f->vtid, "ok"); return 0; }
    kvlangVthreadSet(f->kv, f->vtid, parent, "running");
    free(parent);
    return 0;
}

int kvlangCtlGoto(kvlangFrame_t *f) {
    kvlangRwirInst_t *inst = f->inst;
    if (inst->nr != 1) {
        char msg[128]; snprintf(msg, sizeof msg, "RuntimeError: goto expects 1 irseq, got %d", inst->nr);
        kvlangVthreadSetError(f->kv, f->vtid, f->pc, msg);
        return -1;
    }
    return jump_to(f->kv, f->vtid, f->pc, &inst->reads[0], OP_GOTO);
}

int kvlangCtlBr(kvlangFrame_t *f) {
    kvlangRwirInst_t *inst = f->inst;
    if (inst->nr != 3) {
        char msg[128]; snprintf(msg, sizeof msg, "RuntimeError: br expects cond trueIrseq falseIrseq, got %d", inst->nr);
        kvlangVthreadSetError(f->kv, f->vtid, f->pc, msg);
        return -1;
    }
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    kvlangXvalue_t cond; kvlangXvalueZero(&cond);
    kvlangBuiltinResolveReadValue(f->kv, fr, inst->reads[0].name, &inst->reads[0].val, &cond);
    free(fr);
    if (kvlangXvalueNone(&cond)) {
        kvlangVthreadSetError(f->kv, f->vtid, f->pc, "TypeError: None in branch condition");
        kvlangXvalueFree(&cond);
        return -1;
    }
    if (!kvlangXvalueKindIs(&cond, KVSPACE_KIND_BOOL)) {
        char msg[128]; snprintf(msg, sizeof msg, "TypeError: branch condition must be bool, got %s", kvlangXvalueKind(&cond));
        kvlangVthreadSetError(f->kv, f->vtid, f->pc, msg);
        kvlangXvalueFree(&cond);
        return -1;
    }
    bool taken = kvlangScalarI64(kvlangXvalueScalar(&cond)) != 0;
    kvlangXvalueFree(&cond);
    return jump_to(f->kv, f->vtid, f->pc, &inst->reads[taken ? 1 : 2], OP_BR);
}

/* 动态调用：以运行时得到的 funckey 在当前 vthread 造一次 OP_CALL（不新开 vid），
 * pc 落在被调入口，帧结束回到本指令 NextPc。供 native vthread·call 用。 */
int kvlangKvcpuDynCall(kvlangKv_t *kv, const char *vtid, const char *pc, const char *funckey) {
    kvlangRwirInst_t ci;
    ci.opcode = strdup(OP_CALL);
    ci.op_id = 0;
    ci.reads = malloc(sizeof(kvlangParam_t));
    ci.reads[0].name = strdup(funckey);
    kvlangXvalueZero(&ci.reads[0].val);
    ci.nr = 1;
    ci.writes = NULL;
    ci.nw = 0;
    kvlangFrame_t f = { kv, vtid, pc, &ci, NULL };
    int rc = kvlangCtlCall(&f);
    free(ci.opcode); free(ci.reads[0].name); free(ci.reads);
    return rc;
}

int handoff_external_rwir(kvlangKv_t *kv, const char *vtid, const char *pc, kvlangRwirInst_t *inst) {
    /* handoff：把 pc 挂到共享队列 /lib/<opcode>/vids/<vtid>（各 rwir 的 vids 已 Ptr 统一到
     * 第一个 rwir 的 vids 下，Set 经路径穿透落到同一 strkeymap）。外部执行器认领并驱动该 vthread，
     * 完成后删除该条目。本端 watch 同一 key 直至变 None（== 认领方已完成），单键交接、无 id。 */
    char *base = kvlangKeytreeRwir(inst->opcode);
    kvlangStrbuf_t vids; kvlangStrbufInit(&vids);
    kvlangStrbufPrintf(&vids, "%s/vids/%s", base, vtid);
    kvlangXvalue_t pv; kvlangXvalueNewCharUtf8(&pv, pc);
    kvlangKvPair_t p = { vids.p, pv };
    char err[256];
    kvlangKvSet(kv, &p, 1, err, sizeof err);
    kvlangXvalueFree(&pv);

    kvlangXvalue_t none; kvlangXvalueZero(&none);   /* 目标 None：等条目被删除 */
    kvlangXvalue_t got; kvlangXvalueZero(&got);
    int rc = kvlangKvWatch(kv, vids.p, &none, 30000000000ULL, &got);
    kvlangXvalueFree(&got);
    kvlangStrbufFree(&vids); free(base);
    if (rc != 0) {
        char msg[256]; snprintf(msg, sizeof msg, "RuntimeError: external rwir %s handoff failed", inst->opcode);
        kvlangVthreadSetError(kv, vtid, pc, msg);
        return -1;
    }
    return 0;
}

char *kvlangKvcpuBootstrap(kvlangKv_t *kv, const char *vtid, const char *funcname,
                      const char *const *args, int nargs) {
    char *pkg = strdup("");
    char *name = strdup(funcname);
    const char *dot = rfind_sep(funcname);
    if (dot) { free(pkg); pkg = strndup(funcname, (size_t)(dot - funcname)); free(name); name = strdup(dot + MEMBER_SEP_LEN); }
    char *func_key = kvlangKeytreeLibFunc(pkg, name);
    kvlangStrbuf_t func_dir; kvlangStrbufInit(&func_dir);
    kvlangStrbufPuts(&func_dir, func_key); kvlangStrbufPutc(&func_dir, '/');

    kvlangStrbuf_t sig_key; kvlangStrbufInit(&sig_key);
    kvlangStrbufPrintf(&sig_key, "%s[0,0]", func_dir.p);
    kvlangXvalue_t sig; kvlangXvalueZero(&sig);
    kvlangKvGetOne(kv, sig_key.p, &sig);
    if (kvlangXvalueNone(&sig) || !kvlangXvalueKindIs(&sig, KVSPACE_KIND_RWFUNC)) {
        char msg[256]; snprintf(msg, sizeof msg, "Bootstrap: rwir/rwfunc not found: %s", funcname);
        kvlangVthreadSetError(kv, vtid, "", msg);
        kvlangXvalueFree(&sig); kvlangStrbufFree(&sig_key); kvlangStrbufFree(&func_dir);
        free(func_key); free(pkg); free(name);
        return NULL;
    }
    kvspaceHead_t h; kvspaceDecodeHead(sig.data, sig.len, &h);
    const uint8_t *sbody = sig.data + h.body_offset;
    int nr = sbody[0] | (sbody[1] << 8);

    char *frame_root = kvlangKeytreeFrameAt(vtid, 1);
    char *stack_fr = kvlangKeytreeStack(frame_root);
    char err[256];
    kvlangKvMkindex(kv, stack_fr, 0, err, sizeof err);
    kvlangKvExtIndex(kv, stack_fr, func_dir.p, err, sizeof err);

    char *ep = kvlangKeytreeEntryPc(frame_root);
    kvlangStrbuf_t callpc; kvlangStrbufInit(&callpc); kvlangKeytreeFrameCallpc(frame_root, &callpc);
    kvlangStrbuf_t seglib; kvlangStrbufInit(&seglib); kvlangStrbufPuts(&seglib, stack_fr); kvlangStrbufPuts(&seglib, SEG_LIB);
    kvlangXvalue_t v_ep, v_fn; kvlangXvalueZero(&v_ep); kvlangXvalueZero(&v_fn);
    kvlangXvalueNewCharUtf8(&v_ep, ep);
    kvlangXvalueNewCharUtf8(&v_fn, func_key);
    kvlangKvPair_t sys[2] = { { callpc.p, v_ep }, { seglib.p, v_fn } };
    kvlangKvSet(kv, sys, 2, err, sizeof err);
    kvlangXvalueFree(&v_ep); kvlangXvalueFree(&v_fn);

    if (nargs > 0) {
        kvlangKvPair_t pairs[128]; int np = 0;
        for (int i = 0; i < nr && i < nargs; i++) {
            kvlangStrbuf_t slot; kvlangStrbufInit(&slot);
            kvlangStrbufPrintf(&slot, "%s/[0,-%d]", frame_root, i + 1);
            kvlangXvalue_t av; kvlangXvalueZero(&av);
            kvlangBuiltinResolveReadValue(kv, "", args[i], NULL, &av);
            pairs[np].key = kvlangStrbufDetach(&slot);
            pairs[np].val = av;
            np++;
        }
        if (np > 0) kvlangKvSet(kv, pairs, np, err, sizeof err);
        for (int i = 0; i < np; i++) { free(pairs[i].key); kvlangXvalueFree(&pairs[i].val); }
    }

    kvlangXvalueFree(&sig); kvlangStrbufFree(&sig_key); kvlangStrbufFree(&func_dir);
    kvlangStrbufFree(&callpc); kvlangStrbufFree(&seglib);
    free(stack_fr); free(frame_root); free(func_key); free(pkg); free(name);
    return ep;
}

int kvlangKvcpuExecuteMode(kvlangKv_t *kv, const char *pc, kvmode_t mode, char **out_pc) {
    if (out_pc) *out_pc = NULL;
    kvlangStrbuf_t vtid_b; kvlangStrbufInit(&vtid_b);
    const char *vtid = kvlangKeytreeVtidFromPc(pc, &vtid_b);
    if (vtid[0] == 0) { kvlangStrbufFree(&vtid_b); return -1; }

    char *cur = strdup(pc);
    char *cur_frame = NULL, *cur_funcdir = NULL;   /* 帧不变时 funcdir 只读一次，供缓存键 */
    int rc = 0;
    /* status 跨轮携带：尾部 VthreadGet 已连 pc 一并取出，下轮直接复用，省掉背靠背重读
     * （vthread 记录在两次 get 之间不被改写；status 串由 ValueString 自持，跨 ReadReset 存活）。 */
    char *status = NULL;
    { char *pcv = NULL; kvlangVthreadGet(kv, vtid, &pcv, &status); free(pcv); }
    for (;;) {
        /* 指令边界：回收上条指令执行期借出的读池（cache 指令的读参已 Materialize 自持，不受影响）。
         * durable 惰性写不再清池，全靠此处回收；shm 常驻映射侧为 no-op。 */
        kvlangKvReadReset(kv);
        if (!status || (strcmp(status, "init") != 0 && strcmp(status, "running") != 0 && strcmp(status, "wait") != 0)) {
            break;
        }
        free(status); status = NULL;

        int depth = kvlangKeytreeFrameNum(cur);
        if (depth > MAX_STACK_DEPTH) {
            char msg[256];
            snprintf(msg, sizeof msg, "RecursionError: stack overflow: depth=%d pc=%s", depth, cur);
            kvlangVthreadSetError(kv, vtid, cur, msg);
            rc = -1;
            break;
        }

        char *fr = kvlangKeytreeFrameRoot(cur);
        if (!cur_frame || strcmp(cur_frame, fr) != 0) {
            free(cur_frame); cur_frame = strdup(fr);
            free(cur_funcdir); cur_funcdir = read_seglib(kv, fr);
        }
        const char *lastc = NULL;
        for (const char *p = cur; (p = strstr(p, "/[")) != NULL; p += 2) lastc = p;
        int addr0 = lastc ? kvlangRwirExtractAddr0(lastc + 1) : 0;

        kvlangRwirInst_t tmp;
        kvlangRwirInst_t *inst = NULL;
        bool tmp_owned = false;
        if (cur_funcdir) inst = rwir_cache_get(cur_funcdir, addr0);

        if (!inst) {
            char *link_base = kvlangKeytreeStack(fr);
            char err[256];
            if (kvlangRwirDecode(kv, link_base, cur, &tmp, err, sizeof err) != 0) {
                char msg[256]; snprintf(msg, sizeof msg, "decode: %s", err);
                kvlangVthreadSetError(kv, vtid, cur, msg);
                free(link_base); free(fr);
                rc = -1;
                break;
            }
            free(link_base);
            if (cur_funcdir && tmp.opcode && tmp.opcode[0]) {
                /* 转移所有权入缓存：冻结指令永不失效、永不 free */
                kvlangRwirInst_t *persist = malloc(sizeof *persist);
                *persist = tmp;
                rwir_cache_put(cur_funcdir, addr0, persist);
                inst = persist;
            } else {
                inst = &tmp; tmp_owned = true;
            }
        }

        kvlangLogDebug("[%s] PC=%s OP=%s R=%d W=%d", vtid, cur, inst->opcode ? inst->opcode : "(empty)", inst->nr, inst->nw);

        if (!inst->opcode || !inst->opcode[0]) {
            /* layout 对每条路径都补了 return（lower::terminate），走到空槽只能是 /lib 损坏
             * 或 goto/br 越界；报 RuntimeError 让该 vthread 停下，不拖垮整个进程。 */
            char msg[512];
            snprintf(msg, sizeof msg, "RuntimeError: no instruction at %s", cur);
            kvlangVthreadSetError(kv, vtid, cur, msg);
            free(fr); if (tmp_owned) kvlangRwirInstFree(&tmp);
            rc = -1;
            break;
        }

        int exec_err = 0;
        char *yield = NULL;
        if (inst->op_id >= 0) {
            /* 单表派发：native 算子与 control/copy 同居 myrwircaps，op_id 直查一跳到底。 */
            kvlangFrame_t f = { kv, vtid, cur, inst, &yield };
            exec_err = kvlangBuiltinNative(&f);
            if (exec_err == 0 && yield) {
                /* native（vthread·run return 模式）冒泡一个子 vthread 的 rwir pc 给上层驱动。
                 * 本 vthread（主）pc 未推进，驱动派发子 rwir 并推进子 pc 后重入即续跑。 */
                if (out_pc) *out_pc = yield; else free(yield);
                free(fr); if (tmp_owned) kvlangRwirInstFree(&tmp);
                free(cur); free(cur_frame); free(cur_funcdir); free(status); kvlangStrbufFree(&vtid_b);
                return 1;
            }
        } else if (opmeta_get(kv, inst->opcode)->notinmyrwircaps) {
            opmeta_ent_t *m = opmeta_get(kv, inst->opcode);
            if (m->def_sig)
                exec_err = check_read_types(kv, vtid, cur, inst->opcode, m->def_sig, m->def_nr, inst->reads, inst->nr);
            if (exec_err == 0 && mode == KVMODE_RETURN) {
                if (out_pc) *out_pc = strdup(cur);
                free(fr); if (tmp_owned) kvlangRwirInstFree(&tmp);
                free(cur); free(cur_frame); free(cur_funcdir); free(status); kvlangStrbufFree(&vtid_b);
                return 1;
            }
            if (exec_err == 0) exec_err = handoff_external_rwir(kv, vtid, cur, inst);
        } else {
            /* 用户函数 → call */
            kvlangRwirInst_t ci;
            ci.opcode = strdup(OP_CALL);
            ci.op_id = 0;
            ci.nr = inst->nr + 1;
            ci.nw = inst->nw;
            ci.reads = malloc(sizeof(kvlangParam_t) * (size_t)ci.nr);
            ci.reads[0].name = strdup(inst->opcode);
            ci.reads[0].val.data = NULL; ci.reads[0].val.len = 0;
            for (int i = 0; i < inst->nr; i++) { ci.reads[i + 1] = inst->reads[i]; }
            ci.writes = inst->writes;
            kvlangFrame_t cf = { kv, vtid, cur, &ci, NULL };
            exec_err = kvlangCtlCall(&cf);
            free(ci.opcode); free(ci.reads[0].name); free(ci.reads);
        }

        if (exec_err != 0) { free(fr); if (tmp_owned) kvlangRwirInstFree(&tmp); rc = -1; break; }

        char *newpc = NULL;
        kvlangVthreadGet(kv, vtid, &newpc, &status);   /* status 连 pc 一并取出，供下轮直接复用 */
        free(fr);
        if (tmp_owned) kvlangRwirInstFree(&tmp);
        if (!newpc || !newpc[0]) { free(newpc); break; }
        free(cur);
        cur = newpc;
    }

    free(cur); free(cur_frame); free(cur_funcdir); free(status);
    kvlangStrbufFree(&vtid_b);
    return rc;
}

int kvlangKvcpuExecute(kvlangKv_t *kv, const char *pc) {
    int rc = kvlangKvcpuExecuteMode(kv, pc, KVMODE_WATCH, NULL);
    return rc == 1 ? 0 : rc;   /* WATCH 模式不返回 1，防御性归一 */
}
