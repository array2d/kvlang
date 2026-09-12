// builtin_kv —— kvspace·* KV 树 rwir

#include "builtin_internal.h"

static bool is_int_kind(const char *k) {
    return kvlangXvalueIsIntKind(k) || kvlangXvalueIsUintKind(k);
}

static char *kvlangKvKey(const kvlangXvalue_t *v) {
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

static char *path_arg(kvlangFrame_t *f, int idx, const kvlangXvalue_t *in) {
    const char *name = f->inst->reads[idx].name;
    if (name[0] == '/')
        return strdup(name);
    if (!kvlangXvalueNone(&in[idx])) {
        char *s = kvlangXvalueValueString(&in[idx]);
        if (s[0] == '/')
            return s;
        free(s);
    }
    return NULL;
}

/* 容器 base：stringkeymap/index/extindex 或 struct 实例（kind=structref，以 / 起头）。
 * 容器取其写槽路径拼成员 key；非容器把 base 值当 key 串。 */
static bool base_is_container(const kvlangXvalue_t *base) {
    if (kvlangXvalueNone(base))
        return true;
    const char *k = kvlangXvalueKind(base);
    return k[0] == '/' || kvlangKindIsMap(k) ||
           strcmp(k, KVSPACE_KIND_INDEX) == 0 ||
           strcmp(k, KVSPACE_KIND_EXT_INDEX) == 0;
}

static char *member_path(kvlangFrame_t *f, const kvlangXvalue_t *in, int n) {
    const kvlangXvalue_t *base = &in[0];
    char *bp = NULL;
    if (kvlangXvalueIsPtr(base)) {
        /* 数据 Ptr（&x）作成员 base：直接取其目标路径，逐段下钻。 */
        bp = kvlangXvaluePtrTarget(base);
    } else if (base_is_container(base)) {
        char *fr = kvlangKeytreeFrameRoot(f->pc);
        bp = kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->reads[0].name);
        free(fr);
    } else {
        bp = kvlangXvalueValueString(base);
    }
    /* 成员链：base 之后逐段拼 key（变参），每段可为静态字面量或动态键（运行时值）。 */
    for (int i = 1; i < n; i++) {
        char *kk = kvlangKvKey(&in[i]);
        char *next = kvlangKeytreeMember(bp, kk);
        free(kk);
        free(bp);
        bp = next;
    }
    return bp;
}

int kvlangCGet(kvlangFrame_t *f) {
    kvlangXvalue_t in[MAX_PARAMS];
    int n = kvlangBuiltinReadInputs(f, in, MAX_PARAMS);
    char *key = f->inst->nr >= 2 ? member_path(f, in, n)
                                 : (n >= 1 ? path_arg(f, 0, in) : NULL);
    if (!key) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetErr(f, "TypeError: kvspace·get requires a path");
    }
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    kvlangKvGetOne(f->kv, key, &v);
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
int kvlangBuiltinCheckMemhead(kvlangKv_t *kv, const char *frame_root,
                              const char *base) {
    if (!base || !base[0] || strncmp(base, "/lib", 4) == 0)
        return 0;
    char *path = kvlangBuiltinResolveWriteSlot(kv, frame_root, base);
    kvlangXvalue_t cur;
    kvlangXvalueZero(&cur);
    kvlangKvGetOne(kv, path, &cur);
    int ok = !kvlangXvalueNone(&cur);
    kvlangXvalueFree(&cur);
    free(path);
    return ok ? 0 : -1;
}

int kvlangCSet(kvlangFrame_t *f) {
    kvlangXvalue_t in[MAX_PARAMS];
    int n = kvlangBuiltinReadInputs(f, in, MAX_PARAMS);
    char *key;
    kvlangXvalue_t *val;
    if (f->inst->nr >= 3) {
        const char *base = f->inst->reads[0].name;
        char *fr = kvlangKeytreeFrameRoot(f->pc);
        int ok = kvlangBuiltinCheckMemhead(f->kv, fr, base);
        free(fr);
        if (ok != 0) {
            kvlangBuiltinFreeInputs(in, n);
            return kvlangBuiltinSetErr(
                f,
                "TypeError: memhead %s does not exist — declare the container "
                "first (e.g. `%s:T = {}`)",
                base, base);
        }
        key = member_path(f, in, n - 1);
        val = &in[n - 1];
    } else {
        key = n >= 1 ? path_arg(f, 0, in) : NULL;
        val = &in[1];
    }
    if (!key || (f->inst->nr < 3 && n < 2)) {
        free(key);
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetErr(f,
                                   "TypeError: kvspace·set requires path and value");
    }
    kvlangKvPair_t p = {key, *val};
    char err[256];
    int rc = kvlangKvSet(f->kv, &p, 1, err, sizeof err);
    free(key);
    kvlangBuiltinFreeInputs(in, n);
    if (rc != 0)
        return kvlangBuiltinSetErr(f, "%s", err);
    kvlangBuiltinNextPc(f);
    return 0;
}

