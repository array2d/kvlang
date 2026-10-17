#include "runtime_internal.h"

const char *kvlangKeytreeVtidFromPc(const char *pc, kvlangStrbuf_t *out) {
    kvlangStrbufClear(out);
    const char *pfx = VTHREAD_ROOT PATH_SEP;
    size_t pl = strlen(pfx);
    if (strncmp(pc, pfx, pl) != 0) return "";
    const char *rest = pc + pl;
    const char *slash = strchr(rest, '/');
    if (slash) kvlangStrbufPutn(out, rest, (size_t)(slash - rest));
    else kvlangStrbufPuts(out, rest);
    return out->p;
}

char *kvlangKeytreeStack(const char *root) {
    size_t n = strlen(root);
    while (n > 0 && root[n - 1] == '/') n--;
    char *r = malloc(n + 2);
    memcpy(r, root, n); r[n] = '/'; r[n + 1] = 0;
    return r;
}

static void keytree_abort(const char *what, const char *s) {
    fprintf(stderr, "keytree: %s: %s\n", what, s ? s : "(null)");
    abort();
}

static const char *parse_vthread_frame(const char *s, kvlangStrbuf_t *vtid, int *d) {
    const char *pfx = VTHREAD_ROOT PATH_SEP;
    size_t pl = strlen(pfx);
    if (!s || strncmp(s, pfx, pl) != 0) keytree_abort("not /vthread/", s);
    const char *rest = s + pl;
    const char *slash = strchr(rest, '/');
    if (!slash || slash == rest) keytree_abort("missing vtid", s);
    if (vtid) {
        kvlangStrbufClear(vtid);
        kvlangStrbufPutn(vtid, rest, (size_t)(slash - rest));
    }
    if (slash[1] != '[') keytree_abort("frame coord not [d]", s);
    char *end = NULL;
    long depth = strtol(slash + 2, &end, 10);
    if (end == slash + 2 || *end != ']') keytree_abort("frame coord not [d]", s);
    if (depth < 1 || depth > MAX_STACK_DEPTH) keytree_abort("frame depth out of range", s);
    if (d) *d = (int)depth;
    return end + 1;
}

void kvlangKeytreeParsePc(const char *pc, kvlangStrbuf_t *vtid, int *d, int *irseq, int *params) {
    const char *rest = parse_vthread_frame(pc, vtid, d);
    if (*rest != '/') keytree_abort("PC missing /[irseq,params]", pc);
    if (rest[1] != '[') keytree_abort("instruction coord not 2-D", pc);
    char *end = NULL;
    long i = strtol(rest + 2, &end, 10);
    if (end == rest + 2 || *end != ',') keytree_abort("instruction coord not 2-D", pc);
    long j = strtol(end + 1, &end, 10);
    if (*end != ']' || end[1] != 0) keytree_abort("instruction coord trailing junk", pc);
    if (i < 1) keytree_abort("irseq < 1", pc);
    if (irseq) *irseq = (int)i;
    if (params) *params = (int)j;
}

int kvlangKeytreeFrameDepth(const char *frame_or_pc) {
    int d = 0;
    const char *rest = parse_vthread_frame(frame_or_pc, NULL, &d);
    if (*rest == 0) return d;
    kvlangKeytreeParsePc(frame_or_pc, NULL, &d, NULL, NULL);
    return d;
}

char *kvlangKeytreeFrameAt(const char *vtid, int d) {
    if (!vtid || !vtid[0] || d < 1 || d > MAX_STACK_DEPTH) keytree_abort("FrameAt range", vtid);
    kvlangStrbuf_t b; kvlangStrbufInit(&b);
    kvlangStrbufPrintf(&b, "%s/%s/[%d]", VTHREAD_ROOT, vtid, d);
    return kvlangStrbufDetach(&b);
}

char *kvlangKeytreeFrameRoot(const char *pc) {
    kvlangKeytreeParsePc(pc, NULL, NULL, NULL, NULL);
    const char *last = strrchr(pc, '/');
    size_t n = (size_t)(last - pc);
    char *r = malloc(n + 1);
    memcpy(r, pc, n);
    r[n] = 0;
    return r;
}

