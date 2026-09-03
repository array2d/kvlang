// builtin_kv —— kv·* KV 树 rwir

#include "builtin_internal.h"

static bool is_int_kind(const char *k) { return kvlangXvalueIsIntKind(k) || kvlangXvalueIsUintKind(k); }

static char *kvlangKvKey(const kvlangXvalue_t *v) {
    if (kvlangXvalueIsCharKind(kvlangXvalueKind(v))) return kvlangXvalueValueString(v);
    if (is_int_kind(kvlangXvalueKind(v))) { char buf[32]; snprintf(buf, sizeof buf, "%lld", (long long)kvlangXvalueAsInt64(v)); return strdup(buf); }
    return strdup("");
}

static char *path_arg(kvlangFrame_t *f, int idx, const kvlangXvalue_t *in) {
    const char *name = f->inst->reads[idx].name;
    if (name[0] == '/') return strdup(name);
    if (!kvlangXvalueNone(&in[idx])) {
        char *s = kvlangXvalueValueString(&in[idx]);
        if (s[0] == '/') return s;
        free(s);
    }
    return NULL;
}

/* 容器 base：object/map/index/extindex 或 struct 实例（kind=structref，以 / 起头）。
 * 容器取其写槽路径拼成员 key；非容器把 base 值当 key 串。 */
static bool base_is_container(const kvlangXvalue_t *base) {
    if (kvlangXvalueNone(base)) return true;
    const char *k = kvlangXvalueKind(base);
    return k[0] == '/' || strcmp(k, KVSPACE_KIND_OBJ) == 0 || strcmp(k, KVSPACE_KIND_MAP) == 0 ||
           strcmp(k, KVSPACE_KIND_INDEX) == 0 || strcmp(k, KVSPACE_KIND_EXT_INDEX) == 0;
}

static char *member_path(kvlangFrame_t *f, const kvlangXvalue_t *in, int n) {
    const kvlangXvalue_t *base = &in[0];
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *bp = base_is_container(base) ? kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->reads[0].name) : kvlangXvalueValueString(base);
    free(fr);
    /* 成员链：base 之后逐段拼 key（变参），每段可为静态字面量或动态键（运行时值）。 */
    for (int i = 1; i < n; i++) {
        char *kk = kvlangKvKey(&in[i]);
        char *next = kvlangKeytreeMember(bp, kk);
        free(kk); free(bp);
        bp = next;
    }
    return bp;
}

int kvlangBuiltinKvGet(kvlangFrame_t *f) {
    kvlangXvalue_t in[MAX_PARAMS]; int n = kvlangBuiltinReadInputs(f, in, MAX_PARAMS);
    char *key = f->inst->nr >= 2 ? member_path(f, in, n) : (n >= 1 ? path_arg(f, 0, in) : NULL);
    if (!key) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "TypeError: kv.get requires a path"); }
    kvlangXvalue_t v; kvlangXvalueZero(&v);
    kvlangKvGetOne(f->kv, key, &v);
    int rc = kvlangBuiltinWriteResult(f, &v); kvlangXvalueFree(&v);
    free(key); kvlangBuiltinFreeInputs(in, n); return rc;
}

int kvlangBuiltinKvSet(kvlangFrame_t *f) {
    kvlangXvalue_t in[MAX_PARAMS]; int n = kvlangBuiltinReadInputs(f, in, MAX_PARAMS);
    char *key; kvlangXvalue_t *val;
    if (f->inst->nr >= 3) { key = member_path(f, in, n - 1); val = &in[n - 1]; }
    else { key = n >= 1 ? path_arg(f, 0, in) : NULL; val = &in[1]; }
    if (!key || (f->inst->nr < 3 && n < 2)) { free(key); kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "TypeError: kv.set requires path and value"); }
    kvlangKvPair_t p = { key, *val };
    char err[256]; int rc = kvlangKvSet(f->kv, &p, 1, err, sizeof err);
    free(key); kvlangBuiltinFreeInputs(in, n);
    if (rc != 0) return kvlangBuiltinSetErr(f, "%s", err);
    kvlangBuiltinNextPc(f); return 0;
}

static int kv_path_void(kvlangFrame_t *f, const char *name,
                        int (*op)(kvlangKv_t *, const char *, char *, uint32_t)) {
    kvlangXvalue_t in[1]; int n = kvlangBuiltinReadInputs(f, in, 1);
    char *key = n >= 1 ? path_arg(f, 0, in) : NULL;
    if (!key) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "TypeError: %s requires 1 path arg", name); }
    char err[256]; int rc = op(f->kv, key, err, sizeof err);
    free(key); kvlangBuiltinFreeInputs(in, n);
    if (rc != 0) return kvlangBuiltinSetErr(f, "%s", err);
    kvlangBuiltinNextPc(f); return 0;
}

