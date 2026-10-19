#include "runtime_internal.h"

const char *kvlangKeytreeVtidFromPc(const char *pc, char *out, size_t cap) {
    const char *pfx = VTHREAD_ROOT PATH_SEP;
    size_t pl = strlen(pfx);
    if (strncmp(pc, pfx, pl) != 0) {
        if (cap) out[0] = 0;
        return "";
    }
    const char *rest = pc + pl;
    const char *slash = strchr(rest, '/');
    size_t n = slash ? (size_t)(slash - rest) : strlen(rest);
    if (n + 1 > cap)
        kvlangDie("vtid exceeds %zu bytes", cap);
    memcpy(out, rest, n);
    out[n] = 0;
    return out;
}

char *kvlangKeytreeStack(const char *root) {
    size_t n = strlen(root);
    while (n > 0 && root[n - 1] == '/') n--;
    char *r = malloc(n + 2);
    memcpy(r, root, n); r[n] = '/'; r[n + 1] = 0;
    return r;
}

/* 栈缓冲版：把 root（去尾 /）拷进 buf 并补 '/'，返回写入长度（含尾 /）；cap 不足返回 0。
 * 读参热路径复用调用方缓冲，免每操作一次 malloc。 */
size_t kvlangKeytreeStackBuf(const char *root, char *buf, size_t cap) {
    size_t n = strlen(root);
    while (n > 0 && root[n - 1] == '/') n--;
    if (n + 2 > cap) return 0;
    memcpy(buf, root, n);
    buf[n] = '/';
    buf[n + 1] = 0;
    return n + 1;
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

/* 栈缓冲版：帧根（末个 "/[" 之前）拷进 buf 并 NUL 终止，返回长度（不含 NUL）。
 * 无 "/[" 或 cap 不足返回 0（buf 内容不可用），调用方回退 kvlangKeytreeFrameRoot。 */
size_t kvlangKeytreeFrameRootBuf(const char *pc, char *buf, size_t cap) {
    const char *last = NULL;
    for (const char *p = pc; (p = strstr(p, "/[")) != NULL; p += 2) last = p;
    if (!last) return 0;
    size_t n = (size_t)(last - pc);
    if (n + 1 > cap || n == 0) return 0;
    memcpy(buf, pc, n);
    buf[n] = 0;
    return n;
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

char *kvlangKeytreeFrameAt(const char *vtid, int depth) {
    if (!vtid || !vtid[0]) kvlangDie("FrameAt: empty vtid");
    if (depth < 1) kvlangDie("FrameAt: depth %d < 1", depth);
    return kvlangAstr("%s/%s/[%d]", VTHREAD_ROOT, vtid, depth);
}

int kvlangKeytreeFrameNum(const char *path) {
    /* 免分配：/vthread/<vtid>/[d][/...] —— 直接定位 vtid 后的帧坐标，不构造前缀串。 */
    const size_t rl = sizeof(VTHREAD_ROOT) - 1;
    if (strncmp(path, VTHREAD_ROOT PATH_SEP, rl + 1) != 0)
        kvlangDie("FrameNum: not a vthread path: %s", path);
    const char *vtid = path + rl + 1;
    const char *slash = strchr(vtid, '/');
    if (!slash || slash == vtid)
        kvlangDie("FrameNum: not a vthread path: %s", path);
    const char *rest = slash + 1;
    if (rest[0] != '[') kvlangDie("FrameNum: no frame coord in %s", path);
    const char *end = strchr(rest, ']');
    if (!end) kvlangDie("FrameNum: unterminated frame coord in %s", path);
    size_t clen = (size_t)(end - (rest + 1));
    if (clen == 0 || memchr(rest + 1, ',', clen)) {
        kvlangDie("FrameNum: expected [d] frame coord in %s", path);
    }
    char buf[32];
    if (clen >= sizeof buf) kvlangDie("FrameNum: frame coord too long in %s", path);
    memcpy(buf, rest + 1, clen); buf[clen] = 0;
    char *ep = NULL;
    long n = strtol(buf, &ep, 10);
    if (ep == buf || *ep != 0 || n < 1) kvlangDie("FrameNum: invalid frame number %s in %s", buf, path);
    const char *after = end + 1;
    if (after[0] == 0 || (after[0] == '/' && after[1] == 0)) {
        /* frame root */
    } else if (after[0] == '/' && after[1] == '[') {
        const char *tail = after + 1;
        if (strchr(tail, '/')) kvlangDie("FrameNum: nested path after [d]: %s", path);
        if (!strchr(tail, ',')) kvlangDie("FrameNum: expected [irseq,j] after [d] in %s", path);
    } else {
        kvlangDie("FrameNum: expected /[irseq,j] after [d]: %s", path);
    }
    return (int)n;
}

char *kvlangKeytreeIrseqPc(const char *frame_root, int irseq) {
    if (irseq < 0) kvlangDie("IrseqPC: irseq %d < 0", irseq);
    size_t n = strlen(frame_root);
    while (n > 0 && frame_root[n - 1] == '/') n--;
    return kvlangAstr("%.*s/[%d,0]", (int)n, frame_root, irseq);
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
    if (pkg && pkg[0])
        return kvlangAstr("%s%s%s%s", LIB_ROOT PATH_SEP, pkg, MEMBER_SEP, name);
    return kvlangAstr("%s%s", LIB_ROOT PATH_SEP, name);
}

char *kvlangKeytreeRwir(const char *opcode) {
    return kvlangAstr("%s%s", LIB_ROOT PATH_SEP, opcode);
}

void kvlangKeytreeVthread(const char *vtid, char *out, size_t cap) {
    kvlangKeyFmt(out, cap, "%s%s", VTHREAD_ROOT PATH_SEP, vtid);
}

void kvlangKeytreeVthreadSlot(const char *vtid, const char *frame, int i, int j,
                              char *out, size_t cap) {
    if (frame && frame[0])
        kvlangKeyFmt(out, cap, "%s%s/%s/[%d,%d]", VTHREAD_ROOT PATH_SEP, vtid,
                     frame, i, j);
    else
        kvlangKeyFmt(out, cap, "%s%s/[%d,%d]", VTHREAD_ROOT PATH_SEP, vtid, i, j);
}

static void kvlangVthreadMember(const char *vtid, const char *seg, char *out,
                                size_t cap) {
    kvlangKeyFmt(out, cap, "%s%s%s%s%s", VTHREAD_ROOT PATH_SEP, vtid, "/",
                 RUNTIME_MEMBER_SEP, seg);
}

void kvlangKeytreeVthreadPc(const char *vtid, char *out, size_t cap) {
    kvlangVthreadMember(vtid, SEG_PC, out, cap);
}
void kvlangKeytreeVthreadStatus(const char *vtid, char *out, size_t cap) {
    kvlangVthreadMember(vtid, SEG_STATUS, out, cap);
}
void kvlangKeytreeVthreadDebugger(const char *vtid, char *out, size_t cap) {
    kvlangVthreadMember(vtid, "debugger", out, cap);
}

void kvlangKeytreeVthreadStatusMsg(const char *vtid, const char *status,
                                   char *out, size_t cap) {
    kvlangKeyFmt(out, cap, "%s%s%s%s%s%s%s", VTHREAD_ROOT PATH_SEP, vtid, "/",
                 RUNTIME_MEMBER_SEP, status, "/", SEG_MSG);
}

static void frame_member(const char *root, const char *seg, char *out,
                         size_t cap) {
    size_t n = strlen(root);
    while (n > 0 && root[n - 1] == '/') n--;
    kvlangKeyFmt(out, cap, "%.*s%s%s", (int)n, root, RUNTIME_MEMBER_SEP, seg);
}

void kvlangKeytreeFrameCallpc(const char *root, char *out, size_t cap) {
    frame_member(root, SEG_CALLPC, out, cap);
}
void kvlangKeytreeFrameReturnpc(const char *root, char *out, size_t cap) {
    frame_member(root, SEG_RETURNPC, out, cap);
}
void kvlangKeytreeFrameRo(const char *root, char *out, size_t cap) {
    frame_member(root, SEG_RO, out, cap);
}

bool kvlangKeytreeIsEntryPc(const char *pc) {
    const char *slash = strrchr(pc, '/');
    return slash && strcmp(slash, "/[1,0]") == 0;
}

const char *kvlangKeytreeCanonOp(const char *opcode) {
    const char *p = opcode ? opcode : "";
    size_t pl = strlen(LIB_ROOT PATH_SEP);
    return strncmp(p, LIB_ROOT PATH_SEP, pl) == 0 ? p + pl : p;
}

bool kvlangKeytreeValidSegment(const char *s) {
    if (!s || !s[0])
        return false;
    if (strcmp(s, ".") == 0 || strcmp(s, "..") == 0)
        return false;
    if (strcmp(s, MEMBER_SEP) == 0)
        return false;
    return strchr(s, '/') == NULL;
}

static char *join3(const char *a, const char *b, const char *c) {
    if (b && b[0] && c && c[0])
        return kvlangAstr("%s/%s/%s", a, b, c);
    if (b && b[0])
        return kvlangAstr("%s/%s", a, b);
    if (c && c[0])
        return kvlangAstr("%s/%s", a, c);
    return strdup(a);
}

char *kvlangKeytreeSysRwirBackendRoot(void) {
    return strdup(SYS_ROOT PATH_SEP SEG_RWIR_BACKEND);
}

char *kvlangKeytreeSysRwirBackend(const char *name) {
    return join3(SYS_ROOT PATH_SEP SEG_RWIR_BACKEND, name, NULL);
}

char *kvlangKeytreeSysRwirBackendOp(const char *name, const char *opcode) {
    return kvlangAstr("%s/%s/%s/%s", SYS_ROOT PATH_SEP SEG_RWIR_BACKEND, name,
                      SEG_OP, opcode);
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
    return kvlangAstr("%s/%s/%s%s", SYS_ROOT PATH_SEP SEG_TASK, task_id,
                      RUNTIME_MEMBER_SEP, field);
}

char *kvlangKeytreeDoneRwir(const char *task_id) {
    return join3(DONE_ROOT PATH_SEP SEG_RWIR, task_id, NULL);
}

char *kvlangKeytreeLibSig(const char *opcode) {
    return kvlangAstr("%s%s/[0,0]", LIB_ROOT PATH_SEP, opcode);
}

static char *write_err(size_t n, const char *fmt, ...) {
    char *e = malloc(n);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e, n, fmt, ap);
    va_end(ap);
    return e;
}

char *kvlangKeytreeCheckWriteKey(const char *vtid, const char *key) {
    if (!key || key[0] != '/')
        return write_err(160,
                         "ValueError: write slot is not an absolute path: \"%s\"; help: a write slot must resolve to a canonical path starting with /",
                         key ? key : "");
    const char *p = key + 1;
    while (*p) {
        const char *slash = strchr(p, '/');
        size_t n = slash ? (size_t)(slash - p) : strlen(p);
        if (n == 0 || (n == 1 && p[0] == '.') ||
            (n == 2 && p[0] == '.' && p[1] == '.'))
            return write_err(160,
                             "ValueError: write slot is not a canonical path: \"%s\"; help: a path may not contain an empty segment, . or ..",
                             key);
        if (n >= 3 && (unsigned char)p[0] == 0xE2 && (unsigned char)p[1] == 0x80 &&
            (unsigned char)p[2] == 0xA5)
            return write_err(192,
                             "PermissionError: write slot targets an engine-reserved key: \"%s\"; help: keys starting with %s belong to the VM and programs cannot write them",
                             key, RUNTIME_MEMBER_SEP);
        p = slash ? slash + 1 : p + n;
    }
    static const char *roots[] = {LIB_ROOT, SYS_ROOT, DEV_ROOT, DONE_ROOT, VTHREAD_ROOT, NULL};
    for (int i = 0; roots[i]; i++) {
        if (strcmp(key, roots[i]) == 0)
            return write_err(192,
                             "PermissionError: write slot targets domain root %s: \"%s\"; help: the domain belongs to the VM, and a root is a directory besides",
                             roots[i], key);
    }
    static const char *prot[] = {LIB_ROOT, SYS_ROOT, DEV_ROOT, DONE_ROOT, NULL};
    for (int i = 0; prot[i]; i++) {
        size_t pl = strlen(prot[i]);
        if (strncmp(key, prot[i], pl) == 0 && key[pl] == '/')
            return write_err(220,
                             "PermissionError: write slot targets protected domain %s: \"%s\"; help: %s belongs to the VM; write a slot in your own vthread or a user global key",
                             prot[i], key, prot[i]);
    }
    size_t vr = strlen(VTHREAD_ROOT) + 1;
    if (strncmp(key, VTHREAD_ROOT "/", vr) == 0) {
        char own[KV_KEY_MAX];
        kvlangKeytreeVthread(vtid, own, sizeof own);
        if (strcmp(key, own) == 0) {
            return write_err(200,
                             "IsADirectoryError: write slot targets this vthread's frame root: \"%s\"; help: a frame root is a directory and writing it as a leaf destroys the call frame",
                             key);
        }
        size_t ol = strlen(own);
        if (!(strncmp(key, own, ol) == 0 && key[ol] == '/')) {
            return write_err(240,
                             "PermissionError: write slot is outside this vthread's subtree (vtid=%s): \"%s\"; help: the whole /vthread domain belongs to the engine, a program may only write keys under %s/",
                             vtid, key, own);
        }
    }
    return NULL;
}
