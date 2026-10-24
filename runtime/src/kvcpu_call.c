#include "kvcpu_internal.h"
#include "func_code.h"
#include <assert.h>

/* 找末个成员分隔符（·，多字节），对齐 strrchr('.') 的单字节旧语义。 */
static const char *rfind_separator(const char *s) {
    const char *found = NULL, *p = s;
    while ((p = strstr(p, MEMBER_SEP)) != NULL) {
        found = p;
        p += MEMBER_SEP_LEN;
    }
    return found;
}

/* goto/br 目标：layout 已把 label 解析为 int64 irseq（≥1）。非 int64 / 越界返回 -1。 */
static int irseq_of(const kvlangParam_t *p) {
    if (kvlangXvalueNone(&p->value) ||
        !kvlangXvalueKindIs(&p->value, KVSPACE_KIND_INT64))
        return -1;
    int64_t n = kvlangScalarI64(kvlangXvalueScalar(&p->value));
    if (n < 1 || n > 0x7fffffff)
        return -1;
    return (int)n;
}

/* 函数内跳转：只改 PC 的 [irseq]，帧不变。目标非法 → RuntimeError，返回 -1。
 * 目标 irseq 已在行绑定期从 int64 字面量固化；PC 在栈缓冲里格式化，
 * 不按帧/按步分配。异常形状回退动态路径。 */
static int jump_to(kvlangFrame_t *f, int which, const kvlangParam_t *target,
                   const char *op) {
    const kvlangRowBind_t *b = f->bind;
    int known = (b && which >= 0 && which < 2) ? b->jump_irseq[which] : 0;
    int irseq = known > 0 ? known : irseq_of(target);
    if (irseq < 1) {
        char msg[256];
        snprintf(msg, sizeof msg,
                 "RuntimeError: %s target is not an int64 irseq: %s (kind=%s)",
                 op, target->name ? target->name : "",
                 kvlangXvalueKind(&target->value));
        kvlangVthreadSetError(f->kvspace, f->vthread_id, f->pc, msg);
        return -1;
    }
    if (f->cursor && f->cursor->prefix_ok && f->pc_out && f->pc_out_cap >= 16) {
        size_t n = kvlangRwirNextPcFromPrefix(
            f->cursor->prefix, (size_t)f->cursor->prefix_len, irseq - 1,
            f->pc_out, f->pc_out_cap);
        if (n > 0) {
            kvlangVthreadNextPc(f, f->pc_out, "running");
            KV_LOG_DEBUG("[%s] %s -> %s", f->vthread_id, op, f->pc_out);
            f->cursor->irseq = irseq;
            f->cursor->jump = true;
            return 0;
        }
    }
    char *owned = NULL;
    const char *fr = f->frame_root;
    if (!fr) { owned = kvlangKeytreeFrameRoot(f->pc); fr = owned; }
    char pcbuf[KV_KEY_MAX];
    char *np = NULL;
    const char *next = NULL;
    if (kvlangKeytreeIrseqPcBuffer(fr, irseq, pcbuf, sizeof pcbuf) > 0) {
        next = pcbuf;
    } else {
        np = kvlangKeytreeIrseqPc(fr, irseq);
        next = np;
    }
    kvlangVthreadNextPc(f, next, "running");
    KV_LOG_DEBUG("[%s] %s -> %s", f->vthread_id, op, next);
    if (f->cursor) {
        kvlangExecCursor *c = f->cursor;
        size_t pl = strlen(fr);
        while (pl > 0 && fr[pl - 1] == '/')
            pl--;
        if (pl + 1 < sizeof c->prefix) {
            memcpy(c->prefix, fr, pl);
            c->prefix[pl] = '/';
            c->prefix[pl + 1] = 0;
            c->prefix_len = (int)pl + 1;
            c->prefix_ok = true;
        } else {
            c->prefix_ok = false;
        }
        c->irseq = irseq;
        c->jump = true;
    }
    free(np);
    free(owned);
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


/* Resolve a caller operand to its KVSpace key into stack buffer `out`. */
static const char *resolve_read_path_buf(kvspace_t *kv, const char *frame_root,
                                         const char *name, char *out,
                                         size_t cap) {
    if (!name || !name[0] || is_literal(name))
        return NULL;
    if (name[0] == '/') {
        size_t n = strlen(name);
        if (n + 1 > cap)
            return NULL;
        memcpy(out, name, n + 1);
        return out;
    }
    if (name[0] == '*') {
        char *r = kvlangBuiltinResolveWriteSlot(kv, frame_root, name);
        if (!r)
            return NULL;
        size_t n = strlen(r);
        if (n + 1 > cap) {
            free(r);
            return NULL;
        }
        memcpy(out, r, n + 1);
        free(r);
        return out;
    }
    if (strncmp(name, MEMBER_SEP, MEMBER_SEP_LEN) == 0)
        return NULL;
    size_t fl = strlen(frame_root);
    while (fl > 0 && frame_root[fl - 1] == '/')
        fl--;
    size_t nl = strlen(name);
    if (fl + 1 + nl + 1 > cap)
        return NULL;
    memcpy(out, frame_root, fl);
    out[fl] = '/';
    memcpy(out + fl + 1, name, nl + 1);
    return out;
}

static __thread kvlangParkedFrame_t parked_frames[MAX_STACK_DEPTH + 1];
static __thread int parked_max_depth = 0;

struct kvlangParkedFrame *kvlangParkedFrameGet(kvspace_t *kv, const char *vtid,
                                               int depth) {
    if (!kv || !vtid || depth < 1 || depth > MAX_STACK_DEPTH)
        return NULL;
    kvlangParkedFrame_t *pf = &parked_frames[depth];
    if (pf->warm && pf->handle == kv->handle && strcmp(pf->vtid, vtid) == 0)
        return pf;
    return NULL;
}

static void parked_frame_destroy(kvspace_t *kv, kvlangParkedFrame_t *pf) {
    if (!pf->warm)
        return;
    if (kv && kv->handle == pf->handle && pf->frame_root[0] && pf->stack_fr[0]) {
        char err[256];
        kvlangKvspaceDeleteExtensionIndex(kv, pf->stack_fr, err, sizeof err);
        kvlangKvspaceDeleteTree(kv, pf->frame_root, err, sizeof err);
    }
    pf->warm = false;
    pf->active = false;
    pf->func_key[0] = 0;
    pf->retpc_val[0] = 0;
}

void kvlangParkedFramesCleanupAbove(kvspace_t *kv, const char *vtid,
                                    int min_depth) {
    if (min_depth < 1)
        min_depth = 1;
    for (int d = parked_max_depth; d > min_depth; d--) {
        kvlangParkedFrame_t *pf = &parked_frames[d];
        if (!pf->warm)
            continue;
        if (!vtid || !vtid[0] ||
            (kv && pf->handle == kv->handle && strcmp(pf->vtid, vtid) == 0)) {
            parked_frame_destroy(kv, pf);
        }
    }
    while (parked_max_depth > 0 && !parked_frames[parked_max_depth].warm)
        parked_max_depth--;
}

void kvlangParkedFramesInvalidateAll(void *handle) {
    for (int d = 1; d <= parked_max_depth; d++) {
        if (!handle || parked_frames[d].handle == handle) {
            parked_frames[d].warm = false;
            parked_frames[d].active = false;
            parked_frames[d].func_key[0] = 0;
            parked_frames[d].retpc_val[0] = 0;
        }
    }
    while (parked_max_depth > 0 && !parked_frames[parked_max_depth].warm)
        parked_max_depth--;
}

static int handle_return(kvspace_t *kv, const char *vtid, const char *pc,
                         char *out_next, size_t next_cap,
                         const kvlangExecCursor *cursor) {
    out_next[0] = 0;
    char fr[KV_KEY_MAX];
    if (!kvlangKeytreeFrameRootBuffer(pc, fr, sizeof fr))
        return -1;
    int d = kvlangKeytreeFrameNum(fr);
    if (d > 1) {
        if (cursor && d <= MAX_STACK_DEPTH) {
            kvlangParkedFrame_t *pf = &parked_frames[d];
            if (pf->warm && pf->handle == kv->handle &&
                strcmp(pf->vtid, vtid) == 0 && pf->retpc_val[0]) {
                size_t rl = strlen(pf->retpc_val);
                if (rl + 1 <= next_cap) {
                    memcpy(out_next, pf->retpc_val, rl + 1);
                    return 0;
                }
            }
        }
        char rk[KV_KEY_MAX];
        kvlangKeytreeFrameReturnPc(fr, rk, sizeof rk);
        kvlangXvalue_t v;
        kvlangXvalueZero(&v);
        kvlangKvspaceGetOne(kv, rk, &v);
        bool ok = false;
        if (!kvlangXvalueNone(&v) &&
            kvlangXvalueKindIs(&v, KVSPACE_KIND_CHAR_UTF8)) {
            const char *src = (const char *)xh_body(v.bytes);
            size_t n = (size_t)xh_content_len(v.bytes);
            if (n > 0 && n + 1 <= next_cap) {
                memcpy(out_next, src, n);
                out_next[n] = 0;
                ok = true;
            }
        } else if (!kvlangXvalueNone(&v)) {
            char *tmp = kvlangXvalueValueString(&v);
            if (tmp && tmp[0] && strlen(tmp) + 1 <= next_cap) {
                strcpy(out_next, tmp);
                ok = true;
            }
            free(tmp);
        }
        if (!ok) {
            char msg[512];
            snprintf(msg, sizeof msg,
                     "RuntimeError: broken return chain: frame %.180s has no "
                     "returnpc (pc=%.180s)",
                     fr, pc);
            kvlangVthreadSetError(kv, vtid, pc, msg);
            out_next[0] = 0;
            return -1;
        }
    }
    return 0;
}

#define FUNC_SIG_CACHE_CAP 64
#define FUNC_SIG_MAX_CACHE_PARAMS 16
typedef struct {
    void *handle;
    char func_key[256];
    int nr, nw;
    char read_lt[FUNC_SIG_MAX_CACHE_PARAMS][64];
    char write_lt[FUNC_SIG_MAX_CACHE_PARAMS][64];
    bool valid;
} func_sig_entry_t;

static __thread func_sig_entry_t sig_cache[FUNC_SIG_CACHE_CAP];

static uint32_t sig_hash(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) {
        h ^= (uint8_t)*s++;
        h *= 16777619u;
    }
    return h;
}