int kvlangBuiltinKvDel(kvlangFrame_t *f) { return kv_path_void(f, "kv.del", kvlangKvDel); }

int kvlangBuiltinKvDelTree(kvlangFrame_t *f) { return kv_path_void(f, "kv.deltree", kvlangKvDelTree); }

/* 绝对路径 / 路径字符串直取，否则裸标识符解析为本帧槽位 key（对齐 kv.list 的裸变量处理）。 */
static char *resolve_path_arg(kvlangFrame_t *f, int idx, const kvlangXvalue_t *in) {
    char *p = path_arg(f, idx, in);
    if (p) return p;
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *base = kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->reads[idx].name);
    free(fr);
    return base;
}

static int kv_two_path_void(kvlangFrame_t *f, const char *name,
                            int (*op)(kvlangKv_t *, const char *, const char *, char *, uint32_t)) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    char *src = n >= 1 ? resolve_path_arg(f, 0, in) : NULL;
    char *dst = n >= 2 ? resolve_path_arg(f, 1, in) : NULL;
    if (!src || !dst) { free(src); free(dst); kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "TypeError: %s requires src,dst path args", name); }
    char err[256]; int rc = op(f->kv, src, dst, err, sizeof err);
    free(src); free(dst); kvlangBuiltinFreeInputs(in, n);
    if (rc != 0) return kvlangBuiltinSetErr(f, "%s", err);
    kvlangBuiltinNextPc(f); return 0;
}

int kvlangBuiltinKvCp(kvlangFrame_t *f) { return kv_two_path_void(f, "kv.cp", kvlangKvCp); }

int kvlangBuiltinKvCpTree(kvlangFrame_t *f) { return kv_two_path_void(f, "kv.cpdir", kvlangKvCpTree); }

int kvlangBuiltinKvAbs(kvlangFrame_t *f) {
    kvlangXvalue_t in[1]; int n = kvlangBuiltinReadInputs(f, in, 1);
    char *p = n >= 1 ? resolve_path_arg(f, 0, in) : NULL;
    if (!p) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "TypeError: kv.abs requires a key"); }
    kvlangXvalue_t r; kvlangXvalueNewCharUtf32(&r, p);
    int rc = kvlangBuiltinWriteResult(f, &r); kvlangXvalueFree(&r);
    free(p); kvlangBuiltinFreeInputs(in, n); return rc;
}

int kvlangBuiltinKvList(kvlangFrame_t *f) {
    if (f->inst->nw == 0) return kvlangBuiltinSetErr(f, "TypeError: kv.list requires a write param");
    kvlangXvalue_t in[1]; int n = kvlangBuiltinReadInputs(f, in, 1);
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
    if (!key) { kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "TypeError: kv.list requires 1 path arg"); }
    char **names = NULL; int count = 0;
    kvlangKvList(f->kv, key, false, false, &names, &count);
    char *fr = kvlangKeytreeFrameRoot(f->pc);
    char *dst = kvlangBuiltinResolveWriteSlot(f->kv, fr, f->inst->writes[0].name);
    free(fr);
    /* 结果是一个 stringkeymap：容器值在 dst（body 空，dims=[count] 落 head），
     * kvlangBuiltinMemindex 在 dst·，成员名是坐标段 [i]，值是对应的成员名字符串。 */
    char **coords = malloc(sizeof(char *) * (size_t)(count > 0 ? count : 1));
    for (int i = 0; i < count; i++) {
        kvlangStrbuf_t s; kvlangStrbufInit(&s); kvlangStrbufPrintf(&s, "[%d]", i);
        coords[i] = kvlangStrbufDetach(&s);
    }
    char err[256];
    int32_t dims[1] = { count };
    kvlangXvalue_t mark; kvlangBuiltinMapMarker(&mark, dims, 1);
    kvlangKvPair_t p0 = { dst, mark }; kvlangKvSet(f->kv, &p0, 1, err, sizeof err);
    kvlangXvalueFree(&mark);
    char *dir = kvlangKeytreeMember(dst, "");
    kvlangXvalue_t mi; kvlangBuiltinMemindex(&mi, (const char *const *)coords, count);
    kvlangKvPair_t p1 = { dir, mi }; kvlangKvSet(f->kv, &p1, 1, err, sizeof err);
    kvlangXvalueFree(&mi); free(dir);
    for (int i = 0; i < count; i++) free(coords[i]);
    free(coords);
    for (int i = 0; i < count; i++) {
        int64_t c[1] = { i };
        char *k = kvlangBuiltinScatterKey(dst, c, 1);
        kvlangXvalue_t e; kvlangXvalueNewCharUtf8(&e, names[i]);
        kvlangKvPair_t p = { k, e }; kvlangKvSet(f->kv, &p, 1, err, sizeof err);
        kvlangXvalueFree(&e); free(k); free(names[i]);
    }
    for (int i = count; ; i++) {
        int64_t c[1] = { i };
        char *k = kvlangBuiltinScatterKey(dst, c, 1);
        kvlangXvalue_t v; kvlangXvalueZero(&v); kvlangKvGetOne(f->kv, k, &v);
        bool none = kvlangXvalueNone(&v); kvlangXvalueFree(&v);
        if (none) { free(k); break; }
        kvlangKvDel(f->kv, k, err, sizeof err); free(k);
    }
    free(names); free(dst); free(key);
    kvlangBuiltinNextPc(f); kvlangBuiltinFreeInputs(in, n); return 0;
}

