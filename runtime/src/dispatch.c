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

void kvlangDispatchSetDefaultTimeoutNanoseconds(int64_t ns) { default_timeout_ns = ns; }

static int64_t now_nanoseconds(void) {
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    return (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec;
}

static char *get_char(kvspace_t *kv, const char *key) {
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    kvlangKvspaceGetOne(kv, key, &v);
    char *s = kvlangXvalueNone(&v) ? NULL : kvlangXvalueValueString(&v);
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

static bool heartbeat_fresh(kvspace_t *kv, const char *backend) {
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

static bool is_on_duty(kvspace_t *kv, const char *backend) {
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

static double parse_load(kvspace_t *kv, const char *backend) {
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
    char **items;
    int count, capacity;
} strlist_t;

static void strlist_push(strlist_t *l, char *s) {
    if (l->count == l->capacity) {
        l->capacity = l->capacity ? l->capacity * 2 : 8;
        l->items = realloc(l->items, sizeof(char *) * (size_t)l->capacity);
    }
    l->items[l->count++] = s;
}

static void strlist_free(strlist_t *l) {
    for (int i = 0; i < l->count; i++)
        free(l->items[i]);
    free(l->items);
    l->items = NULL;
    l->count = l->capacity = 0;
}

static void list_children(kvspace_t *kv, const char *dir, strlist_t *out) {
    char *pref = kvlangKeytreeStack(dir);
    char **names = NULL;
    int n = 0;
    kvlangKvspaceList(kv, pref, false, false, &names, &n);
    free(pref);
    for (int i = 0; i < n; i++) {
        size_t ln = strlen(names[i]);
        if (ln > 0 && names[i][ln - 1] == '/')
            names[i][ln - 1] = 0;
        strlist_push(out, names[i]);
    }
    free(names);
}

static void backends_for(kvspace_t *kv, const char *op, strlist_t *reg, strlist_t *duty) {
    if (!kvlangKeytreeValidSegment(op))
        return;
    char *root = kvlangKeytreeSysRwirBackendRoot();
    strlist_t names = {0};
    list_children(kv, root, &names);
    free(root);
    for (int i = 0; i < names.count; i++) {
        char *opk = kvlangKeytreeSysRwirBackendOp(names.items[i], op);
        kvlangXvalue_t v;
        kvlangXvalueZero(&v);
        kvlangKvspaceGetOne(kv, opk, &v);
        free(opk);
        if (kvlangXvalueNone(&v)) {
            continue;
        }
        if (reg)
            strlist_push(reg, strdup(names.items[i]));
        if (is_on_duty(kv, names.items[i]))
            strlist_push(duty, strdup(names.items[i]));
    }
    strlist_free(&names);
}

typedef struct {
    kvspace_t *kvspace;
    char opcode[NEG_OP_CAP];
    int64_t expires_at_ns;
    int in_use;
} negent_t;

static struct {
    pthread_mutex_t mu;
    negent_t e[NEG_CACHE_CAP];
} neg = {.mu = PTHREAD_MUTEX_INITIALIZER};

static bool negative_cache_hit(kvspace_t *kv, const char *op) {
    int64_t t = now_nanoseconds();
    pthread_mutex_lock(&neg.mu);
    bool hit = false;
    for (int i = 0; i < NEG_CACHE_CAP; i++) {
        if (!neg.e[i].in_use)
            continue;
        if (neg.e[i].kvspace != kv || strcmp(neg.e[i].opcode, op) != 0)
            continue;
        if (t >= neg.e[i].expires_at_ns) {
            neg.e[i].in_use = 0;
            break;
        }
        hit = true;
        break;
    }
    pthread_mutex_unlock(&neg.mu);
    return hit;
}

static void negative_cache_put(kvspace_t *kv, const char *op) {
    if (strlen(op) >= NEG_OP_CAP) {
        fprintf(stderr, "panic: opcode exceeds delegation cache key\n");
        abort();
    }
    int64_t exp = now_nanoseconds() + NEG_CACHE_TTL_NS;
    pthread_mutex_lock(&neg.mu);
    int slot = 0;
    for (int i = 0; i < NEG_CACHE_CAP; i++) {
        if (!neg.e[i].in_use || (neg.e[i].kvspace == kv && strcmp(neg.e[i].opcode, op) == 0)) {
            slot = i;
            break;
        }
    }
    neg.e[slot].kvspace = kv;
    snprintf(neg.e[slot].opcode, sizeof neg.e[slot].opcode, "%s", op);
    neg.e[slot].expires_at_ns = exp;
    neg.e[slot].in_use = 1;
    pthread_mutex_unlock(&neg.mu);
}

bool kvlangDispatchIsDelegatedOp(kvspace_t *kv, const char *opcode) {
    const char *op = kvlangKeytreeCanonicalOp(opcode);
    if (negative_cache_hit(kv, op))
        return false;
    strlist_t duty = {0};
    backends_for(kv, op, NULL, &duty);
    bool ok = duty.count > 0;
    if (!ok)
        negative_cache_put(kv, op);
    strlist_free(&duty);
    return ok;
}

typedef struct {
    char **items;
    size_t count, capacity, length;
} parts_t;

static void parts_push(parts_t *p, char *s) {
    if (p->count == p->capacity) {
        p->capacity = p->capacity ? p->capacity * 2 : 8;
        p->items = realloc(p->items, sizeof(char *) * p->capacity);
        if (!p->items)
            kvlangDie("out of memory building string parts");
    }
    p->items[p->count++] = s;
    p->length += strlen(s);
}

/* 拼接后释放所有片段；sep 仅用于分隔。 */
static char *parts_join(parts_t *p, const char *sep) {
    size_t sl = strlen(sep);
    size_t total = p->length + (p->count > 1 ? (p->count - 1) * sl : 0);
    char *out = malloc(total + 1);
    if (!out)
        kvlangDie("out of memory joining string parts");
    size_t off = 0;
    for (size_t i = 0; i < p->count; i++) {
        if (i && sl) {
            memcpy(out + off, sep, sl);
            off += sl;
        }
        size_t n = strlen(p->items[i]);
        memcpy(out + off, p->items[i], n);
        off += n;
        free(p->items[i]);
    }
    out[off] = 0;
    free(p->items);
    *p = (parts_t){0};
    return out;
}

static char *json_escape_dup(const char *s) {
    size_t n = 2; /* quotes */
    for (const char *p = s ? s : ""; *p; p++) {
        unsigned char c = (unsigned char)*p;
        n += (c == '"' || c == '\\' || c == '\n' || c == '\r' || c == '\t')
                 ? 2
                 : c < 0x20 ? 6 : 1;
    }
    char *o = malloc(n + 1);
    if (!o)
        kvlangDie("out of memory escaping json");
    static const char hex[] = "0123456789abcdef";
    size_t off = 0;
    o[off++] = '"';
    for (const char *p = s ? s : ""; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') {
            o[off++] = '\\';
            o[off++] = (char)c;
        } else if (c == '\n') {
            o[off++] = '\\';
            o[off++] = 'n';
        } else if (c == '\r') {
            o[off++] = '\\';
            o[off++] = 'r';
        } else if (c == '\t') {
            o[off++] = '\\';
            o[off++] = 't';
        } else if (c < 0x20) {
            o[off++] = '\\';
            o[off++] = 'u';
            o[off++] = '0';
            o[off++] = '0';
            o[off++] = hex[(c >> 4) & 0xF];
            o[off++] = hex[c & 0xF];
        } else {
            o[off++] = (char)c;
        }
    }
    o[off++] = '"';
    o[off] = 0;
    return o;
}

static int select_backend(kvspace_t *kv, const char *op, char **out, char *err, size_t err_cap) {
    *out = NULL;
    strlist_t reg = {0}, duty = {0};
    backends_for(kv, op, &reg, &duty);
    if (reg.count == 0) {
        snprintf(err, err_cap, "no backend supports opcode=%s", op);
        strlist_free(&reg);
        strlist_free(&duty);
        return -1;
    }
    if (duty.count == 0) {
        parts_t got = {0};
        for (int i = 0; i < reg.count; i++) {
            char *sk = kvlangKeytreeSysRwirBackendStatus(reg.items[i]);
            char *st = get_char(kv, sk);
            free(sk);
            parts_push(&got, kvlangStringFormat("%s=%s", reg.items[i], st ? st : ""));
            free(st);
        }
        char *got_line = parts_join(&got, ", ");
        snprintf(err, err_cap,
                 "no on-duty backend for opcode=%s; backend status must be ready|busy, got %s",
                 op, got_line);
        free(got_line);
        strlist_free(&reg);
        strlist_free(&duty);
        return -1;
    }
    char *best = duty.items[0];
    double best_load = parse_load(kv, best);
    for (int i = 1; i < duty.count; i++) {
        double ld = parse_load(kv, duty.items[i]);
        if (ld < best_load) {
            best_load = ld;
            best = duty.items[i];
        }
    }
    *out = strdup(best);
    strlist_free(&reg);
    strlist_free(&duty);
    return 0;
}

static int64_t timeout_for(kvspace_t *kv, const char *backend) {
    int64_t d = default_timeout_ns;
    char *root = kvlangKeytreeSysRwirBackendCategoryRoot(backend);
    strlist_t cats = {0};
    list_children(kv, root, &cats);
    free(root);
    for (int i = 0; i < cats.count; i++) {
        int64_t t = 0;
        if (strcmp(cats.items[i], "compute") == 0)
            t = 30LL * 1000000000LL;
        else if (strcmp(cats.items[i], "api") == 0)
            t = 120LL * 1000000000LL;
        else if (strcmp(cats.items[i], "agent") == 0)
            t = 300LL * 1000000000LL;
        if (t > d)
            d = t;
    }
    strlist_free(&cats);
    return d;
}

static int set_char(kvspace_t *kv, const char *key, const char *s, char *err, uint32_t err_cap) {
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    kvlangXvalueNewCharUtf8(&v, s);
    kvlangKvPair_t p = {(char *)key, v};
    int rc = kvlangKvspaceSet(kv, &p, 1, err, err_cap);
    kvlangXvalueRelease(&v);
    return rc;
}


static bool is_var_slot(const kvlangParam_t *p) {
    if (!p->name || !p->name[0] || strstr(p->name, "/" RUNTIME_MEMBER_SEP "operand["))
        return false;
    return p->name[0] == '/' || kvlangXvalueKindIs(&p->value, KVSPACE_KIND_RWIR) ||
           kvlangXvalueNone(&p->value);
}

static bool keyset_has(char **keys, int n, const char *k) {
    for (int i = 0; i < n; i++)
        if (strcmp(keys[i], k) == 0)
            return true;
    return false;
}

static int fail_message(kvspace_t *kv, const char *vtid, const char *pc, const char *fmt, ...) {
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

int kvlangDispatchDelegate(kvspace_t *kv, const char *vtid, const char *pc, kvlangRwirInst_t *inst) {
    const char *op = kvlangKeytreeCanonicalOp(inst->opcode);

    char *sigk = kvlangKeytreeLibSignature(op);
    kvlangXvalue_t sig;
    kvlangXvalueZero(&sig);
    kvlangKvspaceGetOne(kv, sigk, &sig);
    free(sigk);
    if (!kvlangXvalueNone(&sig) && kvlangXvalueKindIs(&sig, KVSPACE_KIND_RWFUNC)) {
        strlist_t duty = {0};
        backends_for(kv, op, NULL, &duty);
        if (duty.count == 0) {
            strlist_free(&duty);
            return KVLANG_DELEGATE_LOCAL;
        }
        parts_t names = {0};
        for (int i = 0; i < duty.count; i++)
            parts_push(&names, strdup(duty.items[i]));
        strlist_free(&duty);
        char *name_list = parts_join(&names, ",");
        int rc = fail_message(kv, vtid, pc,
                          "%s: on-duty backend %s and local rwfunc both define it; one opcode, one definition",
                          op, name_list);
        free(name_list);
        return rc;
    }

    char *declk = kvlangKeytreeRwir(op);
    kvlangXvalue_t decl;
    kvlangXvalueZero(&decl);
    kvlangKvspaceGetOne(kv, declk, &decl);
    free(declk);
    if (!kvlangXvalueNone(&decl) && kvlangXvalueKindIs(&decl, KVSPACE_KIND_DEF_RWIR)) {
        int dnr = 0, dnw = 0, dynamic = 0;
        if (lib_arity(&decl, &dnr, &dnw, &dynamic) != 0) {
            return fail_message(kv, vtid, pc, "%s: def rwir body is not a signature", op);
        }
        int min_r = dynamic && dnr > 0 ? dnr - 1 : dnr;
        if (inst->read_count < min_r || (!dynamic && inst->read_count != dnr)) {
            return fail_message(kv, vtid, pc,
                            "%s: read arity mismatch: call site has %d, declaration has %d", op,
                            inst->read_count, dnr);
        }
        if (inst->write_count != dnw) {
            return fail_message(kv, vtid, pc,
                            "%s: write arity mismatch: call site has %d, declaration has %d", op,
                            inst->write_count, dnw);
        }
    } else if (!kvlangXvalueNone(&decl)) {
        const char *k = kvlangXvalueKind(&decl);
        int rc = fail_message(kv, vtid, pc, "%s: /lib entry holds kind %s, which is not callable", op,
                          k[0] ? k : "?");
        return rc;
    }

    char selerr[256];
    char *backend = NULL;
    if (select_backend(kv, op, &backend, selerr, sizeof selerr) != 0)
        return fail_message(kv, vtid, pc, "%s", selerr);

    char task_id[256];
    make_task_id(task_id, sizeof task_id, backend, vtid);

    char *fr = kvlangKeytreeFrameRoot(pc);
    if (!fr) {
        free(backend);
        return fail_message(kv, vtid, pc, "%s: pc has no frame: %s", op, pc);
    }

    char **out_keys = inst->write_count > 0 ? calloc((size_t)inst->write_count, sizeof(char *)) : NULL;
    int rc = KVLANG_DELEGATE_OK;
    for (int i = 0; i < inst->write_count; i++) {
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

    char *json = NULL;
    parts_t j = {0};
    parts_push(&j, strdup("{\"request_id\":"));
    parts_push(&j, json_escape_dup(task_id));
    parts_push(&j, strdup(",\"vtid\":"));
    parts_push(&j, json_escape_dup(vtid));
    parts_push(&j, strdup(",\"pc\":"));
    parts_push(&j, json_escape_dup(pc));
    parts_push(&j, strdup(",\"opcode\":"));
    parts_push(&j, json_escape_dup(op));
    parts_push(&j, strdup(",\"inputs\":["));
    for (int i = 0; i < inst->read_count; i++) {
        if (i)
            parts_push(&j, strdup(","));
        parts_push(&j, strdup("{"));
        char *rk = is_var_slot(&inst->reads[i])
                       ? kvlangBuiltinResolveWriteSlot(kv, fr, inst->reads[i].name)
                       : NULL;
        if (rk && !keyset_has(out_keys, inst->write_count, rk)) {
            parts_push(&j, strdup("\"key\":"));
            parts_push(&j, json_escape_dup(rk));
        } else {
            kvlangXvalue_t val;
            kvlangXvalueZero(&val);
            kvlangBuiltinResolveReadValue(kv, fr, inst->reads[i].name, &inst->reads[i].value, &val);
            char *disp = NULL;
            kvlangDisplay(&val, &disp);
            parts_push(&j, strdup("\"value\":"));
            parts_push(&j, json_escape_dup(disp ? disp : ""));
            free(disp);
        }
        free(rk);
        parts_push(&j, strdup("}"));
    }
    parts_push(&j, strdup("],\"outputs\":["));
    for (int i = 0; i < inst->write_count; i++) {
        if (i)
            parts_push(&j, strdup(","));
        parts_push(&j, strdup("{\"key\":"));
        parts_push(&j, json_escape_dup(out_keys[i]));
        parts_push(&j, strdup("}"));
    }
    char *done_key = kvlangKeytreeDoneRwir(task_id);
    parts_push(&j, strdup("],\"done_key\":"));
    parts_push(&j, json_escape_dup(done_key));
    free(done_key);
    parts_push(&j, strdup("}"));
    json = parts_join(&j, "");

    char *status_key = kvlangKeytreeSysTask(task_id, SEG_STATUS);
    kvlangXvalue_t existing;
    kvlangXvalueZero(&existing);
    kvlangKvspaceGetOne(kv, status_key, &existing);
    if (!kvlangXvalueNone(&existing)) {
        fprintf(stderr, "panic: task id already exists: %s\n", status_key);
        abort();
    }

    char err[256];
    if (set_char(kv, status_key, "pending", err, sizeof err) != 0) {
        rc = fail_message(kv, vtid, pc, "%s: init task status: %s", op, err);
        goto enqueue_done;
    }
    for (int i = 0; i < inst->write_count; i++) {
        if (kvlangKvspaceDelete(kv, out_keys[i], err, sizeof err) != 0) {
            rc = fail_message(kv, vtid, pc, "%s: clear output slot %s: %s", op, out_keys[i], err);
            goto enqueue_done;
        }
    }

    char *cmd = kvlangKeytreeSysRwirBackendCommand(backend);
    char cmdk[KV_KEY_MAX];
    kvlangKeyFormat(cmdk, sizeof cmdk, "%s/%s", cmd, task_id);
    free(cmd);
    if (set_char(kv, cmdk, json, err, sizeof err) != 0) {
        rc = fail_message(kv, vtid, pc, "push task: %s", err);
        goto enqueue_done;
    }

    kvlangVthreadSet(kv, vtid, pc, "wait");
    kvlangLogDebug("[%s] DELEGATE %s task=%s", vtid, inst->opcode, task_id);

    kvlangXvalue_t want;
    kvlangXvalueZero(&want);
    kvlangXvalueNewCharUtf8(&want, "done");
    kvlangXvalue_t got;
    kvlangXvalueZero(&got);
    kvlangKvspaceWatch(kv, status_key, &want, (uint64_t)timeout_for(kv, backend), &got);
    kvlangXvalueRelease(&want);
    kvlangXvalueRelease(&got);

    char *st = get_char(kv, status_key);
    if (!st || strcmp(st, "done") != 0) {
        rc = fail_message(kv, vtid, pc, "%s: timeout or failed (status=%s)", op, st ? st : "");
        free(st);
        goto enqueue_done;
    }
    free(st);

    for (int i = 0; i < inst->write_count; i++) {
        kvlangXvalue_t ov;
        kvlangXvalueZero(&ov);
        kvlangKvspaceGetOne(kv, out_keys[i], &ov);
        bool empty = kvlangXvalueNone(&ov);
        if (empty) {
            rc = fail_message(kv, vtid, pc,
                          "%s: executor reported done but left output slot %s unwritten", op,
                          out_keys[i]);
            goto enqueue_done;
        }
    }

    char npc[KV_KEY_MAX];
    if (kvlangRwirNextPcBuffer(pc, npc, sizeof npc) == 0)
        kvlangDie("next pc exceeds %zu bytes", sizeof npc);
    kvlangVthreadSet(kv, vtid, npc, "running");
    kvlangLogDebug("[%s] DONE %s task=%s", vtid, inst->opcode, task_id);

enqueue_done:
    free(status_key);
    free(json);
done:
    if (out_keys) {
        for (int i = 0; i < inst->write_count; i++)
            free(out_keys[i]);
        free(out_keys);
    }
    free(fr);
    free(backend);
    return rc;
}
