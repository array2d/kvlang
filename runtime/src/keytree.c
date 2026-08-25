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

char *kvlangKeytreeFrameRoot(const char *pc) {
    const char *last = NULL;
    for (const char *p = pc; (p = strstr(p, "/[")) != NULL; p += 2) last = p;
    if (!last) return NULL;
    size_t n = (size_t)(last - pc);
    char *r = malloc(n + 1);
    memcpy(r, pc, n); r[n] = 0;
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
char *kvlangKeytreeScopeEntryPc(const char *root) { return trim_right_join(root, "/[0,0]"); }

char *kvlangKeytreeParentFrame(const char *root) {
    size_t n = strlen(root);
    while (n > 0 && root[n - 1] == '/') n--;
    if (n == 0) return strdup("");
    size_t last = (size_t)-1;
    for (size_t i = 0; i < n; i++) if (root[i] == '/') last = i;
    if (last == (size_t)-1) return strdup("");
    size_t cut = last + 1;
    char *r = malloc(cut + 1);
    memcpy(r, root, cut); r[cut] = 0;
    return r;
}

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

void kvlangKeytreeVthreadSlot(const char *vtid, const char *frame, int i, int j, kvlangStrbuf_t *out) {
    kvlangStrbufClear(out);
    kvlangStrbufPuts(out, VTHREAD_ROOT PATH_SEP);
    kvlangStrbufPuts(out, vtid);
    if (frame && frame[0]) { kvlangStrbufPutc(out, '/'); kvlangStrbufPuts(out, frame); }
    kvlangStrbufPrintf(out, "/[%d,%d]", i, j);
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

bool kvlangKeytreeIsEntryPc(const char *pc) {
    const char *slash = strrchr(pc, '/');
    return slash && strcmp(slash, "/[1,0]") == 0;
}
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
            snprintf(e, 200, "IsADirectoryError: write slot targets this vthread's frame root: \"%s\"; help: a frame root is a directory and writing it as a leaf destroys the call frame", key);
            return e;
        }
        size_t ol = strlen(own.p);
        if (!(strncmp(key, own.p, ol) == 0 && key[ol] == '/')) {
            char *e = malloc(240);
            snprintf(e, 240, "PermissionError: write slot is outside this vthread's subtree (vtid=%s): \"%s\"; help: the whole /vthread domain belongs to the engine, a program may only write keys under %s/", vtid, key, own.p);
            kvlangStrbufFree(&own);
            return e;
        }
        kvlangStrbufFree(&own);
    }
    return NULL;
}

