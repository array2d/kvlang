// rwir_kv —— kvspace·* KV 树 rwir

#include "rwir_internal.h"

static bool is_int_kind(const char *k) {
    return kvlangXvalueIsIntKind(k) || kvlangXvalueIsUintKind(k);
}

static char *kvlangKvspaceKey(const kvlangXvalue_t *v) {
    if (kvlangXvalueIsCharKind(kvlangXvalueKind(v)))
        return kvlangXvalueValueString(v);
    if (is_int_kind(kvlangXvalueKind(v))) {
        char buf[32];
        snprintf(buf, sizeof buf, "%lld",
                 (long long)kvlangScalarI64(kvlangXvalueScalar(v)));
        return strdup(buf);
    }
    return strdup("");
}

static char *original_absolute_arg(kvlangFrame_t *f, int idx) {
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    if (!fr)
        return NULL;
    char key[KV_KEY_MAX];
    kvlangKeyFormat(key, sizeof key, "%s/%slib", fr, RUNTIME_MEMBER_SEP);
    kvlangXvalue_t lib;
    kvlangXvalueZero(&lib);
    kvlangKvspaceGetOne(f->kvspace, key, &lib);
    char *root = kvlangXvalueNone(&lib) ? NULL : kvlangXvalueValueString(&lib);
    kvlangXvalueFree(&lib);
    if (!root || root[0] != '/') {
        free(root);
        free(fr);
        return NULL;
    }
    int row = kvlangRwirExtractAddress(strrchr(f->pc, '/') + 1);
    kvlangKeyFormat(key, sizeof key, "%s/[%d,-%d]", root, row, idx + 1);
    kvlangXvalue_t source;
    kvlangXvalueZero(&source);
    kvlangKvspaceGetOne(f->kvspace, key, &source);
    char *name = kvlangXvalueKindIs(&source, KVSPACE_KIND_RWIR)
                     ? kvlangXvalueSlotName(&source)
                     : NULL;
    if (name && name[0] != '/') {
        free(name);
        name = NULL;
    }
    kvlangXvalueFree(&source);
    free(root);
    free(fr);
    return name;
}

static char *path_arg(kvlangFrame_t *f, int idx, const kvlangXvalue_t *in) {
    const char *name = f->instruction->reads[idx].name;
    if (name[0] == '/') {
        if (strncmp(name, "/vthread/", 9) != 0)
            return strdup(name);
        char *source = original_absolute_arg(f, idx);
        if (source)
            return source;
    }
    if (!kvlangXvalueNone(&in[idx])) {
        char *s = kvlangXvalueValueString(&in[idx]);
        if (s[0] == '/')
            return s;
        free(s);
    }
    return NULL;
}

/* Container members use physical key prefixes. */
static bool base_is_container(const kvlangXvalue_t *base) {
    if (kvlangXvalueNone(base))
        return true;
    const char *k = kvlangXvalueKind(base);
    return k[0] == '/' || kvlangKindIsMap(k);
}

/* 非容器 base 的统一报错（返回 NULL 供调用方判定失败）：带上实际 langtype 便于定位。 */
static char *member_not_container(const kvlangXvalue_t *v, char *err,
                                  size_t errsz) {
    char lt[256];
    xh_langtype_copy_of(v, lt, sizeof lt);
    snprintf(err, errsz,
             "member access requires a container (struct/map), got %s; "
             "compact arrays use [] indexing",
             lt);
    return NULL;
}

/* 成员访问的 base 解析（spec [[成员访问]] 的「按指针优先、按名回退」+ 形态判定）：
 *   Ptr → 取其目标路径（目标须是容器）；容器/None → 取 base 的写槽路径；路径串（char/）→ 其值即父路径；
 *   其余（compact 数组、标量）不是容器 → 报错，绝不把 body 字节冒充路径串拼键。 */
