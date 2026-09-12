#include "runtime_internal.h"
#include "rwir_internal.h"

/* ── resolve ───────────────────────────────────────────────────────── */
/* #116 后 if/while 不再建 scope 帧，当前帧 [d] 即 rwfunc 帧，frame_root 直接可用。 */

void kvlangBuiltinResolveReadValue(kvlangKv_t *kv, const char *frame_root, const char *name,
                           const kvlangXvalue_t *val, kvlangXvalue_t *out) {
    kvlangXvalueZero(out);
    if (val && !kvlangXvalueNone(val) && !kvlangXvalueKindIs(val, KVSPACE_KIND_RWIR) && !kvlangXvalueKindIs(val, KVSPACE_KIND_RWFUNC)) {
        out->data = val->data; /* 借指令内冻结字面量（生命周期同 decode 缓存，永不 flush/free） */
        out->len = val->len;
        out->borrowed = 1;
        return;
    }
    if (!name || !name[0]) return;
    if (name[0] == '/') { kvlangKvGetOne(kv, name, out); return; }
    if (name[0] == '*') {
        /* 显式解引用：*[0,±k] → 读 frame_root/[0,±k]（ref=1 Ptr）→ 解引用到实参值。
         * 同 ResolveWriteSlot：帧槽必有 Ptr，读不到即传参链路已坏 → panic，
         * 绝不静默留 None（那会让形参读成空值，把真因藏到几层之外）。 */
        char *stk = kvlangKeytreeStack(frame_root);
        kvlangXvalue_t pv; kvlangXvalueZero(&pv);
        kvlangKvGetMember(kv, stk, name + 1, &pv);
        if (!kvlangXvalueIsPtr(&pv)) {
            fprintf(stderr, "panic: %s%s is not a Ptr — frame slot missing, param passing broken\n", stk, name + 1);
            abort();
        }
        char *target = kvlangXvaluePtrTarget(&pv);
        kvlangKvGetOne(kv, target, out);
        free(target);
        kvlangXvalueFree(&pv); free(stk);
        return;
    }
    char *stk = kvlangKeytreeStack(frame_root);
    kvlangXvalue_t pv; kvlangXvalueZero(&pv);
    kvlangKvGetMember(kv, stk, name, &pv);
    if (kvlangXvalueIsPtr(&pv)) {
        /* 数据 Ptr（&x）：地址值本体，读值不解引用。成员访问另走 kvspace·get 的 member_path。 */
        *out = pv; pv.data = NULL; pv.len = 0;
    } else if (!kvlangXvalueNone(&pv)) {
        *out = pv; pv.data = NULL; pv.len = 0;
    }
    kvlangXvalueFree(&pv); free(stk);
}

/* 读参 → 其最终存储键（malloc）：字面量（指令内冻结的具体值，无后端槽）返 NULL；
 * 变量则复用 ResolveWriteSlot——读侧最终槽键与写侧同一路径（普通成员=stk+name，
 * ptr 追链到最终目标），供 xv 系列对 key 直发 GetHead/GetPart/SetPart 做分片读写。 */
char *kvlangBuiltinResolveReadKey(kvlangKv_t *kv, const char *frame_root, const char *name,
                          const kvlangXvalue_t *val) {
    if (val && !kvlangXvalueNone(val) && !kvlangXvalueKindIs(val, KVSPACE_KIND_RWIR) && !kvlangXvalueKindIs(val, KVSPACE_KIND_RWFUNC))
        return NULL;
    if (!name || !name[0]) return NULL;
    return kvlangBuiltinResolveWriteSlot(kv, frame_root, name);
}

