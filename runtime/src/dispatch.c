#include "runtime_internal.h"
#include <math.h>
#include <pthread.h>
#include <time.h>

#define HEARTBEAT_STALE_S 15
#define NEG_CACHE_TTL_NS  250000000LL
#define NEG_CACHE_CAP     64

static int64_t default_timeout_ns = 30LL * 1000000000LL;
static int64_t task_status_ttl_ns = 10LL * 60LL * 1000000000LL;

void kvlangDispatchSetDefaultTimeoutNs(int64_t ns) { default_timeout_ns = ns; }
void kvlangDispatchSetTaskStatusTtlNs(int64_t ns) { task_status_ttl_ns = ns; }

static int64_t now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    return (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec;
}

static char *get_char(kvlangKv_t *kv, const char *key) {
    kvlangXvalue_t v; kvlangXvalueZero(&v);
    kvlangKvGetOne(kv, key, &v);
    char *s = kvlangXvalueNone(&v) ? NULL : kvlangXvalueValueString(&v);
    kvlangXvalueFree(&v);
    return s;
}

static void trim_inplace(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == '\t')) s[--n] = 0;
    char *p = s;
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

static bool heartbeat_fresh(kvlangKv_t *kv, const char *backend) {
    char *k = kvlangKeytreeSysRwirBackendHeartbeat(backend);
    char *s = get_char(kv, k);
    free(k);
    if (!s) return false;
    trim_inplace(s);
    char *end = NULL;
    long long sec = strtoll(s, &end, 10);
    bool bad = end == s || (end && *end);
    free(s);
    if (bad) return false;
    time_t now = time(NULL);
    long long d = (long long)now - sec;
    if (d < 0) d = -d;
    return d <= HEARTBEAT_STALE_S;
}

static bool is_on_duty(kvlangKv_t *kv, const char *backend) {
    char *k = kvlangKeytreeSysRwirBackendStatus(backend);
    char *s = get_char(kv, k);
    free(k);
    if (!s) return false;
    trim_inplace(s);
    bool ok = (strcmp(s, "ready") == 0 || strcmp(s, "busy") == 0) && heartbeat_fresh(kv, backend);
    free(s);
    return ok;
}

static double parse_load(kvlangKv_t *kv, const char *backend) {
    char *k = kvlangKeytreeSysRwirBackendLoad(backend);
    char *s = get_char(kv, k);
    free(k);
    if (!s) return 0;
    trim_inplace(s);
    char *end = NULL;
    double f = strtod(s, &end);
    bool bad = end == s || (end && *end) || isnan(f) || isinf(f) || f < 0;
    free(s);
    if (bad) {
        fprintf(stderr, "rwir-backend load is not a number in [0, +inf]\n");
        abort();
    }
    return f;
}

typedef struct { char **v; int n, cap; } strlist_t;

static void sl_push(strlist_t *l, char *s) {
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 8;
        l->v = realloc(l->v, sizeof(char *) * (size_t)l->cap);
    }
    l->v[l->n++] = s;
}

static void sl_free(strlist_t *l) {
    for (int i = 0; i < l->n; i++) free(l->v[i]);
    free(l->v);
    l->v = NULL; l->n = l->cap = 0;
}

static void list_children(kvlangKv_t *kv, const char *dir, strlist_t *out) {
    char *pref = kvlangKeytreeStack(dir);
    char **names = NULL; int n = 0;
    kvlangKvList(kv, pref, false, false, &names, &n);
    free(pref);
    for (int i = 0; i < n; i++) {
        size_t ln = strlen(names[i]);
        if (ln > 0 && names[i][ln - 1] == '/') names[i][ln - 1] = 0;
        sl_push(out, names[i]);
    }
    free(names);
}

static void backends_for(kvlangKv_t *kv, const char *op, strlist_t *reg, strlist_t *duty) {
    if (!kvlangKeytreeValidSegment(op)) return;
    char *root = kvlangKeytreeSysRwirBackendRoot();
    strlist_t names = {0};
    list_children(kv, root, &names);
    free(root);
    for (int i = 0; i < names.n; i++) {
        char *opk = kvlangKeytreeSysRwirBackendOp(names.v[i], op);
        kvlangXvalue_t v; kvlangXvalueZero(&v);
        kvlangKvGetOne(kv, opk, &v);
        free(opk);
        if (kvlangXvalueNone(&v)) { kvlangXvalueFree(&v); continue; }
        kvlangXvalueFree(&v);
        if (reg) sl_push(reg, strdup(names.v[i]));
        if (is_on_duty(kv, names.v[i])) sl_push(duty, strdup(names.v[i]));
    }
    sl_free(&names);
}