static char *member_path(kvlangFrame_t *f, const kvlangXvalue_t *in, int n,
                         char *err, size_t errsz) {
    const kvlangXvalue_t *base = &in[0];
    char *bp = NULL;
    err[0] = 0;
    if (kvlangXvalueIsPtr(base)) {
        /* 数据 Ptr（&x）作成员 base：直接取其目标路径，逐段下钻。Ptr 的 head 即目标 kindexpr，
         * 故容器判定即对目标判定——`p·val`（目标 struct）通过，`p·[0]`（目标 compact 数组）报错。 */
        if (!base_is_container(base))
            return member_not_container(base, err, errsz);
        bp = kvlangXvaluePtrTarget(base);
    } else if (base_is_container(base)) {
        char *fr = kvlangKeytreeFrameRoot(f->pc);
        bp = kvlangBuiltinResolveWriteSlot(f->kvspace, fr,
                                           f->instruction->reads[0].name);
        free(fr);
    } else if (kvlangXvalueIsCharKind(kvlangXvalueKind(base))) {
        bp = kvlangXvalueValueString(base);
    } else {
        return member_not_container(base, err, errsz);
    }
    /* 成员链：base 之后逐段拼 key（变参），每段可为静态字面量或动态键（运行时值）。 */
    for (int i = 1; i < n; i++) {
        char *kk = kvlangKvspaceKey(&in[i]);
        char *next = kvlangKeytreeMember(bp, kk);
        free(kk);
        free(bp);
        bp = next;
    }
    return bp;
}

int kvlangBuiltinKvspaceGet(kvlangFrame_t *f) {
    kvlangXvalue_t in[MAX_PARAMS];
    int n = kvlangBuiltinReadInputs(f, in, MAX_PARAMS);
    char merr[512];
    char *key = f->instruction->read_count >= 2
                    ? member_path(f, in, n, merr, sizeof merr)
                    : (n >= 1 ? path_arg(f, 0, in) : NULL);
    if (!key) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetError(f, "TypeError: kvspace·get: %s",
                                     merr[0] ? merr : "requires a path");
    }
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    kvlangKvspaceGetOne(f->kvspace, key, &v);
    int rc = kvlangBuiltinWriteResult(f, &v);
    kvlangXvalueFree(&v);
    free(key);
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}

/* 成员写（memitem）的前置条件：memhead（容器值 base）必须已存在。不存在即拒绝写入——
 * 容器由用户显式建（`m:T = {}`），不隐式补：隐式补会掩盖「写了一个不存在的容器」这一
 * 错误，与「禁止写入未声明的 key」一致（对齐 strict 语义，见 [[容器]]）。
 * `/lib` 下跳过：那是 layout/runtime 的结构域，`·` 是包·函数命名分隔符而非成员写。
 * 返回 0 通过；-1 拒绝（调用方置 TypeError）。 */
int kvlangBuiltinCheckMemoryHead(kvspace_t *kv, const char *frame_root,
                                 const char *base) {
    if (!base || !base[0] || strncmp(base, "/lib", 4) == 0)
        return 0;
    char *path = kvlangBuiltinResolveWriteSlot(kv, frame_root, base);
    kvlangXvalue_t cur;
    kvlangXvalueZero(&cur);
    kvlangKvspaceGetOne(kv, path, &cur);
    int ok = !kvlangXvalueNone(&cur);
    kvlangXvalueFree(&cur);
    free(path);
    return ok ? 0 : -1;
}

int kvlangBuiltinKvspaceSet(kvlangFrame_t *f) {
    kvlangXvalue_t in[MAX_PARAMS];
    int n = kvlangBuiltinReadInputs(f, in, MAX_PARAMS);
    char *key;
    kvlangXvalue_t *val;
    char merr[512];
    if (f->instruction->read_count >= 3) {
        const char *base = f->instruction->reads[0].name;
        char *fr = kvlangKeytreeFrameRoot(f->pc);
        int ok = kvlangBuiltinCheckMemoryHead(f->kvspace, fr, base);
        free(fr);
        if (ok != 0) {
            kvlangBuiltinFreeInputs(in, n);
            return kvlangBuiltinSetError(
                f,
                "TypeError: memhead %s does not exist — declare the container "
                "first (e.g. `%s:T = {}`)",
                base, base);
        }
        key = member_path(f, in, n - 1, merr, sizeof merr);
        val = &in[n - 1];
    } else {
        key = n >= 1 ? path_arg(f, 0, in) : NULL;
        merr[0] = 0;
        val = &in[1];
    }
    if (!key || (f->instruction->read_count < 3 && n < 2)) {
        free(key);
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetError(f, "TypeError: kvspace·set: %s",
                                     merr[0] ? merr
                                             : "requires path and value");
    }
    kvlangKvPair_t p = {key, *val};
    char err[256];
    int rc = kvlangKvspaceSet(f->kvspace, &p, 1, err, sizeof err);
    free(key);
    kvlangBuiltinFreeInputs(in, n);
    if (rc != 0)
        return kvlangBuiltinSetError(f, "%s", err);
    kvlangBuiltinNextPc(f);
    return 0;
}