char *kvlangBuiltinResolveWriteSlot(kvlangKv_t *kv, const char *frame_root, const char *name) {
    if (name[0] == '/') return strdup(name);
    char *stk = kvlangKeytreeStack(frame_root);
    if (name[0] == '*') {
        /* 显式解引用：*[0,±k] → 读 frame_root/[0,±k]（ref=1 Ptr）→ target=写槽路径。
         * `*[0,±k]` 是 layout 编译期生成的形参引用，帧槽必有调用点写入的 Ptr；读不到
         * 即传参链路已坏（不变量违反，非用户错误）→ panic，绝不回退成字面路径——
         * 回退会把「写形参」静默变成「写一个叫 *[0,±k] 的键」，掩盖真因。 */
        kvlangXvalue_t pv; kvlangXvalueZero(&pv);
        kvlangKvGetMember(kv, stk, name + 1, &pv);
        if (!kvlangXvalueIsPtr(&pv)) {
            fprintf(stderr, "panic: %s%s is not a Ptr — frame slot missing, param passing broken\n", stk, name + 1);
            abort();
        }
        char *target = kvlangXvaluePtrTarget(&pv);
        kvlangXvalueFree(&pv); free(stk);
        return target;
    }
    kvlangStrbuf_t o; kvlangStrbufInit(&o);
    kvlangStrbufPuts(&o, stk); kvlangStrbufPuts(&o, name);
    free(stk);
    return kvlangStrbufDetach(&o);
}

/* ── coerce / kvlangDisplay ─────────────────────────────────────────────── */

static bool try_parse_int(const char *s, int64_t *out) {
    if (!s || !s[0]) return false;
    char *end; long long v = strtoll(s, &end, 10);
    if (end == s || *end != 0) return false;
    *out = v; return true;
}

bool kvlangBuiltinTryParseNumber(const char *s, kvlangXvalue_t *out) {
    kvlangXvalueZero(out);
    if (!s || !s[0]) return false;
    char c0 = s[0];
    bool num = (c0 >= '0' && c0 <= '9') || (c0 == '-' && s[1] >= '0' && s[1] <= '9');
    if (!num) return false;
    int64_t iv;
    if (try_parse_int(s, &iv)) { kvlangXvalueNewInt64(out, iv); return true; }
    if (c0 != '-' && !strpbrk(s, ".eE")) {
        char *end; unsigned long long uv = strtoull(s, &end, 10);
        if (end != s && *end == 0) {
            uint8_t r[8]; memcpy(r, &uv, 8);
            kvlangXvalueNewTlv(out, KVSPACE_KIND_UINT64, r, 8, 1); return true;
        }
    }
    char *end; double f = strtod(s, &end);
    if (end != s && *end == 0) { kvlangXvalueNewFloat64(out, f); return true; }
    return false;
}

void kvlangBuiltinXvalueAt(const kvlangXvalue_t *v, int i, kvlangXvalue_t *out) {
    kvlangXvalueZero(out);
    int n = kvlangXvalueArrayLen(v);
    if (i < 0 || i >= n) return;
    const char *k = kvlangXvalueKind(v);
    int sz = kvlangXvalueElemSize(k);
    if (sz <= 0) return;
    kvspaceHead_t h; kvspaceDecodeHead(v->data, v->len, &h);
    const uint8_t *body = v->data + h.body_offset;
    kvlangXvalueNewTlv(out, k, body + i * sz, (uint32_t)sz, 1);
}

void kvlangDisplay(const kvlangXvalue_t *v, char **out);

static void format_array(const kvlangXvalue_t *v, char **out) {
    int n = kvlangXvalueArrayLen(v);
    kvlangStrbuf_t b; kvlangStrbufInit(&b);
    kvlangStrbufPutc(&b, '[');
    for (int i = 0; i < n; i++) {
        if (i) kvlangStrbufPuts(&b, ", ");
        kvlangXvalue_t e; kvlangBuiltinXvalueAt(v, i, &e);
        char *s; kvlangDisplay(&e, &s);
        kvlangStrbufPuts(&b, s); free(s); kvlangXvalueFree(&e);
    }
    kvlangStrbufPutc(&b, ']');
    *out = kvlangStrbufDetach(&b);
}

