#include "runtime_internal.h"
#include "rwir_internal.h"
#include "func_code.h"
#include <assert.h>

#ifdef KV_COUNT
unsigned long kvCBodyFast, kvCBodyFallback;
#endif

/* ── resolve ───────────────────────────────────────────────────────── */
/* #116 后 if/while 不再建 scope 帧，当前帧 [d] 即 rwfunc 帧，frame_root 直接可用。 */

static inline bool xvalue_ready_non_rwir(const kvlangXvalue_t *val) {
    if (!val || !val->bytes || val->length == 0 || xh_is_none(val->bytes))
        return false;
    if ((val->bytes[1] & 3u) != 0 || val->bytes[XH_PREFIX] != 'r')
        return true;
    return !kvlangXvalueKindIs(val, KVSPACE_KIND_RWIR) &&
           !kvlangXvalueKindIs(val, KVSPACE_KIND_RWFUNC);
}

static void slot_bind_scalar_kind(kvlangBodySlot *slot,
                                  const kvlangXvalue_t *v) {
    if (slot->kind || slot->dead)
        return;
    uint32_t ltlen = xh_langtype_len(v->bytes);
    int tid = kvlangLangtypeId((const char *)(v->bytes + XH_PREFIX), ltlen);
    if ((kvlangLangtypeIsNum(tid) || tid == KVLANG_LT_BOOL) &&
        v->length == 32u + (uint32_t)kvlangLangtypeElemSize(tid)) {
        slot->kind = kvlangLangtypeKind(tid);
        slot->width = (uint32_t)kvlangLangtypeElemSize(tid);
        slot->kind_len = (uint8_t)ltlen;
    }
}