static int run_path_op(kvlangFrame_t *f, const char *name,
                       int (*op)(kvspace_t *, const char *, char *, uint32_t)) {
    kvlangXvalue_t in[1];
    int n = kvlangBuiltinReadInputs(f, in, 1);
    char *key = n >= 1 ? path_arg(f, 0, in) : NULL;
    if (!key) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetError(f, "TypeError: %s requires 1 path arg",
                                     name);
    }
    char err[256];
    int rc = op(f->kvspace, key, err, sizeof err);
    free(key);
    kvlangBuiltinFreeInputs(in, n);
    if (rc != 0)
        return kvlangBuiltinSetError(f, "%s", err);
    kvlangBuiltinNextPc(f);
    return 0;
}

int kvlangBuiltinKvspaceDelete(kvlangFrame_t *f) {
    int rc = run_path_op(f, "kvspace·del", kvlangKvspaceDelete);
    if (f->addr_epoch)
        (*f->addr_epoch)++; /* 被删 key 的地址作废，帧层 keyoff 全部重绑 */
    return rc;
}

int kvlangBuiltinKvspaceDeleteTree(kvlangFrame_t *f) {
    int rc = run_path_op(f, "kvspace·deltree", kvlangKvspaceDeleteTree);
    if (f->addr_epoch)
        (*f->addr_epoch)++;
    return rc;
}

/* 绝对路径 / 路径字符串直取，否则裸标识符解析为本帧槽位 key（对齐 kvspace·list 的裸变量处理）。 */
static char *resolve_path_arg(kvlangFrame_t *f, int idx,
                              const kvlangXvalue_t *in) {
    char *p = path_arg(f, idx, in);
    if (p)
        return p;
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *base = kvlangBuiltinResolveWriteSlot(f->kvspace, fr,
                                               f->instruction->reads[idx].name);
    free(fr);
    return base;
}

static int run_two_path_op(kvlangFrame_t *f, const char *name,
                           int (*op)(kvspace_t *, const char *, const char *,
                                     char *, uint32_t)) {
    kvlangXvalue_t in[2];
    int n = kvlangBuiltinReadInputs(f, in, 2);
    char *src = n >= 1 ? resolve_path_arg(f, 0, in) : NULL;
    char *dst = n >= 2 ? resolve_path_arg(f, 1, in) : NULL;
    if (!src || !dst) {
        free(src);
        free(dst);
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetError(
            f, "TypeError: %s requires src,dst path args", name);
    }
    char err[256];
    int rc = op(f->kvspace, src, dst, err, sizeof err);
    free(src);
    free(dst);
    kvlangBuiltinFreeInputs(in, n);
    if (rc != 0)
        return kvlangBuiltinSetError(f, "%s", err);
    kvlangBuiltinNextPc(f);
    return 0;
}

int kvlangBuiltinKvspaceCopy(kvlangFrame_t *f) {
    return run_two_path_op(f, "kvspace·cp", kvlangKvspaceCopy);
}

int kvlangBuiltinKvspaceCopyTree(kvlangFrame_t *f) {
    return run_two_path_op(f, "kvspace·cpdir", kvlangKvspaceCopyTree);
}

int kvlangBuiltinKvspaceCopyList(kvlangFrame_t *f) {
    return run_two_path_op(f, "kvspace·cplist", kvlangKvspaceCopyList);
}