typedef struct { kvlangKv_t *kv; char op[96]; int64_t exp; int used; } negent_t;

static struct {
    pthread_mutex_t mu;
    negent_t e[NEG_CACHE_CAP];
} neg = { .mu = PTHREAD_MUTEX_INITIALIZER };

static bool neg_hit(kvlangKv_t *kv, const char *op) {
    int64_t t = now_ns();
    pthread_mutex_lock(&neg.mu);
    bool hit = false;
    for (int i = 0; i < NEG_CACHE_CAP; i++) {
        if (!neg.e[i].used) continue;
        if (neg.e[i].kv != kv || strcmp(neg.e[i].op, op) != 0) continue;
        if (t >= neg.e[i].exp) { neg.e[i].used = 0; break; }
        hit = true;
        break;
    }
    pthread_mutex_unlock(&neg.mu);
    return hit;
}

static void neg_put(kvlangKv_t *kv, const char *op) {
    int64_t exp = now_ns() + NEG_CACHE_TTL_NS;
    pthread_mutex_lock(&neg.mu);
    int slot = 0;
    for (int i = 0; i < NEG_CACHE_CAP; i++) {
        if (!neg.e[i].used || (neg.e[i].kv == kv && strcmp(neg.e[i].op, op) == 0)) { slot = i; break; }
    }
    neg.e[slot].kv = kv;
    snprintf(neg.e[slot].op, sizeof neg.e[slot].op, "%s", op);
    neg.e[slot].exp = exp;
    neg.e[slot].used = 1;
    pthread_mutex_unlock(&neg.mu);
}

bool kvlangDispatchIsDelegatedOp(kvlangKv_t *kv, const char *opcode) {
    char *op = kvlangKeytreeCanonOp(opcode);
    if (neg_hit(kv, op)) { free(op); return false; }
    strlist_t duty = {0};
    backends_for(kv, op, NULL, &duty);
    bool ok = duty.n > 0;
    if (!ok) neg_put(kv, op);
    sl_free(&duty); free(op);
    return ok;
}

static int select_backend(kvlangKv_t *kv, const char *op, char **out, char *err, size_t err_cap) {
    *out = NULL;
    strlist_t reg = {0}, duty = {0};
    backends_for(kv, op, &reg, &duty);
    if (reg.n == 0) {
        snprintf(err, err_cap, "no backend supports opcode=%s", op);
        sl_free(&reg); sl_free(&duty);
        return -1;
    }
    if (duty.n == 0) {
        kvlangStrbuf_t got; kvlangStrbufInit(&got);
        for (int i = 0; i < reg.n; i++) {
            if (i) kvlangStrbufPuts(&got, ", ");
            char *sk = kvlangKeytreeSysRwirBackendStatus(reg.v[i]);
            char *st = get_char(kv, sk);
            free(sk);
            kvlangStrbufPuts(&got, reg.v[i]);
            kvlangStrbufPuts(&got, "=");
            kvlangStrbufPuts(&got, st ? st : "");
            free(st);
        }
        snprintf(err, err_cap, "no on-duty backend for opcode=%s; backend status must be ready|busy, got %s", op, got.p ? got.p : "");
        kvlangStrbufFree(&got);
        sl_free(&reg); sl_free(&duty);
        return -1;
    }
    char *best = duty.v[0];
    double best_load = parse_load(kv, best);
    for (int i = 1; i < duty.n; i++) {
        double ld = parse_load(kv, duty.v[i]);
        if (ld < best_load) { best_load = ld; best = duty.v[i]; }
    }
    *out = strdup(best);
    sl_free(&reg); sl_free(&duty);
    return 0;
}

static int64_t timeout_for(kvlangKv_t *kv, const char *backend) {
    int64_t d = default_timeout_ns;
    char *root = kvlangKeytreeSysRwirBackendCategoryRoot(backend);
    strlist_t cats = {0};
    list_children(kv, root, &cats);
    free(root);
    for (int i = 0; i < cats.n; i++) {
        int64_t t = 0;
        if (strcmp(cats.v[i], "compute") == 0) t = 30LL * 1000000000LL;
        else if (strcmp(cats.v[i], "api") == 0) t = 120LL * 1000000000LL;
        else if (strcmp(cats.v[i], "agent") == 0) t = 300LL * 1000000000LL;
        if (t > d) d = t;
    }
    sl_free(&cats);
    return d;
}