void kvlangDisplay(const kvlangXvalue_t *v, char **out) {
    if (kvlangXvalueIsCharKind(kvlangXvalueKind(v))) { *out = kvlangXvalueValueString(v); return; }
    if (kvlangXvalueArrayLen(v) > 1) { format_array(v, out); return; }
    *out = kvlangXvalueValueString(v);
}

/* ── frame helper ─────────────────────────────────────────────────── */

int kvlangBuiltinReadInputs(kvlangFrame_t *f, kvlangXvalue_t *out, int cap) {
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    int n = 0;
    for (int i = 0; i < f->inst->nr && n < cap; i++) {
        kvlangBuiltinResolveReadValue(f->kv, fr, f->inst->reads[i].name, &f->inst->reads[i].val, &out[n]);
        n++;
    }
    free(fr);
    return n;
}

void kvlangBuiltinFreeInputs(kvlangXvalue_t *in, int n) { for (int i = 0; i < n; i++) kvlangXvalueFree(&in[i]); }

void kvlangBuiltinNextPc(kvlangFrame_t *f) {
    kvlangStrbuf_t npc; kvlangStrbufInit(&npc);
    kvlangRwirNextPc(f->pc, &npc);
    kvlangVthreadSet(f->kv, f->vtid, npc.p, "running");
    kvlangStrbufFree(&npc);
}

int kvlangBuiltinWriteResult(kvlangFrame_t *f, const kvlangXvalue_t *result) {
    if (f->inst->nw > 0) {
        char *fr = kvlangKeytreeFrameRoot(f->pc);
        char *key = kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->writes[0].name);
        free(fr);
        kvlangKvPair_t pair = { key, *result };
        char err[256];
        kvlangKvSet(f->kv, &pair, 1, err, sizeof err);
        free(key);
    }
    kvlangBuiltinNextPc(f);
    return 0;
}

int kvlangBuiltinSetErr(kvlangFrame_t *f, const char *fmt, ...) {
    char msg[512];
    va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    kvlangVthreadSetError(f->kv, f->vtid, f->pc, msg);
    return -1;
}

int kvlangBuiltinExecuteCopy(kvlangKv_t *kv, const char *vtid, const char *pc, kvlangRwirInst_t *inst) {
    char *fr = kvlangKeytreeFrameRoot(pc);
    kvlangFrame_t f = { kv, vtid, pc, inst };
    if (inst->nr == 0) { free(fr); kvlangBuiltinNextPc(&f); return 0; }
    kvlangXvalue_t v; kvlangXvalueZero(&v);
    kvlangBuiltinResolveReadValue(kv, fr, inst->reads[0].name, &inst->reads[0].val, &v);
    for (int i = 0; i < inst->nw; i++) {
        const char *slot = inst->writes[i].name;
        const char *dot = strstr(slot, MEMBER_SEP);
        if (dot && dot != slot) {
            /* 成员写前置条件：memhead 必须已存在（同 kvspace·set，见 rwir_kv.c）。 */
            char *b = strndup(slot, (size_t)(dot - slot));
            int ok = kvlangBuiltinCheckMemhead(kv, fr, b);
            char msg[320];
            if (ok != 0)
                snprintf(msg, sizeof msg, "TypeError: memhead %s does not exist — declare the container first (e.g. `%s:T = {}`)", b, b);
            free(b);
            if (ok != 0) {
                kvlangXvalueFree(&v); free(fr);
                kvlangVthreadSetError(kv, vtid, pc, msg);
                return -1;
            }
        }
        char *key = kvlangBuiltinResolveWriteSlot(kv, fr, slot);
        kvlangKvPair_t pair = { key, v };
        char err[256];
        kvlangKvSet(kv, &pair, 1, err, sizeof err);
        free(key);
    }
    kvlangXvalueFree(&v);
    free(fr);
    kvlangBuiltinNextPc(&f);
    return 0;
}