static char *trim_right_join(const char *root, const char *suffix) {
    size_t n = strlen(root);
    while (n > 0 && root[n - 1] == '/') n--;
    size_t sl = strlen(suffix);
    char *r = malloc(n + sl + 1);
    memcpy(r, root, n); memcpy(r + n, suffix, sl); r[n + sl] = 0;
    return r;
}

char *kvlangKeytreeEntryPc(const char *root) { return trim_right_join(root, "/[1,0]"); }

char *kvlangKeytreeMember(const char *base, const char *name) {
    size_t bl = strlen(base), nl = strlen(name);
    char *r = malloc(bl + nl + 2);
    memcpy(r, base, bl); r[bl] = '.'; memcpy(r + bl + 1, name, nl); r[bl + 1 + nl] = 0;
    return r;
}

char *kvlangKeytreeLibFunc(const char *pkg, const char *name) {
    kvlangStrbuf_t b; kvlangStrbufInit(&b);
    kvlangStrbufPuts(&b, LIB_ROOT PATH_SEP);
    if (pkg && pkg[0]) { kvlangStrbufPuts(&b, pkg); kvlangStrbufPutc(&b, '.'); }
    kvlangStrbufPuts(&b, name);
    return kvlangStrbufDetach(&b);
}

char *kvlangKeytreeRwir(const char *opcode) {
    kvlangStrbuf_t b; kvlangStrbufInit(&b);
    kvlangStrbufPuts(&b, LIB_ROOT PATH_SEP);
    kvlangStrbufPuts(&b, opcode);
    return kvlangStrbufDetach(&b);
}

void kvlangKeytreeVthread(const char *vtid, kvlangStrbuf_t *out) {
    kvlangStrbufClear(out);
    kvlangStrbufPuts(out, VTHREAD_ROOT PATH_SEP);
    kvlangStrbufPuts(out, vtid);
}

static void kvlangVthreadMember(const char *vtid, const char *seg, kvlangStrbuf_t *out) {
    kvlangStrbufClear(out);
    kvlangStrbufPuts(out, VTHREAD_ROOT PATH_SEP);
    kvlangStrbufPuts(out, vtid);
    kvlangStrbufPutc(out, '/');
    kvlangStrbufPuts(out, RUNTIME_MEMBER_SEP);
    kvlangStrbufPuts(out, seg);
}

void kvlangKeytreeVthreadPc(const char *vtid, kvlangStrbuf_t *out) { kvlangVthreadMember(vtid, SEG_PC, out); }
void kvlangKeytreeVthreadStatus(const char *vtid, kvlangStrbuf_t *out) { kvlangVthreadMember(vtid, SEG_STATUS, out); }
void kvlangKeytreeVthreadDebugger(const char *vtid, kvlangStrbuf_t *out) { kvlangVthreadMember(vtid, "debugger", out); }

void kvlangKeytreeVthreadStatusMsg(const char *vtid, const char *status, kvlangStrbuf_t *out) {
    kvlangVthreadMember(vtid, status, out);
    kvlangStrbufPutc(out, '/');
    kvlangStrbufPuts(out, SEG_MSG);
}

static void frame_member(const char *root, const char *seg, kvlangStrbuf_t *out) {
    kvlangStrbufClear(out);
    char *s = kvlangKeytreeStack(root);
    kvlangStrbufPuts(out, s);
    free(s);
    kvlangStrbufPuts(out, RUNTIME_MEMBER_SEP);
    kvlangStrbufPuts(out, seg);
}

void kvlangKeytreeFrameCallpc(const char *root, kvlangStrbuf_t *out) { frame_member(root, SEG_CALLPC, out); }
void kvlangKeytreeFrameReturnpc(const char *root, kvlangStrbuf_t *out) { frame_member(root, SEG_RETURNPC, out); }
void kvlangKeytreeFrameRo(const char *root, kvlangStrbuf_t *out) { frame_member(root, SEG_RO, out); }

char *kvlangKeytreeCanonOp(const char *opcode) {
    const char *p = opcode ? opcode : "";
    if (strncmp(p, LIB_ROOT PATH_SEP, 5) == 0) p += 5;
    return strdup(p);
}