static int set_char(kvlangKv_t *kv, const char *key, const char *s, char *err, uint32_t err_cap) {
    kvlangXvalue_t v; kvlangXvalueZero(&v);
    kvlangXvalueNewCharUtf8(&v, s);
    kvlangKvPair_t p = { (char *)key, v };
    int rc = kvlangKvSet(kv, &p, 1, err, err_cap);
    kvlangXvalueFree(&v);
    return rc;
}

static void json_esc(kvlangStrbuf_t *b, const char *s) {
    kvlangStrbufPutc(b, '"');
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { kvlangStrbufPutc(b, '\\'); kvlangStrbufPutc(b, (char)c); }
        else if (c == '\n') kvlangStrbufPuts(b, "\\n");
        else if (c == '\r') kvlangStrbufPuts(b, "\\r");
        else if (c == '\t') kvlangStrbufPuts(b, "\\t");
        else kvlangStrbufPutc(b, (char)c);
    }
    kvlangStrbufPutc(b, '"');
}

static bool is_var_slot(const kvlangParam_t *p) {
    if (!p->name || !p->name[0]) return false;
    if (p->name[0] == '/') return true;
    return kvlangXvalueKindIs(&p->val, KVSPACE_KIND_RWIR) || kvlangXvalueNone(&p->val);
}

typedef struct { char **keys; int n; } keyset_t;

static bool keyset_has(const keyset_t *s, const char *k) {
    for (int i = 0; i < s->n; i++) if (strcmp(s->keys[i], k) == 0) return true;
    return false;
}

static int fail_msg(kvlangKv_t *kv, const char *vtid, const char *pc, const char *fmt, ...) {
    char body[384];
    va_list ap; va_start(ap, fmt);
    vsnprintf(body, sizeof body, fmt, ap);
    va_end(ap);
    char msg[420];
    snprintf(msg, sizeof msg, "RuntimeError: delegate: %s", body);
    kvlangVthreadSetError(kv, vtid, pc, msg);
    return KVLANG_DELEGATE_ERR;
}

static int fail_as(kvlangKv_t *kv, const char *vtid, const char *pc, const char *msg) {
    kvlangVthreadSetError(kv, vtid, pc, msg);
    return KVLANG_DELEGATE_ERR;
}

static int lib_arity(const kvlangXvalue_t *v, int *nr, int *nw) {
    kvspaceHead_t h;
    if (kvlangXvalueHead(v, &h) != 0) return -1;
    int32_t bl = 0;
    const uint8_t *b = kvlangXvalueBody(v, &h, &bl);
    if (!b || bl < 4) return -1;
    *nr = b[0] | (b[1] << 8);
    *nw = b[2] | (b[3] << 8);
    return 0;
}

