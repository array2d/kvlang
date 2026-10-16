#include "runtime_internal.h"

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>

#define HEARTBEAT_STALE_S 15
#define NEG_CACHE_TTL_NS 250000000LL
#define NEG_CACHE_CAP 64
#define NEG_OP_CAP 160

static int64_t default_timeout_ns = 30LL * 1000000000LL;

void kvlangDispatchSetDefaultTimeoutNs(int64_t ns) { default_timeout_ns = ns; }

static int64_t now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    return (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec;
}

static char *get_char(kvlangKv_t *kv, const char *key) {
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    kvlangKvGetOne(kv, key, &v);
    char *s = kvlangXvalueNone(&v) ? NULL : kvlangXvalueValueString(&v);
    kvlangXvalueFree(&v);
    return s;
}

static void trim_inplace(char *s) {
    if (!s)
        return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\n' || s[n - 1] == '\r' ||
                     s[n - 1] == '\t'))
        s[--n] = 0;
    char *p = s;
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')
        p++;
    if (p != s)
        memmove(s, p, strlen(p) + 1);
}

static bool heartbeat_fresh(kvlangKv_t *kv, const char *backend) {
    char *k = kvlangKeytreeSysRwirBackendHeartbeat(backend);
    char *s = get_char(kv, k);
    free(k);
    if (!s)
        return false;
    trim_inplace(s);
    char *end = NULL;
    long long sec = strtoll(s, &end, 10);
    bool bad = end == s || (end && *end);
    free(s);
    if (bad)
        return false;
    long long d = (long long)time(NULL) - sec;
    if (d < 0)
        d = -d;
    return d <= HEARTBEAT_STALE_S;
}

static bool is_on_duty(kvlangKv_t *kv, const char *backend) {
    char *k = kvlangKeytreeSysRwirBackendStatus(backend);
    char *s = get_char(kv, k);
    free(k);
    if (!s)
        return false;
    trim_inplace(s);
    bool ok = (strcmp(s, "ready") == 0 || strcmp(s, "busy") == 0) &&
              heartbeat_fresh(kv, backend);
    free(s);
    return ok;
}

static double parse_load(kvlangKv_t *kv, const char *backend) {
    char *k = kvlangKeytreeSysRwirBackendLoad(backend);
    char *s = get_char(kv, k);
    free(k);
    if (!s)
        return 0;
    trim_inplace(s);
    char *end = NULL;
    double f = strtod(s, &end);
    bool bad = end == s || (end && *end) || isnan(f) || isinf(f) || f < 0;
    free(s);
    if (bad) {
        fprintf(stderr, "panic: rwir-backend load is not a number in [0, +inf]\n");
        abort();
    }
    return f;
}

typedef struct {
    char **v;
    int n, cap;
} strlist_t;

static void sl_push(strlist_t *l, char *s) {
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 8;
        l->v = realloc(l->v, sizeof(char *) * (size_t)l->cap);
    }
    l->v[l->n++] = s;
}

static void sl_free(strlist_t *l) {
    for (int i = 0; i < l->n; i++)
        free(l->v[i]);
    free(l->v);
    l->v = NULL;
    l->n = l->cap = 0;
}

static void list_children(kvlangKv_t *kv, const char *dir, strlist_t *out) {
    char *pref = kvlangKeytreeStack(dir);
    char **names = NULL;
    int n = 0;
    kvlangKvList(kv, pref, false, false, &names, &n);
    free(pref);
    for (int i = 0; i < n; i++) {
        size_t ln = strlen(names[i]);
        if (ln > 0 && names[i][ln - 1] == '/')
            names[i][ln - 1] = 0;
        sl_push(out, names[i]);
    }
    free(names);
}