/* 解析 obj/map 成员目录 key（<frame>/<name>.），供 kv.list/listlen/listn 共用。 */
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

int kvlangBuiltinKvListLen(kvlangFrame_t *f) {
    kvlangXvalue_t in[1]; int n = kvlangBuiltinReadInputs(f, in, 1);
    char *key = kv_list_dir(f, in, n);
    int64_t count = 0;
    if (key) {
        char **names = NULL; int cnt = 0;
        kvlangKvList(f->kv, key, false, false, &names, &cnt);
        for (int i = 0; i < cnt; i++) free(names[i]);
        free(names);
        count = cnt;
        free(key);
    }
    kvlangXvalue_t r; kvlangXvalueNewInt64(&r, count);
    int rc = kvlangBuiltinWriteResult(f, &r); kvlangXvalueFree(&r); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinKvListN(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    char *key = kv_list_dir(f, in, n);
    int idx = n >= 2 ? (int)kvlangXvalueAsInt64(&in[1]) : -1;
    kvlangXvalue_t r; kvlangXvalueZero(&r);
    if (key && idx >= 0) {
        char **names = NULL; int cnt = 0;
        kvlangKvList(f->kv, key, false, false, &names, &cnt);
        if (idx < cnt) {
            kvlangXvalueNewCharUtf8(&r, names[idx]);
        }
        for (int i = 0; i < cnt; i++) free(names[i]);
        free(names);
        free(key);
    }
    int rc = kvlangBuiltinWriteResult(f, &r); kvlangXvalueFree(&r); kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinKvMkindex(kvlangFrame_t *f) { return kv_path_void(f, "kv.mkindex", kvlangKvMkindex); }

int kvlangBuiltinKvExtIndex(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    char *key = n >= 1 ? path_arg(f, 0, in) : NULL;
    char *ext = n >= 2 ? kvlangXvalueValueString(&in[1]) : NULL;
    if (!key || !ext) { free(key); free(ext); kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "TypeError: kv.extindex requires path and ext path"); }
    char err[256]; int rc = kvlangKvExtIndex(f->kv, key, ext, err, sizeof err);
    free(key); free(ext); kvlangBuiltinFreeInputs(in, n);
    if (rc != 0) return kvlangBuiltinSetErr(f, "%s", err);
    kvlangBuiltinNextPc(f); return 0;
}

int kvlangBuiltinKvRmIndexExt(kvlangFrame_t *f) { return kv_path_void(f, "kv.rmindexext", kvlangKvDelExtIndex); }

int kvlangBuiltinKvWatch(kvlangFrame_t *f) {
    kvlangXvalue_t in[2]; int n = kvlangBuiltinReadInputs(f, in, 2);
    char *key = n >= 1 ? path_arg(f, 0, in) : NULL;
    if (!key || n < 2) { free(key); kvlangBuiltinFreeInputs(in, n); return kvlangBuiltinSetErr(f, "TypeError: kv.watch requires key and target"); }
    kvlangXvalue_t v; kvlangXvalueZero(&v);
    kvlangKvWatch(f->kv, key, &in[1], 1000000, &v);
    int rc = kvlangBuiltinWriteResult(f, &v); kvlangXvalueFree(&v);
    free(key); kvlangBuiltinFreeInputs(in, n); return rc;
}