int kvlangDispatchDelegate(kvlangKv_t *kv, const char *vtid, const char *pc, kvlangRwirInst_t *inst) {
    char *op = kvlangKeytreeCanonOp(inst->opcode);

    char *declk = kvlangKeytreeRwir(op);
    kvlangXvalue_t decl; kvlangXvalueZero(&decl);
    kvlangKvGetOne(kv, declk, &decl);
    free(declk);

    char *sigk = kvlangKeytreeLibSig(op);
    kvlangXvalue_t sig; kvlangXvalueZero(&sig);
    kvlangKvGetOne(kv, sigk, &sig);
    free(sigk);

    if (!kvlangXvalueNone(&decl) && kvlangXvalueKindIs(&decl, KVSPACE_KIND_DEF_RWIR)) {
        int dnr = 0, dnw = 0;
        if (lib_arity(&decl, &dnr, &dnw) != 0) {
            kvlangXvalueFree(&decl); kvlangXvalueFree(&sig);
            int rc = fail_msg(kv, vtid, pc, "%s: defrwir body is not a signature", op);
            free(op);
            return rc;
        }
        if (inst->nr != dnr) {
            kvlangXvalueFree(&decl); kvlangXvalueFree(&sig);
            int rc = fail_msg(kv, vtid, pc, "%s: read arity mismatch: call site has %d, declaration has %d", op, inst->nr, dnr);
            free(op);
            return rc;
        }
        if (inst->nw != dnw) {
            kvlangXvalueFree(&decl); kvlangXvalueFree(&sig);
            int rc = fail_msg(kv, vtid, pc, "%s: write arity mismatch: call site has %d, declaration has %d", op, inst->nw, dnw);
            free(op);
            return rc;
        }
    } else if (!kvlangXvalueNone(&sig) && kvlangXvalueKindIs(&sig, KVSPACE_KIND_DEF_RWFUNC)) {
        strlist_t duty = {0};
        backends_for(kv, op, NULL, &duty);
        if (duty.n == 0) {
            sl_free(&duty);
            kvlangXvalueFree(&decl); kvlangXvalueFree(&sig);
            free(op);
            return KVLANG_DELEGATE_LOCAL;
        }
        kvlangStrbuf_t names; kvlangStrbufInit(&names);
        for (int i = 0; i < duty.n; i++) {
            if (i) kvlangStrbufPuts(&names, ",");
            kvlangStrbufPuts(&names, duty.v[i]);
        }
        sl_free(&duty);
        kvlangXvalueFree(&decl); kvlangXvalueFree(&sig);
        int rc = fail_msg(kv, vtid, pc, "%s: on-duty backend %s and local rwfunc both define it; one opcode, one definition", op, names.p ? names.p : "");
        kvlangStrbufFree(&names);
        free(op);
        return rc;
    } else if (!kvlangXvalueNone(&decl) && !kvlangXvalueKindIs(&decl, KVSPACE_KIND_DEF_RWIR)) {
        const char *k = kvlangXvalueKind(&decl);
        int rc = fail_msg(kv, vtid, pc, "%s: /lib entry holds kind %s, which is not callable", op, k[0] ? k : "?");
        kvlangXvalueFree(&decl); kvlangXvalueFree(&sig);
        free(op);
        return rc;
    }
    kvlangXvalueFree(&decl);
    kvlangXvalueFree(&sig);

    char selerr[256];
    char *backend = NULL;
    if (select_backend(kv, op, &backend, selerr, sizeof selerr) != 0) {
        int rc = fail_msg(kv, vtid, pc, "%s", selerr);
        free(op);
        return rc;
    }

    char *seqk = kvlangKeytreeVthreadDelegSeq(vtid);
    int64_t seq = kvlangVthreadNextSeq(kv, seqk);
    free(seqk);
    char task_id[160];
    snprintf(task_id, sizeof task_id, "rwir:%s:%s:%lld", backend, vtid, (long long)seq);

    char *fr = kvlangKeytreeFrameRoot(pc);
    char **out_keys = calloc((size_t)inst->nw, sizeof(char *));
    keyset_t outset = { out_keys, 0 };
    for (int i = 0; i < inst->nw; i++) {
        char *key = kvlangBuiltinResolveWriteSlot(kv, fr, inst->writes[i].name);
        char *werr = kvlangKeytreeCheckWriteKey(vtid, key);
        if (werr) {
            free(key);
            for (int j = 0; j < i; j++) free(out_keys[j]);
            free(out_keys); free(fr); free(backend); free(op);
            int rc = fail_as(kv, vtid, pc, werr);
            free(werr);
            return rc;
        }
        out_keys[i] = key;
        outset.n++;
    }

    kvlangStrbuf_t json; kvlangStrbufInit(&json);
    kvlangStrbufPuts(&json, "{\"request_id\":"); json_esc(&json, task_id);
    kvlangStrbufPuts(&json, ",\"vtid\":"); json_esc(&json, vtid);
    kvlangStrbufPuts(&json, ",\"pc\":"); json_esc(&json, pc);
    kvlangStrbufPuts(&json, ",\"opcode\":"); json_esc(&json, op);
    kvlangStrbufPuts(&json, ",\"inputs\":[");
    for (int i = 0; i < inst->nr; i++) {
        if (i) kvlangStrbufPutc(&json, ',');
        kvlangStrbufPutc(&json, '{');
        if (is_var_slot(&inst->reads[i]) && inst->reads[i].name[0] == '/') {
            kvlangStrbufPuts(&json, "\"key\":"); json_esc(&json, inst->reads[i].name);
        } else if (is_var_slot(&inst->reads[i])) {
            char *rk = kvlangBuiltinResolveWriteSlot(kv, fr, inst->reads[i].name);
            if (keyset_has(&outset, rk)) {
                kvlangXvalue_t val; kvlangXvalueZero(&val);
                kvlangBuiltinResolveReadValue(kv, fr, inst->reads[i].name, &inst->reads[i].val, &val);
                char *disp = NULL; kvlangDisplay(&val, &disp);
                kvlangStrbufPuts(&json, "\"value\":"); json_esc(&json, disp ? disp : "");
                free(disp); kvlangXvalueFree(&val);
            } else {
                kvlangStrbufPuts(&json, "\"key\":"); json_esc(&json, rk);
            }
            free(rk);
        } else {
            char *disp = NULL; kvlangDisplay(&inst->reads[i].val, &disp);
            kvlangStrbufPuts(&json, "\"value\":"); json_esc(&json, disp ? disp : "");
            free(disp);
        }
        kvlangStrbufPutc(&json, '}');
    }
    kvlangStrbufPuts(&json, "],\"outputs\":[");
    for (int i = 0; i < inst->nw; i++) {
        if (i) kvlangStrbufPutc(&json, ',');
        kvlangStrbufPuts(&json, "{\"key\":"); json_esc(&json, out_keys[i]);
        kvlangStrbufPutc(&json, '}');
    }
    char *done_key = kvlangKeytreeDoneRwir(task_id);
    kvlangStrbufPuts(&json, "],\"done_key\":"); json_esc(&json, done_key);
    kvlangStrbufPutc(&json, '}');

    char *status_key = kvlangKeytreeSysTask(task_id, SEG_STATUS);
    char err[256];
    if (set_char(kv, status_key, "pending", err, sizeof err) != 0) {
        fail_msg(kv, vtid, pc, "%s: init task status: %s", op, err);
        goto done_err;
    }

    for (int i = 0; i < inst->nw; i++) {
        if (kvlangKvDel(kv, out_keys[i], err, sizeof err) != 0) {
            fail_msg(kv, vtid, pc, "%s: clear output slot %s: %s", op, out_keys[i], err);
            goto done_err;
        }
    }

    char *cmd = kvlangKeytreeSysRwirBackendCmd(backend);
    kvlangXvalue_t cmdv; kvlangXvalueZero(&cmdv);
    kvlangXvalueNewCharUtf8(&cmdv, json.p);
    if (kvlangKvNotify(kv, cmd, &cmdv, err, sizeof err) != 0) {
        kvlangXvalueFree(&cmdv); free(cmd);
        fail_msg(kv, vtid, pc, "push task: %s", err);
        goto done_err;
    }
    kvlangXvalueFree(&cmdv);
    free(cmd);

    kvlangVthreadSet(kv, vtid, pc, "wait");
    kvlangLogDebug("[%s] DELEGATE %s task=%s", vtid, inst->opcode, task_id);

    kvlangXvalue_t want; kvlangXvalueZero(&want);
    kvlangXvalueNewCharUtf8(&want, "done");
    kvlangXvalue_t got; kvlangXvalueZero(&got);
    kvlangKvWatch(kv, status_key, &want, (uint64_t)timeout_for(kv, backend), &got);
    kvlangXvalueFree(&want); kvlangXvalueFree(&got);

    char *st = get_char(kv, status_key);
    if (!st || strcmp(st, "done") != 0) {
        fail_msg(kv, vtid, pc, "%s: timeout or failed (status=%s)", op, st ? st : "");
        free(st);
        goto done_err;
    }
    free(st);

    for (int i = 0; i < inst->nw; i++) {
        kvlangXvalue_t ov; kvlangXvalueZero(&ov);
        kvlangKvGetOne(kv, out_keys[i], &ov);
        bool empty = kvlangXvalueNone(&ov);
        kvlangXvalueFree(&ov);
        if (empty) {
            fail_msg(kv, vtid, pc, "%s: executor reported done but left output slot %s unwritten", op, out_keys[i]);
            goto done_err;
        }
    }

    kvlangStrbuf_t npc; kvlangStrbufInit(&npc);
    kvlangRwirNextPc(pc, &npc);
    kvlangVthreadSet(kv, vtid, npc.p, "running");
    kvlangStrbufFree(&npc);
    kvlangLogDebug("[%s] DONE %s task=%s", vtid, inst->opcode, task_id);

    if (kvlangKvExpire(kv, status_key, (uint64_t)task_status_ttl_ns, err, sizeof err) != 0) {
        fprintf(stderr, "Expire task status after success: %s\n", err[0] ? err : "Expire failed");
        abort();
    }

    for (int i = 0; i < inst->nw; i++) free(out_keys[i]);
    free(out_keys); free(fr); free(backend); free(op);
    free(done_key); free(status_key); kvlangStrbufFree(&json);
    return KVLANG_DELEGATE_OK;

done_err:
    if (status_key) kvlangKvExpire(kv, status_key, (uint64_t)task_status_ttl_ns, err, sizeof err);
    for (int i = 0; i < inst->nw; i++) free(out_keys[i]);
    free(out_keys); free(fr); free(backend); free(op);
    free(done_key); free(status_key); kvlangStrbufFree(&json);
    return KVLANG_DELEGATE_ERR;
}