static void backends_for(kvlangKv_t *kv, const char *op, strlist_t *reg, strlist_t *duty) {
    if (!kvlangKeytreeValidSegment(op))
        return;
    char *root = kvlangKeytreeSysRwirBackendRoot();
    strlist_t names = {0};
    list_children(kv, root, &names);
    free(root);
    for (int i = 0; i < names.n; i++) {
        char *opk = kvlangKeytreeSysRwirBackendOp(names.v[i], op);
        kvlangXvalue_t v;
        kvlangXvalueZero(&v);
        kvlangKvGetOne(kv, opk, &v);
        free(opk);
        if (kvlangXvalueNone(&v)) {
            kvlangXvalueFree(&v);
            continue;
        }
        kvlangXvalueFree(&v);
        if (reg)
            sl_push(reg, strdup(names.v[i]));
        if (is_on_duty(kv, names.v[i]))
            sl_push(duty, strdup(names.v[i]));
    }
    sl_free(&names);
}

typedef struct {
    kvlangKv_t *kv;
    char op[NEG_OP_CAP];
    int64_t exp;
    int used;
} negent_t;

static struct {
    pthread_mutex_t mu;
    negent_t e[NEG_CACHE_CAP];
} neg = {.mu = PTHREAD_MUTEX_INITIALIZER};

static bool neg_hit(kvlangKv_t *kv, const char *op) {
    int64_t t = now_ns();
    pthread_mutex_lock(&neg.mu);
    bool hit = false;
    for (int i = 0; i < NEG_CACHE_CAP; i++) {
        if (!neg.e[i].used)
            continue;
        if (neg.e[i].kv != kv || strcmp(neg.e[i].op, op) != 0)
            continue;
        if (t >= neg.e[i].exp) {
            neg.e[i].used = 0;
            break;
        }
        hit = true;
        break;
    }
    pthread_mutex_unlock(&neg.mu);
    return hit;
}

static void neg_put(kvlangKv_t *kv, const char *op) {
    if (strlen(op) >= NEG_OP_CAP) {
        fprintf(stderr, "panic: opcode exceeds delegation cache key\n");
        abort();
    }
    int64_t exp = now_ns() + NEG_CACHE_TTL_NS;
    pthread_mutex_lock(&neg.mu);
    int slot = 0;
    for (int i = 0; i < NEG_CACHE_CAP; i++) {
        if (!neg.e[i].used || (neg.e[i].kv == kv && strcmp(neg.e[i].op, op) == 0)) {
            slot = i;
            break;
        }
    }
    neg.e[slot].kv = kv;
    snprintf(neg.e[slot].op, sizeof neg.e[slot].op, "%s", op);
    neg.e[slot].exp = exp;
    neg.e[slot].used = 1;
    pthread_mutex_unlock(&neg.mu);
}

bool kvlangDispatchIsDelegatedOp(kvlangKv_t *kv, const char *opcode) {
    char *op = kvlangKeytreeCanonOp(opcode);
    if (neg_hit(kv, op)) {
        free(op);
        return false;
    }
    strlist_t duty = {0};
    backends_for(kv, op, NULL, &duty);
    bool ok = duty.n > 0;
    if (!ok)
        neg_put(kv, op);
    sl_free(&duty);
    free(op);
    return ok;
}