bool kvlangKeytreeValidSegment(const char *s) {
    if (!s || !s[0]) return false;
    if (strcmp(s, ".") == 0 || strcmp(s, "..") == 0) return false;
    if (strcmp(s, MEMBER_SEP) == 0) return false;
    return strchr(s, '/') == NULL;
}

static char *join3(const char *a, const char *b, const char *c) {
    kvlangStrbuf_t s; kvlangStrbufInit(&s);
    kvlangStrbufPuts(&s, a);
    if (b && b[0]) { kvlangStrbufPutc(&s, '/'); kvlangStrbufPuts(&s, b); }
    if (c && c[0]) { kvlangStrbufPutc(&s, '/'); kvlangStrbufPuts(&s, c); }
    return kvlangStrbufDetach(&s);
}

char *kvlangKeytreeSysRwirBackendRoot(void) {
    return strdup(SYS_ROOT PATH_SEP SEG_RWIR_BACKEND);
}

char *kvlangKeytreeSysRwirBackend(const char *name) {
    return join3(SYS_ROOT PATH_SEP SEG_RWIR_BACKEND, name, NULL);
}

char *kvlangKeytreeSysRwirBackendOp(const char *name, const char *opcode) {
    kvlangStrbuf_t s; kvlangStrbufInit(&s);
    kvlangStrbufPuts(&s, SYS_ROOT PATH_SEP SEG_RWIR_BACKEND "/");
    kvlangStrbufPuts(&s, name);
    kvlangStrbufPuts(&s, "/" SEG_OP "/");
    kvlangStrbufPuts(&s, opcode);
    return kvlangStrbufDetach(&s);
}

char *kvlangKeytreeSysRwirBackendCmd(const char *name) {
    return join3(SYS_ROOT PATH_SEP SEG_RWIR_BACKEND, name, SEG_CMD);
}

char *kvlangKeytreeSysRwirBackendStatus(const char *name) {
    return join3(SYS_ROOT PATH_SEP SEG_RWIR_BACKEND, name, SEG_STATUS);
}

char *kvlangKeytreeSysRwirBackendLoad(const char *name) {
    return join3(SYS_ROOT PATH_SEP SEG_RWIR_BACKEND, name, SEG_LOAD);
}

char *kvlangKeytreeSysRwirBackendHeartbeat(const char *name) {
    return join3(SYS_ROOT PATH_SEP SEG_RWIR_BACKEND, name, SEG_LAST_HEARTBEAT);
}

char *kvlangKeytreeSysRwirBackendCategoryRoot(const char *name) {
    return join3(SYS_ROOT PATH_SEP SEG_RWIR_BACKEND, name, SEG_CATEGORY);
}

char *kvlangKeytreeSysTask(const char *task_id, const char *field) {
    kvlangStrbuf_t s; kvlangStrbufInit(&s);
    kvlangStrbufPuts(&s, SYS_ROOT PATH_SEP SEG_TASK "/");
    kvlangStrbufPuts(&s, task_id);
    char *base = kvlangStrbufDetach(&s);
    char *r = kvlangKeytreeMember(base, field);
    free(base);
    return r;
}

char *kvlangKeytreeDoneRwir(const char *task_id) {
    return join3(DONE_ROOT PATH_SEP SEG_RWIR, task_id, NULL);
}

char *kvlangKeytreeVthreadDelegSeq(const char *vtid) {
    kvlangStrbuf_t s; kvlangStrbufInit(&s);
    kvlangKeytreeVthread(vtid, &s);
    kvlangStrbufPutc(&s, '/');
    kvlangStrbufPuts(&s, RUNTIME_MEMBER_SEP);
    kvlangStrbufPuts(&s, SEG_DELEGSEQ);
    return kvlangStrbufDetach(&s);
}

char *kvlangKeytreeLibSig(const char *opcode) {
    kvlangStrbuf_t s; kvlangStrbufInit(&s);
    kvlangStrbufPuts(&s, LIB_ROOT PATH_SEP);
    kvlangStrbufPuts(&s, opcode);
    kvlangStrbufPuts(&s, "/[0,0]");
    return kvlangStrbufDetach(&s);
}