static int kv_path_void(kvlangFrame_t *f, const char *name,
                        int (*op)(kvlangKv_t *, const char *, char *,
                                  uint32_t)) {
    kvlangXvalue_t in[1];
    int n = kvlangBuiltinReadInputs(f, in, 1);
    char *key = n >= 1 ? path_arg(f, 0, in) : NULL;
    if (!key) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetErr(f, "TypeError: %s requires 1 path arg",
                                   name);
    }
    char err[256];
    int rc = op(f->kv, key, err, sizeof err);
    free(key);
    kvlangBuiltinFreeInputs(in, n);
    if (rc != 0)
        return kvlangBuiltinSetErr(f, "%s", err);
    kvlangBuiltinNextPc(f);
    return 0;
}

int kvlangCDel(kvlangFrame_t *f) {
    return kv_path_void(f, "kvspace·del", kvlangKvDel);
}

int kvlangCDelTree(kvlangFrame_t *f) {
    return kv_path_void(f, "kvspace·deltree", kvlangKvDelTree);
}

/* 绝对路径 / 路径字符串直取，否则裸标识符解析为本帧槽位 key（对齐 kvspace·list 的裸变量处理）。 */
static char *resolve_path_arg(kvlangFrame_t *f, int idx,
                              const kvlangXvalue_t *in) {
    char *p = path_arg(f, idx, in);
    if (p)
        return p;
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *base =
        kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->reads[idx].name);
    free(fr);
    return base;
}

static int kv_two_path_void(kvlangFrame_t *f, const char *name,
                            int (*op)(kvlangKv_t *, const char *, const char *,
                                      char *, uint32_t)) {
    kvlangXvalue_t in[2];
    int n = kvlangBuiltinReadInputs(f, in, 2);
    char *src = n >= 1 ? resolve_path_arg(f, 0, in) : NULL;
    char *dst = n >= 2 ? resolve_path_arg(f, 1, in) : NULL;
    if (!src || !dst) {
        free(src);
        free(dst);
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetErr(
            f, "TypeError: %s requires src,dst path args", name);
    }
    char err[256];
    int rc = op(f->kv, src, dst, err, sizeof err);
    free(src);
    free(dst);
    kvlangBuiltinFreeInputs(in, n);
    if (rc != 0)
        return kvlangBuiltinSetErr(f, "%s", err);
    kvlangBuiltinNextPc(f);
    return 0;
}

int kvlangCCp(kvlangFrame_t *f) {
    return kv_two_path_void(f, "kvspace·cp", kvlangKvCp);
}

int kvlangCCpTree(kvlangFrame_t *f) {
    return kv_two_path_void(f, "kvspace·cpdir", kvlangKvCpTree);
}

int kvlangCCpList(kvlangFrame_t *f) {
    return kv_two_path_void(f, "kvspace·cplist", kvlangKvCpList);
}