static int select_backend(kvlangKv_t *kv, const char *op, char **out, char *err, size_t err_cap) {
    *out = NULL;
    strlist_t reg = {0}, duty = {0};
    backends_for(kv, op, &reg, &duty);
    if (reg.n == 0) {
        snprintf(err, err_cap, "no backend supports opcode=%s", op);
        sl_free(&reg);
        sl_free(&duty);
        return -1;
    }
    if (duty.n == 0) {
        kvlangStrbuf_t got;
        kvlangStrbufInit(&got);
        for (int i = 0; i < reg.n; i++) {
            if (i)
                kvlangStrbufPuts(&got, ", ");
            char *sk = kvlangKeytreeSysRwirBackendStatus(reg.v[i]);
            char *st = get_char(kv, sk);
            free(sk);
            kvlangStrbufPuts(&got, reg.v[i]);
            kvlangStrbufPuts(&got, "=");
            kvlangStrbufPuts(&got, st ? st : "");
            free(st);
        }
        snprintf(err, err_cap,
                 "no on-duty backend for opcode=%s; backend status must be ready|busy, got %s",
                 op, got.p ? got.p : "");
        kvlangStrbufFree(&got);
        sl_free(&reg);
        sl_free(&duty);
        return -1;
    }
    /* 负载相同保留 List 返回序的前者。kvspace 0.3 的 List 按名字排序，没有注册序号。 */
    char *best = duty.v[0];
    double best_load = parse_load(kv, best);
    for (int i = 1; i < duty.n; i++) {
        double ld = parse_load(kv, duty.v[i]);
        if (ld < best_load) {
            best_load = ld;
            best = duty.v[i];
        }
    }
    *out = strdup(best);
    sl_free(&reg);
    sl_free(&duty);
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
        if (strcmp(cats.v[i], "compute") == 0)
            t = 30LL * 1000000000LL;
        else if (strcmp(cats.v[i], "api") == 0)
            t = 120LL * 1000000000LL;
        else if (strcmp(cats.v[i], "agent") == 0)
            t = 300LL * 1000000000LL;
        if (t > d)
            d = t;
    }
    sl_free(&cats);
    return d;
}

static int set_char(kvlangKv_t *kv, const char *key, const char *s, char *err, uint32_t err_cap) {
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    kvlangXvalueNewCharUtf8(&v, s);
    kvlangKvPair_t p = {(char *)key, v};
    int rc = kvlangKvSet(kv, &p, 1, err, err_cap);
    kvlangXvalueFree(&v);
    return rc;
}

static void json_esc(kvlangStrbuf_t *b, const char *s) {
    kvlangStrbufPutc(b, '"');
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            kvlangStrbufPutc(b, '\\');
            kvlangStrbufPutc(b, (char)c);
        } else if (c == '\n')
            kvlangStrbufPuts(b, "\\n");
        else if (c == '\r')
            kvlangStrbufPuts(b, "\\r");
        else if (c == '\t')
            kvlangStrbufPuts(b, "\\t");
        else
            kvlangStrbufPutc(b, (char)c);
    }
    kvlangStrbufPutc(b, '"');
}

static bool is_var_slot(const kvlangParam_t *p) {
    if (!p->name || !p->name[0])
        return false;
    if (p->name[0] == '/')
        return true;
    return kvlangXvalueKindIs(&p->val, KVSPACE_KIND_RWIR) || kvlangXvalueNone(&p->val);
}

static bool keyset_has(char **keys, int n, const char *k) {
    for (int i = 0; i < n; i++)
        if (strcmp(keys[i], k) == 0)
            return true;
    return false;
}

static int fail_msg(kvlangKv_t *kv, const char *vtid, const char *pc, const char *fmt, ...) {
    char body[384];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof body, fmt, ap);
    va_end(ap);
    char msg[420];
    snprintf(msg, sizeof msg, "RuntimeError: delegate: %s", body);
    kvlangVthreadSetError(kv, vtid, pc, msg);
    return KVLANG_DELEGATE_ERR;
}

static int lib_arity(const kvlangXvalue_t *v, int *nr, int *nw, int *dynamic) {
    kvspaceHead_t h;
    if (kvlangXvalueHead(v, &h) != 0)
        return -1;
    int32_t bl = 0;
    const uint8_t *b = kvlangXvalueBody(v, &h, &bl);
    if (!b || bl < 4)
        return -1;
    *nr = b[0] | (b[1] << 8);
    *nw = b[2] | (b[3] << 8);
    *dynamic = bl >= 5 && b[4] ? 1 : 0;
    return 0;
}