int kvlangBuiltinKvlangAbs(kvlangFrame_t *f) {
    kvlangXvalue_t in[MAX_PARAMS];
    int n = kvlangBuiltinReadInputs(f, in, MAX_PARAMS);
    char *p = NULL;
    char merr[512];
    if (n >= 2)
        p = member_path(f, in, n, merr, sizeof merr);
    else if (n >= 1)
        p = resolve_path_arg(f, 0, in);
    if (!p) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetError(f, "TypeError: kvlang·abs: %s",
                                     n >= 2 && merr[0] ? merr
                                                       : "requires a key");
    }
    /* 产出 Ptr（ref=1）：langtype = 目标 kindexpr、body = 目标绝对路径。
     * &x ≡ kvlang·abs(x)：取址返回指向 x 所在节点的软链接（单跳同型），不再产 char 路径串。 */
    kvlangXvalue_t tg;
    kvlangXvalueZero(&tg);
    kvlangKvspaceGetOne(f->kvspace, p, &tg);
    char lt[256] = {0};
    if (!kvlangXvalueNone(&tg))
        xh_langtype_copy_of(&tg, lt, sizeof lt);
    kvlangXvalue_t r;
    kvlangXvalueNewPtr(&r, lt, p);
    int rc = kvlangBuiltinWriteResult(f, &r);
    kvlangXvalueFree(&r);
    kvlangXvalueFree(&tg);
    free(p);
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinKvspaceList(kvlangFrame_t *f) {
    if (f->instruction->write_count == 0)
        return kvlangBuiltinSetError(
            f, "TypeError: kvspace·list requires a write param");
    kvlangXvalue_t in[1];
    int n = kvlangBuiltinReadInputs(f, in, 1);
    char *key = n >= 1 ? path_arg(f, 0, in) : NULL;
    if (!key && f->instruction->read_count >= 1) {
        const char *name = f->instruction->reads[0].name;
        char *fr = kvlangKeytreeFrameRoot(f->pc);
        char *base = kvlangBuiltinResolveWriteSlot(f->kvspace, fr, name);
        free(fr);
        key = kvlangKeytreeMember(base, "");
        free(base);
    }
    if (!key) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetError(
            f, "TypeError: kvspace·list requires 1 path arg");
    }
    char **names = NULL;
    int count = 0;
    kvlangKvspaceList(f->kvspace, key, false, false, &names, &count);
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *dst = kvlangBuiltinResolveWriteSlot(f->kvspace, fr,
                                              f->instruction->writes[0].name);
    free(fr);
    /* Members are physical keys under dst·. */
    char err[256];
    kvlangXvalue_t mark;
    kvlangBuiltinMapMarker(&mark, "[int64]" MEMBER_SEP "[]char/utf8");
    kvlangKvPair_t p0 = {dst, mark};
    kvlangKvspaceSet(f->kvspace, &p0, 1, err, sizeof err);
    kvlangXvalueFree(&mark);
    for (int i = 0; i < count; i++) {
        int64_t c[1] = {i};
        char *k = kvlangBuiltinScatterKey(dst, c, 1);
        kvlangXvalue_t e;
        kvlangXvalueNewCharUtf8(&e, names[i]);
        kvlangKvPair_t p = {k, e};
        kvlangKvspaceSet(f->kvspace, &p, 1, err, sizeof err);
        kvlangXvalueFree(&e);
        free(k);
        free(names[i]);
    }
    for (int i = count;; i++) {
        int64_t c[1] = {i};
        char *k = kvlangBuiltinScatterKey(dst, c, 1);
        kvlangXvalue_t v;
        kvlangXvalueZero(&v);
        kvlangKvspaceGetOne(f->kvspace, k, &v);
        bool none = kvlangXvalueNone(&v);
        kvlangXvalueFree(&v);
        if (none) {
            free(k);
            break;
        }
        kvlangKvspaceDelete(f->kvspace, k, err, sizeof err);
        free(k);
    }
    free(names);
    free(dst);
    free(key);
    kvlangBuiltinNextPc(f);
    kvlangBuiltinFreeInputs(in, n);
    return 0;
}

/* 解析 obj/map 成员目录 key（<frame>/<name>.），供 kvspace·list/listlen/listn 共用。 */
static char *list_dir_key(kvlangFrame_t *f, kvlangXvalue_t *in, int n) {
    char *key = n >= 1 ? path_arg(f, 0, in) : NULL;
    if (!key && f->instruction->read_count >= 1) {
        const char *name = f->instruction->reads[0].name;
        char *fr = kvlangKeytreeFrameRoot(f->pc);
        char *base = kvlangBuiltinResolveWriteSlot(f->kvspace, fr, name);
        free(fr);
        key = kvlangKeytreeMember(base, "");
        free(base);
    }
    return key;
}