char *kvlangKeytreeCheckWriteKey(const char *vtid, const char *key) {
    if (!key || key[0] != '/') {
        char *e = malloc(160);
        snprintf(e, 160, "ValueError: write slot is not an absolute path: \"%s\"; help: a write slot must resolve to a canonical path starting with /", key ? key : "");
        return e;
    }
    const char *p = key + 1;
    while (*p) {
        const char *slash = strchr(p, '/');
        size_t n = slash ? (size_t)(slash - p) : strlen(p);
        if (n == 0 || (n == 1 && p[0] == '.') || (n == 2 && p[0] == '.' && p[1] == '.')) {
            char *e = malloc(160);
            snprintf(e, 160, "ValueError: write slot is not a canonical path: \"%s\"; help: a path may not contain an empty segment, . or ..", key);
            return e;
        }
        if (n >= 3 && (unsigned char)p[0] == 0xE2 && (unsigned char)p[1] == 0x80 && (unsigned char)p[2] == 0xA5) {
            char *e = malloc(192);
            snprintf(e, 192, "PermissionError: write slot targets an engine-reserved key: \"%s\"; help: keys starting with %s belong to the VM and programs cannot write them", key, RUNTIME_MEMBER_SEP);
            return e;
        }
        p = slash ? slash + 1 : p + n;
    }
    static const char *roots[] = { LIB_ROOT, SYS_ROOT, DEV_ROOT, DONE_ROOT, VTHREAD_ROOT, NULL };
    for (int i = 0; roots[i]; i++) {
        if (strcmp(key, roots[i]) == 0) {
            char *e = malloc(192);
            snprintf(e, 192, "PermissionError: write slot targets domain root %s: \"%s\"; help: the domain belongs to the VM, and a root is a directory besides", roots[i], key);
            return e;
        }
    }
    static const char *prot[] = { LIB_ROOT, SYS_ROOT, DEV_ROOT, DONE_ROOT, NULL };
    for (int i = 0; prot[i]; i++) {
        size_t pl = strlen(prot[i]);
        if (strncmp(key, prot[i], pl) == 0 && key[pl] == '/') {
            char *e = malloc(220);
            snprintf(e, 220, "PermissionError: write slot targets protected domain %s: \"%s\"; help: %s belongs to the VM; write a slot in your own vthread or a user global key", prot[i], key, prot[i]);
            return e;
        }
    }
    if (strncmp(key, VTHREAD_ROOT "/", strlen(VTHREAD_ROOT) + 1) == 0) {
        kvlangStrbuf_t own; kvlangStrbufInit(&own);
        kvlangKeytreeVthread(vtid, &own);
        if (strcmp(key, own.p) == 0) {
            kvlangStrbufFree(&own);
            char *e = malloc(200);
            snprintf(e, 200, "IsADirectoryError: write slot targets this vthread root: \"%s\"; help: the vthread root is a directory, not a leaf", key);
            return e;
        }
        size_t ol = strlen(own.p);
        if (!(strncmp(key, own.p, ol) == 0 && key[ol] == '/')) {
            char *e = malloc(240);
            snprintf(e, 240, "PermissionError: write slot is outside this vthread's subtree (vtid=%s): \"%s\"; help: the whole /vthread domain belongs to the engine, a program may only write keys under %s/", vtid, key, own.p);
            kvlangStrbufFree(&own);
            return e;
        }
        const char *rest = key + ol + 1;
        if (rest[0] == '[') {
            const char *q = rest + 1;
            if (*q >= '0' && *q <= '9') {
                while (*q >= '0' && *q <= '9') q++;
                if (*q == ']' && (q[1] == 0 || (q[1] == '/' && q[2] == 0))) {
                    kvlangStrbufFree(&own);
                    char *e = malloc(220);
                    snprintf(e, 220, "IsADirectoryError: write slot targets a frame directory: \"%s\"; help: a frame is a directory and writing it as a leaf destroys the call frame", key);
                    return e;
                }
            }
        }
        kvlangStrbufFree(&own);
    }
    return NULL;
}

