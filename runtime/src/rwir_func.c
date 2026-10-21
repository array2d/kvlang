#include "runtime_internal.h"
#include "rwir_internal.h"

/* ── resolve ───────────────────────────────────────────────────────── */
/* #116 后 if/while 不再建 scope 帧，当前帧 [d] 即 rwfunc 帧，frame_root 直接可用。 */

void kvlangBuiltinResolveReadValue(kvspace_t *kv, const char *frame_root, const char *name,
                           const kvlangXvalue_t *val, kvlangXvalue_t *out) {
    kvlangXvalueZero(out);
    /* decode 期已按 read_target 取回目标值（out->name 是绝对路径时亦然），直接借用即可。
     * 这一支必须排在绝对路径之前：否则每步都会把已经拿到的值丢掉、再去后端读一遍。 */
    if (val && !kvlangXvalueNone(val) && !kvlangXvalueKindIs(val, KVSPACE_KIND_RWIR) && !kvlangXvalueKindIs(val, KVSPACE_KIND_RWFUNC)) {
        out->bytes = val->bytes; /* Borrowed for this instruction. */
        out->length = val->length;
        return;
    }
    if (name && name[0] == '/') {
        kvlangKvspaceGetOne(kv, name, out);
        return;
    }
    if (!name || !name[0]) return;
    if (name[0] == '*') {
        char *target = kvlangBuiltinResolveWriteSlot(kv, frame_root, name);
        kvlangKvspaceGetOne(kv, target, out);
        free(target);
        return;
    }
    char stkbuf[512];
    char *stk = kvlangKeytreeStackBuffer(frame_root, stkbuf, sizeof stkbuf)
                    ? stkbuf
                    : kvlangKeytreeStack(frame_root);
    kvlangXvalue_t pv; kvlangXvalueZero(&pv);
    kvlangKvspaceGetMember(kv, stk, name, &pv);
    if (kvlangXvalueIsPtr(&pv)) {
        /* 数据 Ptr（&x）：地址值本体，读值不解引用。成员访问另走 kvspace·get 的 member_path。 */
        *out = pv; pv.bytes = NULL; pv.length = 0;
    } else if (!kvlangXvalueNone(&pv)) {
        *out = pv; pv.bytes = NULL; pv.length = 0;
    }
    if (stk != stkbuf)
        free(stk);
}

/* 读参 → 其最终存储键（malloc）：字面量（指令内冻结的具体值，无后端槽）返 NULL；
 * 变量则复用 ResolveWriteSlot——读侧最终槽键与写侧同一路径（普通成员=stk+name，
 * ptr 追链到最终目标），供 xv 系列对 key 直发 GetHead/GetPart/SetPart 做分片读写。 */
char *kvlangBuiltinResolveReadKey(kvspace_t *kv, const char *frame_root, const char *name,
                          const kvlangXvalue_t *val) {
    if (name && name[0] == '/')
        return strdup(name);
    if (val && !kvlangXvalueNone(val) && !kvlangXvalueKindIs(val, KVSPACE_KIND_RWIR) && !kvlangXvalueKindIs(val, KVSPACE_KIND_RWFUNC))
        return NULL;
    if (!name || !name[0]) return NULL;
    return kvlangBuiltinResolveWriteSlot(kv, frame_root, name);
}

char *kvlangBuiltinResolveWriteSlot(kvspace_t *kv, const char *frame_root, const char *name) {
    if (name[0] == '/') return strdup(name);
    char stkbuf[512];
    char *stk = kvlangKeytreeStackBuffer(frame_root, stkbuf, sizeof stkbuf)
                    ? stkbuf
                    : kvlangKeytreeStack(frame_root);
    if (name[0] == '*') {
        kvlangXvalue_t pv; kvlangXvalueZero(&pv);
        kvlangKvspaceGetMember(kv, stk, name + 1, &pv);
        if (!kvlangXvalueIsPtr(&pv)) {
            fprintf(stderr, "panic: %s%s is not a Ptr — frame slot missing, param passing broken\n", stk, name + 1);
            abort();
        }
        char *target = kvlangXvaluePtrTarget(&pv);
        if (strncmp(name + 1, "[0,", 3) == 0) {
            kvlangXvalue_t arg;
            kvlangXvalueZero(&arg);
            kvlangKvspaceGetOne(kv, target, &arg);
            if (!kvlangXvalueIsPtr(&arg)) {
                fprintf(stderr, "panic: %s is not an argument Ptr\n", target);
                abort();
            }
            char *value_key = kvlangXvaluePtrTarget(&arg);
            free(target);
            target = value_key;
        }
        if (stk != stkbuf)
            free(stk);
        return target;
    }
    /* stk+name 精确分配（返回值须自持）。 */
    size_t sl = strlen(stk), nl = strlen(name);
    char *r = malloc(sl + nl + 1);
    if (!r)
        kvlangDie("out of memory building slot key");
    memcpy(r, stk, sl);
    memcpy(r + sl, name, nl);
    r[sl + nl] = 0;
    if (stk != stkbuf)
        free(stk);
    return r;
}