int kvlangBuiltinKvspaceListLength(kvlangFrame_t *f) {
    kvlangXvalue_t in[1];
    int n = kvlangBuiltinReadInputs(f, in, 1);
    char *key = list_dir_key(f, in, n);
    int64_t count = 0;
    if (key) {
        char **names = NULL;
        int cnt = 0;
        kvlangKvspaceList(f->kvspace, key, false, false, &names, &cnt);
        for (int i = 0; i < cnt; i++)
            free(names[i]);
        free(names);
        count = cnt;
        free(key);
    }
    kvlangXvalue_t r;
    kvlangXvalueNewInt64(&r, count);
    int rc = kvlangBuiltinWriteResult(f, &r);
    kvlangXvalueFree(&r);
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinKvspaceListNth(kvlangFrame_t *f) {
    kvlangXvalue_t in[2];
    int n = kvlangBuiltinReadInputs(f, in, 2);
    char *key = list_dir_key(f, in, n);
    int idx = n >= 2 ? (int)kvlangScalarI64(kvlangXvalueScalar(&in[1])) : -1;
    kvlangXvalue_t r;
    kvlangXvalueZero(&r);
    if (key && idx >= 0) {
        char **names = NULL;
        int cnt = 0;
        kvlangKvspaceList(f->kvspace, key, false, false, &names, &cnt);
        if (idx < cnt) {
            kvlangXvalueNewCharUtf8(&r, names[idx]);
        }
        for (int i = 0; i < cnt; i++)
            free(names[i]);
        free(names);
        free(key);
    }
    int rc = kvlangBuiltinWriteResult(f, &r);
    kvlangXvalueFree(&r);
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinKvspaceMakeIndex(kvlangFrame_t *f) {
    kvlangXvalue_t in[2];
    int n = kvlangBuiltinReadInputs(f, in, 2);
    char *key = n >= 1 ? path_arg(f, 0, in) : NULL;
    if (!key) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetError(
            f, "TypeError: kvspace·mkindex requires a path");
    }
    uint32_t capacity =
        n >= 2 ? (uint32_t)kvlangScalarI64(kvlangXvalueScalar(&in[1])) : 0;
    char err[256];
    int rc = kvlangKvspaceMakeIndex(f->kvspace, key, capacity, err, sizeof err);
    free(key);
    kvlangBuiltinFreeInputs(in, n);
    if (rc != 0)
        return kvlangBuiltinSetError(f, "%s", err);
    kvlangBuiltinNextPc(f);
    return 0;
}

int kvlangBuiltinKvspaceExtensionIndex(kvlangFrame_t *f) {
    kvlangXvalue_t in[2];
    int n = kvlangBuiltinReadInputs(f, in, 2);
    char *key = n >= 1 ? path_arg(f, 0, in) : NULL;
    char *ext = n >= 2 ? kvlangXvalueValueString(&in[1]) : NULL;
    if (!key || !ext) {
        free(key);
        free(ext);
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetError(
            f, "TypeError: kvspace·extindex requires path and ext path");
    }
    char err[256];
    int rc = kvlangKvspaceExtensionIndex(f->kvspace, key, ext, err, sizeof err);
    free(key);
    free(ext);
    kvlangBuiltinFreeInputs(in, n);
    if (rc != 0)
        return kvlangBuiltinSetError(f, "%s", err);
    kvlangBuiltinNextPc(f);
    return 0;
}

int kvlangBuiltinKvspaceRemoveIndexExtension(kvlangFrame_t *f) {
    return run_path_op(f, "kvspace·rmindexext",
                       kvlangKvspaceDeleteExtensionIndex);
}

int kvlangBuiltinKvspaceWatch(kvlangFrame_t *f) {
    kvlangXvalue_t in[2];
    int n = kvlangBuiltinReadInputs(f, in, 2);
    char *key = n >= 1 ? path_arg(f, 0, in) : NULL;
    if (!key || n < 2) {
        free(key);
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetError(
            f, "TypeError: kvspace·watch requires key and target");
    }
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    kvlangKvspaceWatch(f->kvspace, key, &in[1], 1000000, &v);
    int rc = kvlangBuiltinWriteResult(f, &v);
    kvlangXvalueFree(&v);
    free(key);
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}