void kvlangBuiltinResolveReadValue(kvspace_t *kv, const char *frame_root, const char *name,
                           const kvlangXvalue_t *val, kvlangXvalue_t *out) {
    kvlangXvalueZero(out);
    /* decode 期已按 read_target 取回目标值（out->name 是绝对路径时亦然），直接借用即可。
     * 这一支必须排在绝对路径之前：否则每步都会把已经拿到的值丢掉、再去后端读一遍。 */
    if (xvalue_ready_non_rwir(val)) {
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
    if (xvalue_ready_non_rwir(val))
        return NULL;
    if (!name || !name[0]) return NULL;
    return kvlangBuiltinResolveWriteSlot(kv, frame_root, name);
}

char *kvlangBuiltinResolveWriteSlot(kvspace_t *kv, const char *frame_root, const char *name) {
    /* 写槽/槽键解析必须有名字：字面量读参 name=NULL，调用方须先按值分流。 */
    assert(name);
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
    kvlangXvalueNew(out, k, body + i * sz, (uint32_t)sz, 1);
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
    /* P1b4-a4：typed_core 行直接借用已加载值；实现放晚链接 TU，控制热 TU
     * 布局（#476：同算法仅布局不同就有 ±5% 的 kn 漂移）。 */
    if (f->instruction->typed_core)
        return kvlangBuiltinReadInputsTyped(f, out, cap);
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

/* 下一 PC 的格式化（冷 helper）：热循环里只留一次调用，避免函数体膨胀挪动布局。 */
__attribute__((noinline)) static size_t next_pc_format(kvlangFrame_t *f,
                                                       char *buf, size_t cap) {
    size_t n = 0;
    if (f->cursor && f->cursor->prefix_ok)
        n = kvlangRwirNextPcFromPrefix(f->cursor->prefix,
                                       (size_t)f->cursor->prefix_len,
                                       f->cursor->irseq, buf, cap);
    if (n == 0)
        n = kvlangRwirNextPcBuffer(f->pc, buf, cap);
    if (n == 0)
        kvlangDie("next pc exceeds %zu bytes", cap);
    return n;
}

void kvlangBuiltinNextPc(kvlangFrame_t *f) {
    /* 顺序推进：优先用游标缓存的前缀 + irseq 直接拼下一 PC；能直接写进反馈缓冲
     * 就省掉一次 memcpy。 */
    char stackbuf[KV_KEY_MAX];
    char *buf = stackbuf;
    size_t cap = sizeof stackbuf;
    if (f->pc_out && f->pc_out_cap >= 16) {
        buf = f->pc_out;
        cap = f->pc_out_cap;
    }
    (void)next_pc_format(f, buf, cap);
    kvlangVthreadNextPc(f, buf, "running");
    /* 顺序推进（同一帧、irseq+1）：游标只需 +1，不必重解析 PC。 */
    if (f->cursor)
        f->cursor->dirty = false;
}

/* 写槽公共尾巴：失效 body 缓存 → ByRef 整值写 → 失败回退按 key 写 → NextPc。
 * 调用方负责：coerce（WriteResult）或 kind 已定型（Typed）与 body 快路径。 */
static inline __attribute__((always_inline)) void
write_value_to_slot(kvlangFrame_t *f, const kvlangXvalue_t *v) {
    kvspaceKeyOff *wo = f->instruction->write_offs
                            ? f->instruction->write_offs[0]
                            : NULL;
    if (wo && v->bytes && v->length >= XH_PREFIX)
        kvlangBodyInvalidateKind(wo->key, xh_langtype(v->bytes),
                                 xh_langtype_len(v->bytes));
    else if (wo)
        kvlangBodyInvalidate(wo->key);
    if (wo && v->bytes && v->length > 0 &&
        kvspaceKeyOffSet(wo, v->bytes, v->length, NULL, 0) == 0) {
        kvlangBuiltinNextPc(f);
        return;
    }
    char *owned = NULL;
    const char *fr = f->frame_root;
    if (!fr) {
        owned = kvlangKeytreeFrameRoot(f->pc);
        fr = owned;
    }
    char *key = kvlangBuiltinResolveWriteSlot(f->kvspace, fr,
                                              f->instruction->writes[0].name);
    free(owned);
    if (v->bytes && v->length >= XH_PREFIX)
        kvlangBodyInvalidateKind(key, xh_langtype(v->bytes),
                                 xh_langtype_len(v->bytes));
    else
        kvlangBodyInvalidate(key);
    kvlangKvPair_t pair = {key, *v};
    char err[256];
    kvlangKvspaceSet(f->kvspace, &pair, 1, err, sizeof err);
    free(key);
    kvlangBuiltinNextPc(f);
}

int kvlangBuiltinWriteResult(kvlangFrame_t *f, const kvlangXvalue_t *result) {
#ifdef KV_COUNT
    if (f->instruction->write_count > 0)
        kvCBodyFallback++;
#endif
    if (f->instruction->write_count > 0) {
        const char *target = f->instruction->writes[0].type;
        uint8_t cbuf[64];
        kvlangXvalue_t cv;
        if (kvlangCoerceAssign(f, target, result, cbuf, sizeof cbuf, &cv) != 0)
            return -1;
        if (cv.bytes && cv.length > 32 && cv.bytes[0] == 5 && cv.bytes[1] == 0) {
            kvspaceKeyOff *wo = f->instruction->write_offs
                                    ? f->instruction->write_offs[0]
                                    : NULL;
            kvlangBodySlot *slot = wo ? wo->fast : NULL;
            if (slot) {
                slot_bind_scalar_kind(slot, &cv);
                if (slot->kind &&
                    ((target && target[0]) ||
                     memcmp(cv.bytes + XH_PREFIX, slot->kind,
                            (size_t)slot->kind_len + 1) == 0)) {
                    if (kvlangBuiltinWriteBody(f, slot->kind, cv.bytes + 32,
                                               cv.length - 32))
                        return 0;
                }
            }
        }
        write_value_to_slot(f, &cv);
        return 0;
    }
    kvlangBuiltinNextPc(f);
    return 0;
}

/* P1b3：typed_core 写原语。结果 kind 已由 layout/manifest 定型，跳过
 * CoerceAssign（不做转换/分派）；调用方已先试 WriteBody 快路径。 */
__attribute__((visibility("hidden"))) int
kvlangBuiltinWriteResultTyped(kvlangFrame_t *f, const kvlangXvalue_t *v,
                              const char *kind) {
#ifdef KV_DEBUG_TYPES
    /* 写入侧断言：结果 head kind 必须等于 manifest kind；非空目标槽若已有
     * 值，其 kind 也必须一致（typed 槽不混型）。 */
    if (v->bytes && v->length >= XH_PREFIX &&
        (xh_langtype_len(v->bytes) != strlen(kind) ||
         memcmp(xh_langtype(v->bytes), kind, strlen(kind)) != 0))
        kvlangDie("typed write %s: result kind %.*s != manifest %s",
                  f->instruction->opcode, (int)xh_langtype_len(v->bytes),
                  (const char *)xh_langtype(v->bytes), kind);
    kvspaceKeyOff *wo = f->instruction->write_offs
                            ? f->instruction->write_offs[0]
                            : NULL;
    if (wo && wo->resolved) {
        uint8_t *cur = NULL;
        uint32_t cur_len = 0;
        if (kvspaceGetByRef(f->kvspace->handle, &wo->off, wo->key, &cur,
                            &cur_len) == 0 &&
            cur && cur_len >= XH_PREFIX) {
            if (xh_langtype_len(cur) != strlen(kind) ||
                memcmp(xh_langtype(cur), kind, strlen(kind)) != 0)
                kvlangDie("typed write %s: slot has kind %.*s != manifest %s",
                          f->instruction->opcode,
                          (int)xh_langtype_len(cur),
                          (const char *)xh_langtype(cur), kind);
        }
    }
#endif
    write_value_to_slot(f, v);
    return 0;
}

int kvlangBuiltinEmitResult(kvlangFrame_t *f, kvlangXvalue_t *v) {
    int rc = kvlangBuiltinWriteResult(f, v);
    kvlangXvalueFree(v);
    return rc;
}

__attribute__((visibility("hidden"))) int
kvlangBuiltinWriteBody(kvlangFrame_t *f, const char *kind, const uint8_t *raw,
                       uint32_t len) {
    /* 快路径：写槽 body 已绑定且 kind/宽度一致，只原地写 body，不编码 head、不 Set。
     * 失效后在此惰性重捕一次（槽未解析时不动，等它被写后再来）。 */
    kvspaceKeyOff *wo = f->instruction->write_offs
                            ? f->instruction->write_offs[0]
                            : NULL;
    kvlangBodySlot *slot = wo ? wo->fast : NULL;
    if (slot && !slot->kind && !slot->dead && kind) {
        slot->kind = kind;
        slot->width = len;
        slot->kind_len = (uint8_t)strlen(kind);
    }
    if (slot && !slot->ok && !slot->dead && slot->kind && slot->off->resolved)
        kvlangBodyCapture(f->kvspace, slot, f->frame_root, NULL);
    if (slot && slot->ok && slot->len == len && slot->kind &&
        (slot->kind == kind || strcmp(slot->kind, kind) == 0)) {
#ifdef KV_COUNT
        kvCBodyFast++;
#endif
        memcpy(slot->body, raw, len);
        kvlangBuiltinNextPc(f);
        return 1;
    }
    return 0;
}

static int emit_inline_scalar(kvlangFrame_t *f, const char *kind,
                              const uint8_t *raw, uint32_t len) {
    if (kvlangBuiltinWriteBody(f, kind, raw, len))
        return 0;
    uint8_t buf[64];
    uint32_t n = kvlangScalarEncode(kind, raw, len, buf, sizeof buf);
    kvlangXvalue_t v = {buf, n};
    if (f->instruction && f->instruction->typed_core)
        return kvlangBuiltinWriteResultTyped(f, &v, kind);
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
    const kvlangRowBind_t *rb = (const kvlangRowBind_t *)f->bind;
    if (rb && rb->copy_plain) {
        const kvlangXvalue_t *rv = &inst->reads[0].value;
        kvlangBodySlot *slot = rb->body;
        if (slot && rv->bytes && rv->length > 32 && rv->length <= 40 &&
            rv->bytes[0] == 5 && rv->bytes[1] == 0) {
            slot_bind_scalar_kind(slot, rv);
            const char *target = inst->writes[0].type;
            if (slot->kind && (!target || !target[0]) &&
                rv->length == 32 + slot->width &&
                memcmp(rv->bytes + XH_PREFIX, slot->kind,
                       (size_t)slot->kind_len + 1) == 0) {
                if (!slot->ok && !slot->dead && slot->off->resolved)
                    kvlangBodyCapture(kv, slot, f->frame_root, NULL);
                if (slot->ok) {
#ifdef KV_COUNT
                    kvCBodyFast++;
#endif
                    memcpy(slot->body, rv->bytes + 32, slot->width);
                    kvlangBuiltinNextPc(f);
                    return 0;
                }
                write_value_to_slot(f, rv);
                return 0;
            }
        }
        if (rv->bytes && !kvlangXvalueNone(rv) &&
            !kvlangXvalueKindIs(rv, KVSPACE_KIND_RWIR) &&
            !kvlangXvalueKindIs(rv, KVSPACE_KIND_RWFUNC)) {
            return kvlangBuiltinWriteResult(f, rv);
        }
    }
    char *owned = NULL;
    const char *fr = f->frame_root;
    if (!fr) { owned = kvlangKeytreeFrameRoot(f->pc); fr = owned; }
    if (inst->read_count == 0) { free(owned); kvlangBuiltinNextPc(f); return 0; }
    kvlangXvalue_t v; kvlangXvalueZero(&v);
    kvlangBuiltinResolveReadValue(kv, fr, inst->reads[0].name, &inst->reads[0].value, &v);
    if (inst->write_count == 1) {
        if (rb && rb->copy_plain) {
            free(owned);
            return kvlangBuiltinWriteResult(f, &v);
        }
        const char *slot = inst->writes[0].name;
        const char *dot = strstr(slot, MEMBER_SEP);
        if (!dot || dot == slot) {
            free(owned);
            return kvlangBuiltinWriteResult(f, &v);
        }
    }
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
        uint8_t cbuf[64];
        kvlangXvalue_t cv;
        if (kvlangCoerceAssign(f, inst->writes[i].type, &v, cbuf, sizeof cbuf,
                               &cv) != 0) {
            free(owned);
            return -1;
        }
        kvspaceKeyOff *wo =
            inst->write_offs ? inst->write_offs[i] : NULL;
        /* 整值写可能移动 box：先按同一规则失效 body/operand view。 */
        if (wo && cv.bytes && cv.length >= XH_PREFIX)
            kvlangBodyInvalidateKind(wo->key, xh_langtype(cv.bytes),
                                     xh_langtype_len(cv.bytes));
        else if (wo)
            kvlangBodyInvalidate(wo->key);
        if (wo && cv.bytes && cv.length > 0 &&
            kvspaceKeyOffSet(wo, cv.bytes, cv.length, NULL, 0) == 0)
            continue;
        char *key = kvlangBuiltinResolveWriteSlot(kv, fr, slot);
        kvlangKvPair_t pair = { key, cv };
        char err[256];
        kvlangKvspaceSet(kv, &pair, 1, err, sizeof err);
        free(key);
    }
    free(owned);
    kvlangBuiltinNextPc(f);
    return 0;
}