/* Read the declared target type; the argument value may be None. */
static void param_decl_type(kvspace_t *kv, const char *func_key, int x,
                            char *lt, size_t cap) {
    if (cap)
        lt[0] = 0;
    char pk[KV_KEY_MAX];
    kvlangKeyFormat(pk, sizeof pk, "%s.[0,%d]", func_key, x);
    kvlangXvalue_t dv;
    kvlangXvalueZero(&dv);
    kvlangKvspaceGetOne(kv, pk, &dv);
    if (!kvlangXvalueNone(&dv)) {
        const uint8_t *ab = xh_body_of(&dv);
        int32_t al = xh_content_len_of(&dv);
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

static const func_sig_entry_t *lookup_func_sig(kvspace_t *kv,
                                               const char *func_key,
                                               const char *sig_key,
                                               kvlangXvalue_t *out_sig) {
    size_t fkl = strlen(func_key);
    uint32_t slot = sig_hash(func_key) & (FUNC_SIG_CACHE_CAP - 1);
    func_sig_entry_t *e = &sig_cache[slot];
    if (e->valid && e->handle == kv->handle && fkl < sizeof e->func_key &&
        strcmp(e->func_key, func_key) == 0) {
        return e;
    }
    kvlangXvalueZero(out_sig);
    kvlangKvspaceGetOne(kv, sig_key, out_sig);
    if (kvlangXvalueNone(out_sig) ||
        !kvlangXvalueKindIs(out_sig, KVSPACE_KIND_RWFUNC))
        return NULL;
    const uint8_t *sbody = xh_body(out_sig->bytes);
    int nr = sbody[0] | (sbody[1] << 8);
    int nw = sbody[2] | (sbody[3] << 8);
    if (fkl < sizeof e->func_key && nr >= 0 &&
        nr <= FUNC_SIG_MAX_CACHE_PARAMS && nw >= 0 &&
        nw <= FUNC_SIG_MAX_CACHE_PARAMS) {
        e->handle = kv->handle;
        memcpy(e->func_key, func_key, fkl + 1);
        e->nr = nr;
        e->nw = nw;
        for (int i = 0; i < nr; i++)
            param_decl_type(kv, func_key, -(i + 1), e->read_lt[i],
                            sizeof e->read_lt[i]);
        for (int i = 0; i < nw; i++)
            param_decl_type(kv, func_key, i + 1, e->write_lt[i],
                            sizeof e->write_lt[i]);
        e->valid = true;
        return e;
    }
    return NULL;
}

static uint32_t encode_ptr_buf(const char *type, const char *target,
                               uint8_t *buf, size_t cap) {
    if (!type || !target || strcmp(type, "any") == 0)
        return 0;
    size_t lt = strlen(type);
    size_t tlen = strlen(target);
    if (tlen == 0)
        return 0;
    int pow = 5;
    while (pow <= 31 && (uint64_t)lt + XH_PREFIX > (1ULL << pow))
        pow++;
    if (pow > 31)
        return 0;
    uint32_t headlen = 1u << pow;
    if (headlen + tlen > cap)
        return 0;
    memset(buf, 0, headlen);
    buf[0] = (uint8_t)pow;
    buf[1] = 1u | 4u; /* KVSPACE_XH_SLACK | KVSPACE_XH_PTR_FLAG */
    for (int i = 0; i < 8; i++) {
        buf[2 + i] = (uint8_t)((uint64_t)tlen >> (8 * i));
        buf[10 + i] = (uint8_t)((uint64_t)tlen >> (8 * i));
    }
    if (lt)
        memcpy(buf + XH_PREFIX, type, lt);
    memcpy(buf + headlen, target, tlen);
    return headlen + (uint32_t)tlen;
}

static int write_kv_raw(kvspace_t *kv, const char *key, const uint8_t *bytes,
                        uint32_t len, char *err, uint32_t err_cap) {
    if (!bytes || len < XH_PREFIX)
        return -1;
    kvlangBodyInvalidateKind(key, xh_langtype(bytes), xh_langtype_len(bytes));
    return kvspaceSetValue(kv->handle, key, bytes, len, 0, 0, err, err_cap);
}

static int write_kv_raw_ko(kvspace_t *kv, kvspaceKeyOff *ko, const char *key,
                           const uint8_t *bytes, uint32_t len, char *err,
                           uint32_t err_cap) {
    if (!bytes || len < XH_PREFIX)
        return -1;
    kvlangBodyInvalidateKind(key, xh_langtype(bytes), xh_langtype_len(bytes));
    if (ko)
        return kvspaceKeyOffSet(ko, bytes, len, err, err_cap);
    return kvspaceSetValue(kv->handle, key, bytes, len, 0, 0, err, err_cap);
}

static int write_arg_ptr_ko(kvspace_t *kv, kvspaceKeyOff *ko,
                            const char *arg_key, const char *type,
                            const char *value_key, char *err,
                            uint32_t err_cap) {
    if (!value_key)
        return -1;
    while (*type == '*' || *type == '@')
        type++;
    uint8_t abuf[128 + KV_KEY_MAX];
    uint32_t alen = encode_ptr_buf(type, value_key, abuf, sizeof abuf);
    if (!alen) {
        kvlangXvalue_t arg_ptr;
        kvlangXvalueNewPtr(&arg_ptr, type, value_key);
        int rc = kvlangXvalueNone(&arg_ptr)
                     ? -1
                     : write_kv_raw_ko(kv, ko, arg_key, arg_ptr.bytes,
                                       arg_ptr.length, err, err_cap);
        kvlangXvalueFree(&arg_ptr);
        return rc;
    }
    return write_kv_raw_ko(kv, ko, arg_key, abuf, alen, err, err_cap);
}

static int write_slot_ptr(kvspace_t *kv, const char *slot_key, const char *type,
                          const char *arg_key, char *err, uint32_t err_cap) {
    while (*type == '*' || *type == '@')
        type++;
    uint8_t sbuf[128 + KV_KEY_MAX];
    uint32_t slen = encode_ptr_buf(type, arg_key, sbuf, sizeof sbuf);
    if (!slen) {
        kvlangXvalue_t frame_ptr;
        kvlangXvalueNewPtr(&frame_ptr, type, arg_key);
        int rc = kvlangXvalueNone(&frame_ptr)
                     ? -1
                     : write_kv_raw(kv, slot_key, frame_ptr.bytes,
                                    frame_ptr.length, err, err_cap);
        kvlangXvalueFree(&frame_ptr);
        return rc;
    }
    return write_kv_raw(kv, slot_key, sbuf, slen, err, err_cap);
}

#define MAX_BIND_PAIRS (MAX_PARAMS * 5)

static int add_param_binding(kvlangKvPair_t *pairs, bool *owned, int *np,
                             const char *frame_root, int x, const char *type,
                             const char *value_key) {
    if (!value_key || *np > MAX_BIND_PAIRS - 2)
        return -1;
    while (*type == '*' || *type == '@')
        type++;
    /* 空 kindexp（`name:` / `*`）：Ptr 目标不约束，照常绑定（[[文法与合法性]]）。 */
    char *arg_key = kvlangStringFormat("%s/%sarg[0,%d]", frame_root,
                               RUNTIME_MEMBER_SEP, x);
    char *slot_key = kvlangStringFormat("%s/[0,%d]", frame_root, x);
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
    pairs[*np] = (kvlangKvPair_t){arg_key, arg_ptr};
    owned[(*np)++] = true;
    pairs[*np] = (kvlangKvPair_t){slot_key, frame_ptr};
    owned[(*np)++] = true;
    return 0;
}

/* Create the callee frame and write its entry PC into ep_out. */
static int handle_call_buf(kvspace_t *kv, const char *pc,
                           kvlangRwirInst_t *inst, char *ep_out,
                           size_t ep_cap, kvlangExecCursor *cursor) {
    char vtid_b[KV_KEY_MAX];
    const char *vtid = kvlangKeytreeVthreadIdFromPc(pc, vtid_b, sizeof vtid_b);
    const char *fn = inst->reads[0].name;
    assert(fn);
    char pkg_b[KV_KEY_MAX] = "";
    const char *name = fn;
    const char *lp = "/lib/";
    if (strncmp(fn, lp, 5) == 0) {
        const char *rest = fn + 5;
        const char *dot = rfind_separator(rest);
        if (dot) {
            size_t pl = (size_t)(dot - rest);
            if (pl < sizeof pkg_b) {
                memcpy(pkg_b, rest, pl);
                pkg_b[pl] = 0;
            }
            name = dot + MEMBER_SEP_LEN;
        } else {
            name = rest;
        }
    } else {
        const char *dot = rfind_separator(fn);
        if (dot) {
            size_t pl = (size_t)(dot - fn);
            if (pl < sizeof pkg_b) {
                memcpy(pkg_b, fn, pl);
                pkg_b[pl] = 0;
            }
            name = dot + MEMBER_SEP_LEN;
        } else {
            char ff[KV_KEY_MAX];
            if (kvlangKeytreeFrameRootBuffer(pc, ff, sizeof ff)) {
                char stk[KV_KEY_MAX], lk[KV_KEY_MAX];
                if (kvlangKeytreeStackBuffer(ff, stk, sizeof stk)) {
                    kvlangKeyFormat(lk, sizeof lk, "%s%s", stk, SEG_LIB);
                    kvlangXvalue_t lv;
                    kvlangXvalueZero(&lv);
                    kvlangKvspaceGetOne(kv, lk, &lv);
                    char funcdir[KV_KEY_MAX];
                    if (!kvlangXvalueNone(&lv) &&
                        kvlangXvalueKindIs(&lv, KVSPACE_KIND_CHAR_UTF8)) {
                        size_t fl = (size_t)xh_content_len(lv.bytes);
                        if (fl > 5 && fl < sizeof funcdir) {
                            memcpy(funcdir, xh_body(lv.bytes), fl);
                            funcdir[fl] = 0;
                            char *rel = funcdir + 5;
                            size_t rl = strlen(rel);
                            if (rl > 0 && rel[rl - 1] == '/')
                                rel[rl - 1] = '\0';
                            const char *sep = rfind_separator(rel);
                            if (sep) {
                                size_t cpl = (size_t)(sep - rel);
                                if (cpl < sizeof pkg_b) {
                                    char sk[KV_KEY_MAX];
                                    kvlangKeyFormat(sk, sizeof sk,
                                                    "%s%.*s%s%s/[0,0]",
                                                    LIB_ROOT PATH_SEP,
                                                    (int)cpl, rel, MEMBER_SEP,
                                                    fn);
                                    kvlangXvalue_t sv;
                                    kvlangXvalueZero(&sv);
                                    kvlangKvspaceGetOne(kv, sk, &sv);
                                    if (!kvlangXvalueNone(&sv) &&
                                        kvlangXvalueKindIs(
                                            &sv, KVSPACE_KIND_RWFUNC)) {
                                        memcpy(pkg_b, rel, cpl);
                                        pkg_b[cpl] = 0;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    char func_key[KV_KEY_MAX], func_dir[KV_KEY_MAX], sig_key[KV_KEY_MAX];
    if (pkg_b[0])
        kvlangKeyFormat(func_key, sizeof func_key, "%s%s%s%s",
                        LIB_ROOT PATH_SEP, pkg_b, MEMBER_SEP, name);
    else
        kvlangKeyFormat(func_key, sizeof func_key, "%s%s", LIB_ROOT PATH_SEP,
                        name);
    kvlangKeyFormat(func_dir, sizeof func_dir, "%s/", func_key);
    kvlangKeyFormat(sig_key, sizeof sig_key, "%s[0,0]", func_dir);

    kvlangXvalue_t sig;
    kvlangXvalueZero(&sig);
    const func_sig_entry_t *fsig =
        lookup_func_sig(kv, func_key, sig_key, &sig);
    int nr = 0, nw = 0;
    if (fsig) {
        nr = fsig->nr;
        nw = fsig->nw;
    } else {
        if (kvlangXvalueNone(&sig) ||
            !kvlangXvalueKindIs(&sig, KVSPACE_KIND_RWFUNC)) {
            char rk[KV_KEY_MAX];
            kvlangKeyFormat(rk, sizeof rk, "%s%s", LIB_ROOT PATH_SEP, fn);
            kvlangXvalue_t rv;
            kvlangXvalueZero(&rv);
            kvlangKvspaceGetOne(kv, rk, &rv);
            char msg[256];
            if (!kvlangXvalueNone(&rv) &&
                kvlangXvalueKindIs(&rv, KVSPACE_KIND_DEF_RWIR))
                snprintf(msg, sizeof msg,
                         "NameError: rwir 未注册/签名不匹配: %s", fn);
            else if (!kvlangXvalueNone(&sig))
                snprintf(msg, sizeof msg, "NameError: %s 不是 rwfunc (kind=%s)",
                         fn, kvlangXvalueKind(&sig));
            else
                snprintf(msg, sizeof msg, "NameError: rwfunc not found: %s",
                         fn);
            kvlangVthreadSetError(kv, vtid, pc, msg);
            return -1;
        }
        const uint8_t *sbody = xh_body(sig.bytes);
        nr = sbody[0] | (sbody[1] << 8);
        nw = sbody[2] | (sbody[3] << 8);
        if (nr > MAX_PARAMS || nw > MAX_PARAMS) {
            kvlangVthreadSetError(kv, vtid, pc,
                                  "TypeError: too many parameters");
            return -1;
        }
    }

    char caller_fr[KV_KEY_MAX], frame_root[KV_KEY_MAX], stack_fr[KV_KEY_MAX];
    if (!kvlangKeytreeFrameRootBuffer(pc, caller_fr, sizeof caller_fr))
        return -1;
    int d = kvlangKeytreeFrameNum(pc);
    int callee_d = d + 1;
    char err[256];

    char npc[KV_KEY_MAX];
    if (cursor && cursor->prefix_ok) {
        if (kvlangRwirNextPcFromPrefix(cursor->prefix,
                                       (size_t)cursor->prefix_len,
                                       cursor->irseq, npc, sizeof npc) == 0)
            kvlangDie("next pc exceeds %zu bytes", sizeof npc);
    } else if (kvlangRwirNextPcBuffer(pc, npc, sizeof npc) == 0) {
        kvlangDie("next pc exceeds %zu bytes", sizeof npc);
    }

    kvlangParkedFrame_t *pf = NULL;
    bool can_park = cursor != NULL && callee_d > 1 &&
                    callee_d <= MAX_STACK_DEPTH &&
                    nr <= KVLANG_PARK_MAX_PARAMS &&
                    nw <= KVLANG_PARK_MAX_PARAMS &&
                    strlen(vtid) < sizeof pf->vtid &&
                    strlen(func_key) < sizeof pf->func_key &&
                    strlen(func_dir) < sizeof pf->func_dir;
    bool warm_hit = false;
    if (can_park) {
        pf = &parked_frames[callee_d];
        if (pf->warm && pf->handle == kv->handle &&
            strcmp(pf->vtid, vtid) == 0 &&
            strcmp(pf->func_key, func_key) == 0) {
            warm_hit = true;
            strcpy(frame_root, pf->frame_root);
            strcpy(stack_fr, pf->stack_fr);
        } else if (pf->warm) {
            parked_frame_destroy(kv, pf);
        }
    }

    if (!warm_hit) {
        kvlangKeyFormat(frame_root, sizeof frame_root, "%s/%s/[%d]",
                        VTHREAD_ROOT, vtid, callee_d);
        kvlangKeyFormat(stack_fr, sizeof stack_fr, "%s/", frame_root);
        kvlangKvspaceDeleteTree(kv, frame_root, err, sizeof err);
        if (kvlangKvspaceMakeIndex(kv, stack_fr, 0, err, sizeof err) != 0 ||
            kvlangKvspaceExtensionIndex(kv, stack_fr, func_dir, err,
                                        sizeof err) != 0) {
            kvlangVthreadSetError(kv, vtid, pc, err);
            return -1;
        }
        char retpc[KV_KEY_MAX], callpc[KV_KEY_MAX], seglib[KV_KEY_MAX];
        kvlangKeytreeFrameReturnPc(frame_root, retpc, sizeof retpc);
        kvlangKeytreeFrameCallPc(frame_root, callpc, sizeof callpc);
        kvlangKeyFormat(ep_out, ep_cap, "%s/[1,0]", frame_root);
        kvlangKeyFormat(seglib, sizeof seglib, "%s%s", stack_fr, SEG_LIB);

        uint8_t sbuf[64 + KV_KEY_MAX];
        uint32_t nl_npc = (uint32_t)strlen(npc);
        uint32_t nl_ep = (uint32_t)strlen(ep_out);
        uint32_t nl_fn = (uint32_t)strlen(func_key);
        uint32_t wn_npc = kvlangSlackEncodeChar((const uint8_t *)npc, nl_npc,
                                                nl_npc, sbuf, sizeof sbuf);
        int sys_rc = write_kv_raw(kv, retpc, sbuf, wn_npc, err, sizeof err);
        if (sys_rc == 0) {
            uint32_t wn_ep = kvlangSlackEncodeChar((const uint8_t *)ep_out,
                                                   nl_ep, nl_ep, sbuf,
                                                   sizeof sbuf);
            sys_rc = write_kv_raw(kv, callpc, sbuf, wn_ep, err, sizeof err);
        }
        if (sys_rc == 0) {
            uint32_t wn_fn = kvlangSlackEncodeChar((const uint8_t *)func_key,
                                                   nl_fn, nl_fn, sbuf,
                                                   sizeof sbuf);
            sys_rc = write_kv_raw(kv, seglib, sbuf, wn_fn, err, sizeof err);
        }
        if (sys_rc != 0) {
            kvlangVthreadSetError(kv, vtid, pc,
                                  "RuntimeError: cannot persist call frame");
            goto fail_frame;
        }
        if (pf && strlen(frame_root) < sizeof pf->frame_root &&
            strlen(stack_fr) < sizeof pf->stack_fr &&
            strlen(ep_out) < sizeof pf->entry_pc &&
            strlen(retpc) < sizeof pf->retpc_key &&
            strlen(npc) < sizeof pf->retpc_val) {
            memset(pf, 0, sizeof *pf);
            pf->handle = kv->handle;
            strcpy(pf->vtid, vtid);
            pf->depth = callee_d;
            strcpy(pf->frame_root, frame_root);
            strcpy(pf->stack_fr, stack_fr);
            strcpy(pf->func_key, func_key);
            strcpy(pf->func_dir, func_dir);
            strcpy(pf->entry_pc, ep_out);
            strcpy(pf->retpc_key, retpc);
            strcpy(pf->retpc_val, npc);
            kvspaceKeyOffBind(&pf->retpc_ko, kv, pf->retpc_key);
            pf->nr = nr;
            pf->nw = nw;
            for (int i = 0; i < nr; i++) {
                kvlangKeyFormat(pf->r_arg_key[i], sizeof pf->r_arg_key[i],
                                "%s/%sarg[0,-%d]", frame_root,
                                RUNTIME_MEMBER_SEP, i + 1);
                kvlangKeyFormat(pf->r_lit_key[i], sizeof pf->r_lit_key[i],
                                "%s/._lit%d", frame_root, i);
                pf->r_arg_ko[i].kvspace = kv;
                pf->r_arg_ko[i].key = pf->r_arg_key[i];
                pf->r_lit_ko[i].kvspace = kv;
                pf->r_lit_ko[i].key = pf->r_lit_key[i];
            }
            for (int i = 0; i < nw; i++) {
                kvlangKeyFormat(pf->w_arg_key[i], sizeof pf->w_arg_key[i],
                                "%s/%sarg[0,%d]", frame_root,
                                RUNTIME_MEMBER_SEP, i + 1);
                pf->w_arg_ko[i].kvspace = kv;
                pf->w_arg_ko[i].key = pf->w_arg_key[i];
            }
        } else {
            pf = NULL;
        }
    } else {
        size_t epl = strlen(pf->entry_pc);
        if (epl + 1 <= ep_cap)
            memcpy(ep_out, pf->entry_pc, epl + 1);
        else
            kvlangKeyFormat(ep_out, ep_cap, "%s/[1,0]", frame_root);
        pf->r_changed_mask = 0;
        pf->w_changed_mask = 0;
        if (strcmp(pf->retpc_val, npc) != 0) {
            uint8_t sbuf[64 + KV_KEY_MAX];
            uint32_t nl_npc = (uint32_t)strlen(npc);
            uint32_t wn_npc = kvlangSlackEncodeChar(
                (const uint8_t *)npc, nl_npc, nl_npc, sbuf, sizeof sbuf);
            if (write_kv_raw_ko(kv, &pf->retpc_ko, pf->retpc_key, sbuf, wn_npc,
                                err, sizeof err) != 0) {
                kvlangVthreadSetError(
                    kv, vtid, pc, "RuntimeError: cannot persist call frame");
                goto fail_frame;
            }
            if (nl_npc < sizeof pf->retpc_val)
                memcpy(pf->retpc_val, npc, nl_npc + 1);
            else
                pf->retpc_val[0] = 0;
        }
    }

    for (int i = 0; i < nr; i++) {
        if (i + 1 >= inst->read_count) {
            snprintf(err, sizeof err, "TypeError: missing read argument %d",
                     i + 1);
            kvlangVthreadSetError(kv, vtid, pc, err);
            goto fail_frame;
        }
        kvlangParam_t *arg = &inst->reads[i + 1];
        char lt_buf[256] = {0};
        const char *lt = fsig ? fsig->read_lt[i] : lt_buf;
        if (!fsig)
            param_decl_type(kv, func_key, -(i + 1), lt_buf, sizeof lt_buf);
        bool concrete = !arg->is_address && !kvlangXvalueNone(&arg->value) &&
                        !kvlangXvalueKindIs(&arg->value, KVSPACE_KIND_RWIR) &&
                        !kvlangXvalueKindIs(&arg->value, KVSPACE_KIND_RWFUNC);
        bool literal = concrete || is_literal(arg->name);
        char rk_buf[KV_KEY_MAX];
        const char *rk = NULL;
        if (literal) {
            if (pf) {
                rk = pf->r_lit_key[i];
            } else {
                kvlangKeyFormat(rk_buf, sizeof rk_buf, "%s/._lit%d", frame_root,
                                i);
                rk = rk_buf;
            }
            if (concrete) {
                if (write_kv_raw_ko(kv, pf ? &pf->r_lit_ko[i] : NULL, rk,
                                    arg->value.bytes, arg->value.length, err,
                                    sizeof err) != 0) {
                    kvlangVthreadSetError(kv, vtid, pc, err);
                    goto fail_frame;
                }
            } else if (warm_hit && pf) {
                /* "null" literal on a warm frame must clear any previous ._lit value */
                kvlangKvspaceDelete(kv, rk, err, sizeof err);
                pf->r_lit_ko[i].resolved = false;
            }
        } else {
            rk = resolve_read_path_buf(kv, caller_fr, arg->name, rk_buf,
                                       sizeof rk_buf);
        }
        if (!rk) {
            snprintf(err, sizeof err, "TypeError: cannot bind read argument %d",
                     i + 1);
            kvlangVthreadSetError(kv, vtid, pc, err);
            goto fail_frame;
        }
        if (warm_hit && pf) {
            if (strcmp(pf->r_target[i], rk) != 0) {
                if (write_arg_ptr_ko(kv, &pf->r_arg_ko[i], pf->r_arg_key[i], lt,
                                     rk, err, sizeof err) != 0) {
                    if (!err[0])
                        snprintf(err, sizeof err,
                                 "TypeError: cannot bind read argument %d",
                                 i + 1);
                    kvlangVthreadSetError(kv, vtid, pc, err);
                    goto fail_frame;
                }
                if (strlen(rk) < sizeof pf->r_target[i])
                    strcpy(pf->r_target[i], rk);
                else
                    pf->r_target[i][0] = 0;
                pf->r_changed_mask |= (1u << i);
            }
        } else {
            char arg_key_b[KV_KEY_MAX], slot_key_b[KV_KEY_MAX];
            const char *arg_key = pf ? pf->r_arg_key[i] : arg_key_b;
            if (!pf)
                kvlangKeyFormat(arg_key_b, sizeof arg_key_b, "%s/%sarg[0,-%d]",
                                frame_root, RUNTIME_MEMBER_SEP, i + 1);
            kvlangKeyFormat(slot_key_b, sizeof slot_key_b, "%s/[0,-%d]",
                            frame_root, i + 1);
            if (write_arg_ptr_ko(kv, pf ? &pf->r_arg_ko[i] : NULL, arg_key, lt,
                                 rk, err, sizeof err) != 0 ||
                write_slot_ptr(kv, slot_key_b, lt, arg_key, err, sizeof err) !=
                    0) {
                if (!err[0])
                    snprintf(err, sizeof err,
                             "TypeError: cannot bind read argument %d", i + 1);
                kvlangVthreadSetError(kv, vtid, pc, err);
                goto fail_frame;
            }
            if (pf && strlen(rk) < sizeof pf->r_target[i])
                strcpy(pf->r_target[i], rk);
        }
    }
    for (int i = 0; i < nw; i++) {
        char lt_buf[256] = {0};
        const char *lt = fsig ? fsig->write_lt[i] : lt_buf;
        if (!fsig)
            param_decl_type(kv, func_key, i + 1, lt_buf, sizeof lt_buf);
        char wk_buf[KV_KEY_MAX];
        const char *wk =
            i < inst->write_count
                ? resolve_read_path_buf(kv, caller_fr, inst->writes[i].name,
                                        wk_buf, sizeof wk_buf)
                : NULL;
        if (!wk) {
            snprintf(err, sizeof err,
                     "TypeError: cannot bind write argument %d", i + 1);
            kvlangVthreadSetError(kv, vtid, pc, err);
            goto fail_frame;
        }
        if (warm_hit && pf) {
            if (strcmp(pf->w_target[i], wk) != 0) {
                if (write_arg_ptr_ko(kv, &pf->w_arg_ko[i], pf->w_arg_key[i], lt,
                                     wk, err, sizeof err) != 0) {
                    if (!err[0])
                        snprintf(err, sizeof err,
                                 "TypeError: cannot bind write argument %d",
                                 i + 1);
                    kvlangVthreadSetError(kv, vtid, pc, err);
                    goto fail_frame;
                }
                if (strlen(wk) < sizeof pf->w_target[i])
                    strcpy(pf->w_target[i], wk);
                else
                    pf->w_target[i][0] = 0;
                pf->w_changed_mask |= (1u << i);
            }
        } else {
            char arg_key_b[KV_KEY_MAX], slot_key_b[KV_KEY_MAX];
            const char *arg_key = pf ? pf->w_arg_key[i] : arg_key_b;
            if (!pf)
                kvlangKeyFormat(arg_key_b, sizeof arg_key_b, "%s/%sarg[0,%d]",
                                frame_root, RUNTIME_MEMBER_SEP, i + 1);
            kvlangKeyFormat(slot_key_b, sizeof slot_key_b, "%s/[0,%d]",
                            frame_root, i + 1);
            if (write_arg_ptr_ko(kv, pf ? &pf->w_arg_ko[i] : NULL, arg_key, lt,
                                 wk, err, sizeof err) != 0 ||
                write_slot_ptr(kv, slot_key_b, lt, arg_key, err, sizeof err) !=
                    0) {
                if (!err[0])
                    snprintf(err, sizeof err,
                             "TypeError: cannot bind write argument %d", i + 1);
                kvlangVthreadSetError(kv, vtid, pc, err);
                goto fail_frame;
            }
            if (pf && strlen(wk) < sizeof pf->w_target[i])
                strcpy(pf->w_target[i], wk);
        }
    }
    if (pf) {
        if (!warm_hit) {
            pf->r_changed_mask = (1u << nr) - 1u;
            pf->w_changed_mask = (1u << nw) - 1u;
        }
        pf->warm = true;
        pf->active = true;
        if (callee_d > parked_max_depth)
            parked_max_depth = callee_d;
    }
    return 0;

fail_frame:
    if (pf) {
        pf->warm = false;
        pf->active = false;
        pf->func_key[0] = 0;
    }
    kvlangKvspaceDeleteExtensionIndex(kv, stack_fr, err, sizeof err);
    kvlangKvspaceDeleteTree(kv, frame_root, err, sizeof err);
    ep_out[0] = 0;
    return -1;
}

int kvlangControlCall(kvlangFrame_t *f) {
    char sub[KV_KEY_MAX];
    if (handle_call_buf(f->kvspace, f->pc, f->instruction, sub, sizeof sub,
                        f->cursor) != 0 ||
        !sub[0])
        return -1;
    kvlangVthreadNextPc(f, sub, "running");
    return 0;
}

int kvlangControlReturn(kvlangFrame_t *f) {
    char parent[KV_KEY_MAX];
    if (handle_return(f->kvspace, f->vthread_id, f->pc, parent, sizeof parent,
                      f->cursor) != 0)
        return -1;
    if (!parent[0]) {
        f->persist_failed =
            kvlangVthreadSetDone(f->kvspace, f->vthread_id, "ok") != 0;
    } else {
        kvlangVthreadNextPc(f, parent, "running");
    }
    if (f->persist_failed)
        return -1;
    if (f->cursor && parent[0]) {
        int d = kvlangKeytreeFrameNum(f->pc);
        if (d > 1 && d <= MAX_STACK_DEPTH) {
            kvlangParkedFrame_t *pf = &parked_frames[d];
            if (pf->warm && pf->handle == f->kvspace->handle &&
                strcmp(pf->vtid, f->vthread_id) == 0) {
                pf->active = false;
                return 0;
            }
        }
    }
    if (!parent[0])
        kvlangParkedFramesCleanupAbove(f->kvspace, f->vthread_id, 1);
    char fr[KV_KEY_MAX], stk[KV_KEY_MAX], err[256];
    if (kvlangKeytreeFrameRootBuffer(f->pc, fr, sizeof fr) &&
        kvlangKeytreeStackBuffer(fr, stk, sizeof stk)) {
        kvlangKvspaceDeleteExtensionIndex(f->kvspace, stk, err, sizeof err);
        kvlangKvspaceDeleteTree(f->kvspace, fr, err, sizeof err);
    } else {
        char *ofr = kvlangKeytreeFrameRoot(f->pc);
        char *ostk = kvlangKeytreeStack(ofr);
        kvlangKvspaceDeleteExtensionIndex(f->kvspace, ostk, err, sizeof err);
        kvlangKvspaceDeleteTree(f->kvspace, ofr, err, sizeof err);
        free(ostk);
        free(ofr);
    }
    return 0;
}

int kvlangControlGoto(kvlangFrame_t *f) {
    kvlangRwirInst_t *inst = f->instruction;
    if (inst->read_count != 1) {
        char msg[128];
        snprintf(msg, sizeof msg, "RuntimeError: goto expects 1 irseq, got %d",
                 inst->read_count);
        kvlangVthreadSetError(f->kvspace, f->vthread_id, f->pc, msg);
        return -1;
    }
    return jump_to(f, 0, &inst->reads[0], OP_GOTO);
}

int kvlangControlBr(kvlangFrame_t *f) {
    kvlangRwirInst_t *inst = f->instruction;
    if (inst->read_count != 3) {
        char msg[128];
        snprintf(msg, sizeof msg,
                 "RuntimeError: br expects cond trueIrseq falseIrseq, got %d",
                 inst->read_count);
        kvlangVthreadSetError(f->kvspace, f->vthread_id, f->pc, msg);
        return -1;
    }
    const kvlangXvalue_t *cv = &inst->reads[0].value;
    if (cv->length == 33 && cv->bytes && cv->bytes[0] == 5 &&
        cv->bytes[1] == 0 && memcmp(cv->bytes + 18, "bool", 5) == 0) {
        bool taken = cv->bytes[32] != 0;
        return jump_to(f, taken ? 1 : 0, &inst->reads[taken ? 1 : 2], OP_BR);
    }
    char *owned = NULL;
    const char *fr = f->frame_root;
    if (!fr) { owned = kvlangKeytreeFrameRoot(f->pc); fr = owned; }
    kvlangXvalue_t cond;
    kvlangXvalueZero(&cond);
    kvlangBuiltinResolveReadValue(f->kvspace, fr, inst->reads[0].name,
                                  &inst->reads[0].value, &cond);
    free(owned);
    if (kvlangXvalueNone(&cond)) {
        kvlangVthreadSetError(f->kvspace, f->vthread_id, f->pc,
                              "TypeError: None in branch condition");
        return -1;
    }
    if (!kvlangXvalueKindIs(&cond, KVSPACE_KIND_BOOL)) {
        char msg[128];
        snprintf(msg, sizeof msg,
                 "TypeError: branch condition must be bool, got %s",
                 kvlangXvalueKind(&cond));
        kvlangVthreadSetError(f->kvspace, f->vthread_id, f->pc, msg);
        return -1;
    }
    bool taken = kvlangScalarI64(kvlangXvalueScalar(&cond)) != 0;
    return jump_to(f, taken ? 1 : 0, &inst->reads[taken ? 1 : 2], OP_BR);
}

/* 以伪 OP_CALL 进入被调函数：reads[0] 是目标函数，其后为实参。 */
int kvlangKvcpuCallOpcode(kvspace_t *kv, const char *vtid, const char *pc,
                          const char *status_known, const char *opcode,
                          kvlangParam_t *reads, int nr,
                          kvlangParam_t *writes, int nw, char *pc_out,
                          size_t pc_out_cap, bool *pc_feedback,
                          struct kvspaceKeyOff *pc_off,
                          struct kvspaceKeyOff *status_off,
                          kvlangExecCursor *cursor) {
    kvlangParam_t rbuf[MAX_PARAMS + 1];
    kvlangRwirInst_t ci;
    ci.opcode = (char *)OP_CALL;
    ci.opcode_id = 0;
    ci.read_count = nr + 1;
    ci.write_count = nw;
    ci.reads = ci.read_count <= MAX_PARAMS + 1
                   ? rbuf
                   : malloc(sizeof(kvlangParam_t) * (size_t)ci.read_count);
    ci.reads[0] = (kvlangParam_t){0};
    ci.reads[0].name = (char *)opcode;
    for (int i = 0; i < nr; i++)
        ci.reads[i + 1] = reads[i];
    ci.writes = writes;
    ci.write_offs = NULL;
    kvlangFrame_t f = {kv, vtid, pc, &ci, NULL};
    f.status_known = status_known;
    f.pc_out = pc_out;
    f.pc_out_cap = pc_out_cap;
    f.pc_off = pc_off;
    f.status_off = status_off;
    f.cursor = cursor;
    int rc = kvlangControlCall(&f);
    if (f.persist_failed)
        rc = -1;
    *pc_feedback = f.pc_feedback;
    if (ci.reads != rbuf)
        free(ci.reads);
    return rc;
}

/* 动态调用：以运行时得到的 funckey 在当前 vthread 造一次 OP_CALL（不新开 vid），
 * pc 落在被调入口，帧结束回到本指令 NextPc。供 native vthread·call 用。 */
int kvlangKvcpuDynamicCall(kvlangFrame_t *f, const char *funckey) {
    return kvlangKvcpuCallOpcode(f->kvspace, f->vthread_id, f->pc, NULL, funckey,
                                 NULL, 0, NULL, 0, f->pc_out, f->pc_out_cap,
                                 &f->pc_feedback, f->pc_off, f->status_off,
                                 f->cursor);
}


char *kvlangKvcpuBootstrap(kvspace_t *kv, const char *vtid,
                           const char *funcname, const char *const *args,
                           int nargs) {
    char *pkg = strdup("");
    char *name = strdup(funcname);
    const char *dot = rfind_separator(funcname);
    if (dot) {
        free(pkg);
        pkg = strndup(funcname, (size_t)(dot - funcname));
        free(name);
        name = strdup(dot + MEMBER_SEP_LEN);
    }
    char *func_key = kvlangKeytreeLibFunc(pkg, name);
    char func_dir[KV_KEY_MAX];
    kvlangKeyFormat(func_dir, sizeof func_dir, "%s/", func_key);

    char sig_key[KV_KEY_MAX];
    kvlangKeyFormat(sig_key, sizeof sig_key, "%s[0,0]", func_dir);
    kvlangXvalue_t sig;
    kvlangXvalueZero(&sig);
    kvlangKvspaceGetOne(kv, sig_key, &sig);
    if (kvlangXvalueNone(&sig) ||
        !kvlangXvalueKindIs(&sig, KVSPACE_KIND_RWFUNC)) {
        char msg[256];
        snprintf(msg, sizeof msg, "Bootstrap: rwir/rwfunc not found: %s",
                 funcname);
        kvlangVthreadSetError(kv, vtid, "", msg);
        free(func_key);
        free(pkg);
        free(name);
        return NULL;
    }
    const uint8_t *sbody = xh_body(sig.bytes);
    int nr = sbody[0] | (sbody[1] << 8);

    char *frame_root = kvlangKeytreeFrameAt(vtid, 1);
    char *stack_fr = kvlangKeytreeStack(frame_root);
    char err[256];
    if (kvlangKvspaceMakeIndex(kv, stack_fr, 0, err, sizeof err) != 0 ||
        kvlangKvspaceExtensionIndex(kv, stack_fr, func_dir, err, sizeof err) != 0) {
        kvlangVthreadSetError(kv, vtid, "", err);
        free(stack_fr);
        free(frame_root);
        free(func_key);
        free(pkg);
        free(name);
        return NULL;
    }

    char *ep = kvlangKeytreeEntryPc(frame_root);
    char callpc[KV_KEY_MAX], seglib[KV_KEY_MAX];
    kvlangKeytreeFrameCallPc(frame_root, callpc, sizeof callpc);
    kvlangKeyFormat(seglib, sizeof seglib, "%s%s", stack_fr, SEG_LIB);
    kvlangXvalue_t v_ep, v_fn;
    kvlangXvalueZero(&v_ep);
    kvlangXvalueZero(&v_fn);
    kvlangXvalueNewCharUtf8(&v_ep, ep);
    kvlangXvalueNewCharUtf8(&v_fn, func_key);
    kvlangKvPair_t sys[2] = {{callpc, v_ep}, {seglib, v_fn}};
    int sys_rc = kvlangXvalueNone(&v_ep) || kvlangXvalueNone(&v_fn) ? -1 :
                 kvlangKvspaceSet(kv, sys, 2, err, sizeof err);
    kvlangXvalueFree(&v_ep);
    kvlangXvalueFree(&v_fn);
    if (sys_rc != 0)
        snprintf(err, sizeof err, "RuntimeError: cannot persist entry frame");

    int bind_error = sys_rc != 0;
    if (!bind_error && (nargs < 0 || nargs > MAX_PARAMS || nr > MAX_PARAMS)) {
        snprintf(err, sizeof err, "TypeError: too many entry parameters");
        bind_error = 1;
    }
    if (nargs > 0 && !bind_error) {
        kvlangKvPair_t pairs[MAX_BIND_PAIRS];
        bool owned[MAX_BIND_PAIRS] = {0};
        int np = 0;
        for (int i = 0; i < nr && i < nargs; i++) {
            char *value_key = kvlangStringFormat("%s/._arglit%d", frame_root, i);
            kvlangXvalue_t av;
            kvlangXvalueZero(&av);
            kvlangBuiltinResolveReadValue(kv, "", args[i], NULL, &av);
            if (!kvlangXvalueNone(&av))
                pairs[np++] = (kvlangKvPair_t){strdup(value_key), av};
            char lt[256] = {0};
            param_decl_type(kv, func_key, -(i + 1), lt, sizeof lt);
            if (add_param_binding(pairs, owned, &np, frame_root, -(i + 1), lt, value_key) != 0) {
                snprintf(err, sizeof err, "TypeError: cannot bind entry argument %d", i + 1);
                bind_error = 1;
                free(value_key);
                break;
            }
            free(value_key);
        }
        if (!bind_error && np > 0 && kvlangKvspaceSet(kv, pairs, np, err, sizeof err) != 0)
            bind_error = 1;
        for (int i = 0; i < np; i++) {
            free(pairs[i].key);
            if (owned[i])
                kvlangXvalueFree(&pairs[i].value);
        }
    }
    if (bind_error) {
        kvlangVthreadSetError(kv, vtid, "", err);
        kvlangKvspaceDeleteTree(kv, frame_root, err, sizeof err);
        free(ep);
        ep = NULL;
    }

    free(stack_fr);
    free(frame_root);
    free(func_key);
    free(pkg);
    free(name);
    return ep;
}