static uint64_t g_boot_ns;
static void init_boot(void) {
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    g_boot_ns = (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
}

/* kvspace 0.3 没有 Incr。id = 后端 + vtid + pid + 进程启动时刻 + 进程内序号。
 * 已存在的 status 键直接 abort，不许撞上还在的去重锚点。 */
static void make_task_id(char *out, size_t cap, const char *backend, const char *vtid) {
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    static _Atomic uint64_t seq;
    pthread_once(&once, init_boot);
    uint64_t n = atomic_fetch_add_explicit(&seq, 1, memory_order_relaxed) + 1;
    int wrote = snprintf(out, cap, "rwir:%s:%s:%d:%llu:%llu", backend, vtid, (int)getpid(),
                         (unsigned long long)g_boot_ns, (unsigned long long)n);
    if (wrote < 0 || (size_t)wrote >= cap) {
        fprintf(stderr, "panic: task id does not fit\n");
        abort();
    }
}

int kvlangDispatchDelegate(kvlangKv_t *kv, const char *vtid, const char *pc, kvlangRwirInst_t *inst) {
    char *op = kvlangKeytreeCanonOp(inst->opcode);

    char *sigk = kvlangKeytreeLibSig(op);
    kvlangXvalue_t sig;
    kvlangXvalueZero(&sig);
    kvlangKvGetOne(kv, sigk, &sig);
    free(sigk);
    if (!kvlangXvalueNone(&sig) && kvlangXvalueKindIs(&sig, KVSPACE_KIND_RWFUNC)) {
        strlist_t duty = {0};
        backends_for(kv, op, NULL, &duty);
        if (duty.n == 0) {
            sl_free(&duty);
            kvlangXvalueFree(&sig);
            free(op);
            return KVLANG_DELEGATE_LOCAL;
        }
        kvlangStrbuf_t names;
        kvlangStrbufInit(&names);
        for (int i = 0; i < duty.n; i++) {
            if (i)
                kvlangStrbufPuts(&names, ",");
            kvlangStrbufPuts(&names, duty.v[i]);
        }
        sl_free(&duty);
        int rc = fail_msg(kv, vtid, pc,
                          "%s: on-duty backend %s and local rwfunc both define it; one opcode, one definition",
                          op, names.p ? names.p : "");
        kvlangStrbufFree(&names);
        kvlangXvalueFree(&sig);
        free(op);
        return rc;
    }
    kvlangXvalueFree(&sig);

    char *declk = kvlangKeytreeRwir(op);
    kvlangXvalue_t decl;
    kvlangXvalueZero(&decl);
    kvlangKvGetOne(kv, declk, &decl);
    free(declk);
    if (!kvlangXvalueNone(&decl) && kvlangXvalueKindIs(&decl, KVSPACE_KIND_DEF_RWIR)) {
        int dnr = 0, dnw = 0, dynamic = 0;
        if (lib_arity(&decl, &dnr, &dnw, &dynamic) != 0) {
            kvlangXvalueFree(&decl);
            int rc = fail_msg(kv, vtid, pc, "%s: def rwir body is not a signature", op);
            free(op);
            return rc;
        }
        int min_r = dynamic && dnr > 0 ? dnr - 1 : dnr;
        if (inst->nr < min_r || (!dynamic && inst->nr != dnr)) {
            kvlangXvalueFree(&decl);
            int rc = fail_msg(kv, vtid, pc,
                              "%s: read arity mismatch: call site has %d, declaration has %d", op,
                              inst->nr, dnr);
            free(op);
            return rc;
        }
        if (inst->nw != dnw) {
            kvlangXvalueFree(&decl);
            int rc = fail_msg(kv, vtid, pc,
                              "%s: write arity mismatch: call site has %d, declaration has %d", op,
                              inst->nw, dnw);
            free(op);
            return rc;
        }
    } else if (!kvlangXvalueNone(&decl)) {
        const char *k = kvlangXvalueKind(&decl);
        int rc = fail_msg(kv, vtid, pc, "%s: /lib entry holds kind %s, which is not callable", op,
                          k[0] ? k : "?");
        kvlangXvalueFree(&decl);
        free(op);
        return rc;
    }
    kvlangXvalueFree(&decl);

    char selerr[256];
    char *backend = NULL;
    if (select_backend(kv, op, &backend, selerr, sizeof selerr) != 0) {
        int rc = fail_msg(kv, vtid, pc, "%s", selerr);
        free(op);
        return rc;
    }

    char task_id[256];
    make_task_id(task_id, sizeof task_id, backend, vtid);

    char *fr = kvlangKeytreeFrameRoot(pc);
    if (!fr) {
        int rc = fail_msg(kv, vtid, pc, "%s: pc has no frame: %s", op, pc);
        free(backend);
        free(op);
        return rc;
    }

    char **out_keys = inst->nw > 0 ? calloc((size_t)inst->nw, sizeof(char *)) : NULL;
    int rc = KVLANG_DELEGATE_OK;
    for (int i = 0; i < inst->nw; i++) {
        char *key = kvlangBuiltinResolveWriteSlot(kv, fr, inst->writes[i].name);
        char *werr = kvlangKeytreeCheckWriteKey(vtid, key);
        if (werr) {
            free(key);
            kvlangVthreadSetError(kv, vtid, pc, werr);
            free(werr);
            rc = KVLANG_DELEGATE_ERR;
            goto done;
        }
        out_keys[i] = key;
    }

    kvlangStrbuf_t json;
    kvlangStrbufInit(&json);
    kvlangStrbufPuts(&json, "{\"request_id\":");
    json_esc(&json, task_id);
    kvlangStrbufPuts(&json, ",\"vtid\":");
    json_esc(&json, vtid);
    kvlangStrbufPuts(&json, ",\"pc\":");
    json_esc(&json, pc);
    kvlangStrbufPuts(&json, ",\"opcode\":");
    json_esc(&json, op);
    kvlangStrbufPuts(&json, ",\"inputs\":[");
    for (int i = 0; i < inst->nr; i++) {
        if (i)
            kvlangStrbufPutc(&json, ',');
        kvlangStrbufPutc(&json, '{');
        if (is_var_slot(&inst->reads[i]) && inst->reads[i].name[0] == '/') {
            kvlangStrbufPuts(&json, "\"key\":");
            json_esc(&json, inst->reads[i].name);
        } else if (is_var_slot(&inst->reads[i])) {
            char *rk = kvlangBuiltinResolveWriteSlot(kv, fr, inst->reads[i].name);
            if (keyset_has(out_keys, inst->nw, rk)) {
                kvlangXvalue_t val;
                kvlangXvalueZero(&val);
                kvlangBuiltinResolveReadValue(kv, fr, inst->reads[i].name, &inst->reads[i].val, &val);
                char *disp = NULL;
                kvlangDisplay(&val, &disp);
                kvlangStrbufPuts(&json, "\"value\":");
                json_esc(&json, disp ? disp : "");
                free(disp);
                kvlangXvalueFree(&val);
            } else {
                kvlangStrbufPuts(&json, "\"key\":");
                json_esc(&json, rk);
            }
            free(rk);
        } else {
            char *disp = NULL;
            kvlangDisplay(&inst->reads[i].val, &disp);
            kvlangStrbufPuts(&json, "\"value\":");
            json_esc(&json, disp ? disp : "");
            free(disp);
        }
        kvlangStrbufPutc(&json, '}');
    }
    kvlangStrbufPuts(&json, "],\"outputs\":[");
    for (int i = 0; i < inst->nw; i++) {
        if (i)
            kvlangStrbufPutc(&json, ',');
        kvlangStrbufPuts(&json, "{\"key\":");
        json_esc(&json, out_keys[i]);
        kvlangStrbufPutc(&json, '}');
    }
    char *done_key = kvlangKeytreeDoneRwir(task_id);
    kvlangStrbufPuts(&json, "],\"done_key\":");
    json_esc(&json, done_key);
    kvlangStrbufPutc(&json, '}');

    char *status_key = kvlangKeytreeSysTask(task_id, SEG_STATUS);
    kvlangXvalue_t existing;
    kvlangXvalueZero(&existing);
    kvlangKvGetOne(kv, status_key, &existing);
    if (!kvlangXvalueNone(&existing)) {
        fprintf(stderr, "panic: task id already exists: %s\n", status_key);
        abort();
    }
    kvlangXvalueFree(&existing);

    char err[256];
    if (set_char(kv, status_key, "pending", err, sizeof err) != 0) {
        rc = fail_msg(kv, vtid, pc, "%s: init task status: %s", op, err);
        goto enqueue_done;
    }
    for (int i = 0; i < inst->nw; i++) {
        if (kvlangKvDel(kv, out_keys[i], err, sizeof err) != 0) {
            rc = fail_msg(kv, vtid, pc, "%s: clear output slot %s: %s", op, out_keys[i], err);
            goto enqueue_done;
        }
    }

    /* cmd/<task_id> 是持久任务。kvspace 0.3 的 Notify 不是队列，忙着的后端收不到。 */
    char *cmd = kvlangKeytreeSysRwirBackendCmd(backend);
    kvlangStrbuf_t cmdk;
    kvlangStrbufInit(&cmdk);
    kvlangStrbufPuts(&cmdk, cmd);
    kvlangStrbufPutc(&cmdk, '/');
    kvlangStrbufPuts(&cmdk, task_id);
    free(cmd);
    if (set_char(kv, cmdk.p, json.p, err, sizeof err) != 0) {
        rc = fail_msg(kv, vtid, pc, "push task: %s", err);
        kvlangStrbufFree(&cmdk);
        goto enqueue_done;
    }
    kvlangStrbufFree(&cmdk);

    kvlangVthreadSet(kv, vtid, pc, "wait");
    kvlangLogDebug("[%s] DELEGATE %s task=%s", vtid, inst->opcode, task_id);

    kvlangXvalue_t want;
    kvlangXvalueZero(&want);
    kvlangXvalueNewCharUtf8(&want, "done");
    kvlangXvalue_t got;
    kvlangXvalueZero(&got);
    kvlangKvWatch(kv, status_key, &want, (uint64_t)timeout_for(kv, backend), &got);
    kvlangXvalueFree(&want);
    kvlangXvalueFree(&got);

    char *st = get_char(kv, status_key);
    if (!st || strcmp(st, "done") != 0) {
        rc = fail_msg(kv, vtid, pc, "%s: timeout or failed (status=%s)", op, st ? st : "");
        free(st);
        goto enqueue_done;
    }
    free(st);

    for (int i = 0; i < inst->nw; i++) {
        kvlangXvalue_t ov;
        kvlangXvalueZero(&ov);
        kvlangKvGetOne(kv, out_keys[i], &ov);
        bool empty = kvlangXvalueNone(&ov);
        kvlangXvalueFree(&ov);
        if (empty) {
            rc = fail_msg(kv, vtid, pc,
                          "%s: executor reported done but left output slot %s unwritten", op,
                          out_keys[i]);
            goto enqueue_done;
        }
    }

    kvlangStrbuf_t npc;
    kvlangStrbufInit(&npc);
    kvlangRwirNextPc(pc, &npc);
    kvlangVthreadSet(kv, vtid, npc.p, "running");
    kvlangStrbufFree(&npc);
    kvlangLogDebug("[%s] DONE %s task=%s", vtid, inst->opcode, task_id);
    /* status=done 留下当去重锚点。0.3 没有 Expire，不能从 List 摘掉还让 Get 看得见。 */

enqueue_done:
    free(done_key);
    free(status_key);
    kvlangStrbufFree(&json);
done:
    if (out_keys) {
        for (int i = 0; i < inst->nw; i++)
            free(out_keys[i]);
        free(out_keys);
    }
    free(fr);
    free(backend);
    free(op);
    return rc;
}
