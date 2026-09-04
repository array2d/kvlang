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

static void keytree_die(const char *fmt, ...) {
    fputs("panic: ", stderr);
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    abort();
}

char *kvlangKeytreeFrameAt(const char *vtid, int depth) {
    if (!vtid || !vtid[0]) keytree_die("FrameAt: empty vtid");
    if (depth < 1) keytree_die("FrameAt: depth %d < 1", depth);
    kvlangStrbuf_t b; kvlangStrbufInit(&b);
    kvlangStrbufPrintf(&b, "%s/%s/[%d]", VTHREAD_ROOT, vtid, depth);
    return kvlangStrbufDetach(&b);
}

int kvlangKeytreeFrameNum(const char *path) {
    kvlangStrbuf_t vtid_b; kvlangStrbufInit(&vtid_b);
    const char *vtid = kvlangKeytreeVtidFromPc(path, &vtid_b);
    if (!vtid[0]) keytree_die("FrameNum: not a vthread path: %s", path);
    kvlangStrbuf_t pfx; kvlangStrbufInit(&pfx);
    kvlangKeytreeVthread(vtid, &pfx);
    kvlangStrbufPutc(&pfx, '/');
    if (strncmp(path, pfx.p, pfx.len) != 0) keytree_die("FrameNum: not a vthread path: %s", path);
    const char *rest = path + pfx.len;
    if (rest[0] != '[') keytree_die("FrameNum: no frame coord in %s", path);
    const char *end = strchr(rest, ']');
    if (!end) keytree_die("FrameNum: unterminated frame coord in %s", path);
    size_t clen = (size_t)(end - (rest + 1));
    if (clen == 0 || memchr(rest + 1, ',', clen)) {
        keytree_die("FrameNum: expected [d] frame coord in %s", path);
    }
    char buf[32];
    if (clen >= sizeof buf) keytree_die("FrameNum: frame coord too long in %s", path);
    memcpy(buf, rest + 1, clen); buf[clen] = 0;
    char *ep = NULL;
    long n = strtol(buf, &ep, 10);
    if (ep == buf || *ep != 0 || n < 1) keytree_die("FrameNum: invalid frame number %s in %s", buf, path);
    const char *after = end + 1;
    if (after[0] == 0 || (after[0] == '/' && after[1] == 0)) {
        /* frame root */
    } else if (after[0] == '/' && after[1] == '[') {
        const char *tail = after + 1;
        if (strchr(tail, '/')) keytree_die("FrameNum: nested path after [d]: %s", path);
        if (!strchr(tail, ',')) keytree_die("FrameNum: expected [irseq,j] after [d] in %s", path);
    } else {
        keytree_die("FrameNum: expected /[irseq,j] after [d] in %s", path);
    }
    kvlangStrbufFree(&vtid_b); kvlangStrbufFree(&pfx);
    return (int)n;
}

char *kvlangKeytreeIrseqPc(const char *frame_root, int irseq) {
    if (irseq < 0) keytree_die("IrseqPC: irseq %d < 0", irseq);
    kvlangStrbuf_t b; kvlangStrbufInit(&b);
    size_t n = strlen(frame_root);
    while (n > 0 && frame_root[n - 1] == '/') n--;
    kvlangStrbufPutn(&b, frame_root, n);
    kvlangStrbufPrintf(&b, "/[%d,0]", irseq);
    return kvlangStrbufDetach(&b);
}

char *kvlangKeytreeMember(const char *base, const char *name) {
    size_t bl = strlen(base), nl = strlen(name);
    char *r = malloc(bl + nl + MEMBER_SEP_LEN + 1);
    memcpy(r, base, bl);
    memcpy(r + bl, MEMBER_SEP, MEMBER_SEP_LEN);
    memcpy(r + bl + MEMBER_SEP_LEN, name, nl);
    r[bl + MEMBER_SEP_LEN + nl] = 0;
    return r;
}

char *kvlangKeytreeLibFunc(const char *pkg, const char *name) {
    kvlangStrbuf_t b; kvlangStrbufInit(&b);
    kvlangStrbufPuts(&b, LIB_ROOT PATH_SEP);
    if (pkg && pkg[0]) { kvlangStrbufPuts(&b, pkg); kvlangStrbufPuts(&b, MEMBER_SEP); }
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