/* ── kvlangDisplay ──────────────────────────────────────────────────────── */

void kvlangXvalueElementAt(const kvlangXvalue_t *v, int i, kvlangXvalue_t *out) {
    kvlangXvalueZero(out);
    int n = kvlangXvalueArrayLen(v);
    if (i < 0 || i >= n) return;
    const char *k = kvlangXvalueKind(v);
    int sz = kvlangXvalueElemSize(k);
    if (sz <= 0) return;
    const uint8_t *body = xh_body_of(v);
    kvlangXvalueNewTlv(out, k, body + i * sz, (uint32_t)sz, 1);
}

void kvlangDisplay(const kvlangXvalue_t *v, char **out);

static void format_array(const kvlangXvalue_t *v, char **out) {
    int n = kvlangXvalueArrayLen(v);
    char **parts = malloc(sizeof(char *) * (size_t)(n > 0 ? n : 1));
    if (!parts)
        kvlangDie("out of memory formatting array");
    size_t total = 2; /* "[" + "]" */
    for (int i = 0; i < n; i++) {
        kvlangXvalue_t e; kvlangXvalueElementAt(v, i, &e);
        kvlangDisplay(&e, &parts[i]);
        kvlangXvalueFree(&e);
        total += strlen(parts[i]) + (i ? 2 : 0);
    }
    char *s = malloc(total + 1);
    if (!s)
        kvlangDie("out of memory formatting array");
    size_t off = 0;
    s[off++] = '[';
    for (int i = 0; i < n; i++) {
        if (i) {
            s[off++] = ',';
            s[off++] = ' ';
        }
        size_t sl = strlen(parts[i]);
        memcpy(s + off, parts[i], sl);
        off += sl;
        free(parts[i]);
    }
    s[off++] = ']';
    s[off] = 0;
    free(parts);
    *out = s;
}

void kvlangDisplay(const kvlangXvalue_t *v, char **out) {
    if (kvlangXvalueIsCharKind(kvlangXvalueKind(v))) { *out = kvlangXvalueValueString(v); return; }
    if (kvlangXvalueArrayLen(v) > 1) { format_array(v, out); return; }
    *out = kvlangXvalueValueString(v);
}

/* ── frame helper ─────────────────────────────────────────────────── */

int kvlangBuiltinReadInputs(kvlangFrame_t *f, kvlangXvalue_t *out, int cap) {
    /* Reuse this instruction's frame root. */
    char *owned = NULL;
    const char *fr = f->frame_root;
    if (!fr) { owned = kvlangKeytreeFrameRoot(f->pc); fr = owned; }
    int n = 0;
    for (int i = 0; i < f->instruction->read_count && n < cap; i++) {
        kvlangBuiltinResolveReadValue(f->kvspace, fr, f->instruction->reads[i].name, &f->instruction->reads[i].value, &out[n]);
        n++;
    }
    free(owned);
    return n;
}

void kvlangBuiltinFreeInputs(kvlangXvalue_t *in, int n) {
    (void)in;
    (void)n;
}

void kvlangBuiltinNextPc(kvlangFrame_t *f) {
    char buf[KV_KEY_MAX];
    if (kvlangRwirNextPcBuffer(f->pc, buf, sizeof buf) == 0)
        kvlangDie("next pc exceeds %zu bytes", sizeof buf);
    kvlangVthreadNextPc(f, buf, "running");
}