int kvlangCAbs(kvlangFrame_t *f) {
    kvlangXvalue_t in[MAX_PARAMS];
    int n = kvlangBuiltinReadInputs(f, in, MAX_PARAMS);
    char *p = NULL;
    if (n >= 2)
        p = member_path(f, in, n);
    else if (n >= 1)
        p = resolve_path_arg(f, 0, in);
    if (!p) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetErr(f, "TypeError: kvlang·abs requires a key");
    }
    /* 产出 Ptr（ref=1）：langtype = 目标 kindexpr、body = 目标绝对路径。
     * &x ≡ kvlang·abs(x)：取址返回指向 x 所在节点的软链接（单跳同型），不再产 char 路径串。 */
    kvlangXvalue_t tg;
    kvlangXvalueZero(&tg);
    kvlangKvGetOne(f->kv, p, &tg);
    char lt[256] = {0};
    if (!kvlangXvalueNone(&tg)) {
        kvspaceHead_t h;
        if (kvlangXvalueHead(&tg, &h) == 0 && h.langtype[0]) {
            size_t llen = strlen((const char *)h.langtype);
            if (llen > sizeof lt - 1)
                llen = sizeof lt - 1;
            memcpy(lt, h.langtype, llen);
        }
    }
    kvlangXvalue_t r;
    kvlangXvalueNewPtr(&r, lt, p);
    int rc = kvlangBuiltinWriteResult(f, &r);
    kvlangXvalueFree(&r);
    kvlangXvalueFree(&tg);
    free(p);
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangCList(kvlangFrame_t *f) {
    if (f->inst->nw == 0)
        return kvlangBuiltinSetErr(f,
                                   "TypeError: kvspace·list requires a write param");
    kvlangXvalue_t in[1];
    int n = kvlangBuiltinReadInputs(f, in, 1);
    char *key = n >= 1 ? path_arg(f, 0, in) : NULL;
    if (!key && f->inst->nr >= 1) {
        /* 裸变量（obj/map 成员目录）：解析为 <frame>/<name>. 目录。 */
        const char *name = f->inst->reads[0].name;
        if (name[0] != '/') {
            char *fr = kvlangKeytreeFrameRoot(f->pc);
            char *base = kvlangBuiltinResolveWriteSlot(f->kv, fr, name);
            free(fr);
            key = kvlangKeytreeMember(base, "");
            free(base);
        }
    }
    if (!key) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetErr(f, "TypeError: kvspace·list requires 1 path arg");
    }
    char **names = NULL;
    int count = 0;
    kvlangKvList(f->kv, key, false, false, &names, &count);
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *dst =
        kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->writes[0].name);
    free(fr);
    /* 结果是一个 stringkeymap：容器值在 dst（body 空，dims=[count] 落 head），
     * kvlangBuiltinMemindex 在 dst·，成员名是坐标段 [i]，值是对应的成员名字符串。 */
    char **coords = malloc(sizeof(char *) * (size_t)(count > 0 ? count : 1));
    for (int i = 0; i < count; i++) {
        kvlangStrbuf_t s;
        kvlangStrbufInit(&s);
        kvlangStrbufPrintf(&s, "[%d]", i);
        coords[i] = kvlangStrbufDetach(&s);
    }
    char err[256];
    int32_t dims[1] = {count};
    kvlangXvalue_t mark;
    kvlangBuiltinMapMarker(&mark, "[int64]" MEMBER_SEP "[]char/utf8", dims, 1);
    kvlangKvPair_t p0 = {dst, mark};
    kvlangKvSet(f->kv, &p0, 1, err, sizeof err);
    kvlangXvalueFree(&mark);
    char *dir = kvlangKeytreeMember(dst, "");
    kvlangXvalue_t mi;
    kvlangBuiltinMemindex(&mi, (const char *const *)coords, count);
    kvlangKvPair_t p1 = {dir, mi};
    kvlangKvSet(f->kv, &p1, 1, err, sizeof err);
    kvlangXvalueFree(&mi);
    free(dir);
    for (int i = 0; i < count; i++)
        free(coords[i]);
    free(coords);
    for (int i = 0; i < count; i++) {
        int64_t c[1] = {i};
        char *k = kvlangBuiltinScatterKey(dst, c, 1);
        kvlangXvalue_t e;
        kvlangXvalueNewCharUtf8(&e, names[i]);
        kvlangKvPair_t p = {k, e};
        kvlangKvSet(f->kv, &p, 1, err, sizeof err);
        kvlangXvalueFree(&e);
        free(k);
        free(names[i]);
    }
    for (int i = count;; i++) {
        int64_t c[1] = {i};
        char *k = kvlangBuiltinScatterKey(dst, c, 1);
        kvlangXvalue_t v;
        kvlangXvalueZero(&v);
        kvlangKvGetOne(f->kv, k, &v);
        bool none = kvlangXvalueNone(&v);
        kvlangXvalueFree(&v);
        if (none) {
            free(k);
            break;
        }
        kvlangKvDel(f->kv, k, err, sizeof err);
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
static char *kv_list_dir(kvlangFrame_t *f, kvlangXvalue_t *in, int n) {
    char *key = n >= 1 ? path_arg(f, 0, in) : NULL;
    if (!key && f->inst->nr >= 1) {
        const char *name = f->inst->reads[0].name;
        if (name[0] != '/') {
            char *fr = kvlangKeytreeFrameRoot(f->pc);
            char *base = kvlangBuiltinResolveWriteSlot(f->kv, fr, name);
            free(fr);
            key = kvlangKeytreeMember(base, "");
            free(base);
        }
    }
    return key;
}

int kvlangCListLen(kvlangFrame_t *f) {
    kvlangXvalue_t in[1];
    int n = kvlangBuiltinReadInputs(f, in, 1);
    char *key = kv_list_dir(f, in, n);
    int64_t count = 0;
    if (key) {
        char **names = NULL;
        int cnt = 0;
        kvlangKvList(f->kv, key, false, false, &names, &cnt);
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

int kvlangCListN(kvlangFrame_t *f) {
    kvlangXvalue_t in[2];
    int n = kvlangBuiltinReadInputs(f, in, 2);
    char *key = kv_list_dir(f, in, n);
    int idx = n >= 2 ? (int)kvlangScalarI64(kvlangXvalueScalar(&in[1])) : -1;
    kvlangXvalue_t r;
    kvlangXvalueZero(&r);
    if (key && idx >= 0) {
        char **names = NULL;
        int cnt = 0;
        kvlangKvList(f->kv, key, false, false, &names, &cnt);
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

int kvlangCMkindex(kvlangFrame_t *f) {
    kvlangXvalue_t in[2];
    int n = kvlangBuiltinReadInputs(f, in, 2);
    char *key = n >= 1 ? path_arg(f, 0, in) : NULL;
    if (!key) {
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetErr(f, "TypeError: kvspace·mkindex requires a path");
    }
    uint32_t capacity =
        n >= 2 ? (uint32_t)kvlangScalarI64(kvlangXvalueScalar(&in[1])) : 0;
    char err[256];
    int rc = kvlangKvMkindex(f->kv, key, capacity, err, sizeof err);
    free(key);
    kvlangBuiltinFreeInputs(in, n);
    if (rc != 0)
        return kvlangBuiltinSetErr(f, "%s", err);
    kvlangBuiltinNextPc(f);
    return 0;
}

int kvlangCExtIndex(kvlangFrame_t *f) {
    kvlangXvalue_t in[2];
    int n = kvlangBuiltinReadInputs(f, in, 2);
    char *key = n >= 1 ? path_arg(f, 0, in) : NULL;
    char *ext = n >= 2 ? kvlangXvalueValueString(&in[1]) : NULL;
    if (!key || !ext) {
        free(key);
        free(ext);
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetErr(
            f, "TypeError: kvspace·extindex requires path and ext path");
    }
    char err[256];
    int rc = kvlangKvExtIndex(f->kv, key, ext, err, sizeof err);
    free(key);
    free(ext);
    kvlangBuiltinFreeInputs(in, n);
    if (rc != 0)
        return kvlangBuiltinSetErr(f, "%s", err);
    kvlangBuiltinNextPc(f);
    return 0;
}

int kvlangCRmIndexExt(kvlangFrame_t *f) {
    return kv_path_void(f, "kvspace·rmindexext", kvlangKvDelExtIndex);
}

int kvlangCWatch(kvlangFrame_t *f) {
    kvlangXvalue_t in[2];
    int n = kvlangBuiltinReadInputs(f, in, 2);
    char *key = n >= 1 ? path_arg(f, 0, in) : NULL;
    if (!key || n < 2) {
        free(key);
        kvlangBuiltinFreeInputs(in, n);
        return kvlangBuiltinSetErr(
            f, "TypeError: kvspace·watch requires key and target");
    }
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    kvlangKvWatch(f->kv, key, &in[1], 1000000, &v);
    int rc = kvlangBuiltinWriteResult(f, &v);
    kvlangXvalueFree(&v);
    free(key);
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}