int kvlangBuiltinWriteResult(kvlangFrame_t *f, const kvlangXvalue_t *result) {
    if (f->instruction->write_count > 0) {
        kvspaceKeyOff *wo = f->instruction->write_offs
                                ? f->instruction->write_offs[0]
                                : NULL;
        if (wo && result->bytes && result->length > 0 &&
            kvspaceKeyOffSet(wo, result->bytes, result->length, NULL, 0) == 0) {
            kvlangBuiltinNextPc(f);
            return 0;
        }
        char *owned = NULL;
        const char *fr = f->frame_root;
        if (!fr) { owned = kvlangKeytreeFrameRoot(f->pc); fr = owned; }
        char *key = kvlangBuiltinResolveWriteSlot(f->kvspace, fr, f->instruction->writes[0].name);
        free(owned);
        kvlangKvPair_t pair = { key, *result };
        char err[256];
        kvlangKvspaceSet(f->kvspace, &pair, 1, err, sizeof err);
        free(key);
    }
    kvlangBuiltinNextPc(f);
    return 0;
}

int kvlangBuiltinEmitResult(kvlangFrame_t *f, kvlangXvalue_t *v) {
    int rc = kvlangBuiltinWriteResult(f, v);
    kvlangXvalueFree(v);
    return rc;
}

static int emit_inline_scalar(kvlangFrame_t *f, const char *kind,
                              const uint8_t *raw, uint32_t len) {
    uint8_t buf[64];
    uint32_t n = kvlangScalarEncode(kind, raw, len, buf, sizeof buf);
    kvlangXvalue_t v = {buf, n};
    return kvlangBuiltinWriteResult(f, &v); /* 栈缓冲，不 free */
}

int kvlangBuiltinEmitInt64(kvlangFrame_t *f, int64_t n) {
    uint8_t r[8];
    for (int i = 0; i < 8; i++)
        r[i] = (uint8_t)((uint64_t)n >> (i * 8));
    return emit_inline_scalar(f, KVSPACE_KIND_INT64, r, 8);
}

int kvlangBuiltinEmitFloat64(kvlangFrame_t *f, double d) {
    uint64_t u;
    memcpy(&u, &d, 8);
    uint8_t r[8];
    for (int i = 0; i < 8; i++)
        r[i] = (uint8_t)(u >> (i * 8));
    return emit_inline_scalar(f, KVSPACE_KIND_FLOAT64, r, 8);
}

int kvlangBuiltinEmitBool(kvlangFrame_t *f, bool b) {
    uint8_t r = b ? 1 : 0;
    return emit_inline_scalar(f, KVSPACE_KIND_BOOL, &r, 1);
}

int kvlangBuiltinSetError(kvlangFrame_t *f, const char *fmt, ...) {
    char msg[512];
    va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    kvlangVthreadSetError(f->kvspace, f->vthread_id, f->pc, msg);
    return -1;
}

int kvlangBuiltinExecuteCopy(kvlangFrame_t *f) {
    kvspace_t *kv = f->kvspace;
    kvlangRwirInst_t *inst = f->instruction;
    char *owned = NULL;
    const char *fr = f->frame_root;
    if (!fr) { owned = kvlangKeytreeFrameRoot(f->pc); fr = owned; }
    if (inst->read_count == 0) { free(owned); kvlangBuiltinNextPc(f); return 0; }
    kvlangXvalue_t v; kvlangXvalueZero(&v);
    kvlangBuiltinResolveReadValue(kv, fr, inst->reads[0].name, &inst->reads[0].value, &v);
    for (int i = 0; i < inst->write_count; i++) {
        const char *slot = inst->writes[i].name;
        const char *dot = strstr(slot, MEMBER_SEP);
        if (dot && dot != slot) {
            /* 成员写前置条件：memhead 必须已存在（同 kvspace·set，见 rwir_kv.c）。 */
            char *b = strndup(slot, (size_t)(dot - slot));
            int ok = kvlangBuiltinCheckMemoryHead(kv, fr, b);
            char msg[320];
            if (ok != 0)
                snprintf(msg, sizeof msg, "TypeError: memhead %s does not exist — declare the container first (e.g. `%s:T = {}`)", b, b);
            free(b);
            if (ok != 0) {
                free(owned);
                kvlangVthreadSetError(kv, f->vthread_id, f->pc, msg);
                return -1;
            }
        }
        kvspaceKeyOff *wo =
            inst->write_offs ? inst->write_offs[i] : NULL;
        if (wo && v.bytes && v.length > 0 &&
            kvspaceKeyOffSet(wo, v.bytes, v.length, NULL, 0) == 0)
            continue;
        char *key = kvlangBuiltinResolveWriteSlot(kv, fr, slot);
        kvlangKvPair_t pair = { key, v };
        char err[256];
        kvlangKvspaceSet(kv, &pair, 1, err, sizeof err);
        free(key);
    }
    free(owned);
    kvlangBuiltinNextPc(f);
    return 0;
}
