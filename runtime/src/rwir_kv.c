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
    free(root);
    free(fr);
    return name;
}

static char *path_arg(kvlangFrame_t *f, int idx, const kvlangXvalue_t *in) {
    const char *name = f->instruction->reads[idx].name;
    /* 字面量无槽（name=NULL）：只有绝对路径描述符才可能是槽路径。 */
    if (name && name[0] == '/') {
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

static size_t fmt_i64_into(int64_t val, char *buf, size_t cap) {
    char digits[24];
    int d = 0;
    uint64_t u = val < 0 ? (uint64_t)-(val + 1) + 1ULL : (uint64_t)val;
    do {
        digits[d++] = (char)('0' + (int)(u % 10));
        u /= 10;
    } while (u);
    size_t need = (size_t)d + (val < 0 ? 1 : 0);
    if (need + 1 > cap)
        return (size_t)-1;
    size_t p = 0;
    if (val < 0)
        buf[p++] = '-';
    while (d)
        buf[p++] = digits[--d];
    buf[p] = 0;
    return p;
}

static size_t kvspace_key_into(const kvlangXvalue_t *v, char *buf, size_t cap) {
    if (v->length == 40 && v->bytes && v->bytes[0] == 5 && v->bytes[1] == 0 &&
        memcmp(v->bytes + 18, "int64", 6) == 0)
        return fmt_i64_into((int64_t)xh_rd64(v->bytes + 32), buf, cap);
    if (kvlangXvalueNone(v)) {
        if (cap)
            buf[0] = 0;
        return 0;
    }
    kvlangScalar_t s = kvlangXvalueScalar(v);
    if (kvlangLangtypeIsInt(s.kind_id))
        return fmt_i64_into(kvlangScalarI64(s), buf, cap);
    if (s.kind_id == KVLANG_LT_CHAR_UTF8 || s.kind_id == KVLANG_LT_CHAR_ASCII) {
        size_t n = (size_t)s.length;
        if (n + 1 > cap)
            return (size_t)-1;
        memcpy(buf, s.bytes, n);
        buf[n] = 0;
        return n;
    }
    if (kvlangLangtypeIsChar(s.kind_id)) {
        char *str = kvlangXvalueValueString(v);
        size_t n = strlen(str);
        if (n + 1 > cap) {
            free(str);
            return (size_t)-1;
        }
        memcpy(buf, str, n + 1);
        free(str);
        return n;
    }
    if (cap)
        buf[0] = 0;
    return 0;
}

/* 成员访问的 base 解析（spec [[成员访问]] 的「按指针优先、按名回退」+ 形态判定）：
 *   Ptr → 取其目标路径（目标须是容器）；容器/None → 取 base 的写槽路径；路径串（char/）→ 其值即父路径；
 *   其余（compact 数组、标量）不是容器 → 报错，绝不把 body 字节冒充路径串拼键。 */
static const char *member_path_buf(kvlangFrame_t *f, const kvlangXvalue_t *in,
                                   int n, char *buf, size_t cap,
                                   char **owned_out, char *err, size_t errsz) {
    *owned_out = NULL;
    const kvlangXvalue_t *base = &in[0];
    size_t blen = 0;
    err[0] = 0;
    if (kvlangXvalueIsPtr(base)) {
        /* 数据 Ptr（&x）作成员 base：直接取其目标路径，逐段下钻。Ptr 的 head 即目标 kindexpr，
         * 故容器判定即对目标判定——`p·val`（目标 struct）通过，`p·[0]`（目标 compact 数组）报错。 */
        if (!base_is_container(base))
            return member_not_container(base, err, errsz);
        const uint8_t *b = xh_body(base->bytes);
        int32_t l = xh_content_len(base->bytes);
        if (l < 0 || (size_t)l + 1 > cap)
            goto slow;
        memcpy(buf, b, (size_t)l);
        blen = (size_t)l;
        buf[blen] = 0;
    } else if (base_is_container(base)) {
        /* 容器字面量（{...}）layout 只允许作赋值右值，不会出现在读参里，
         * 故容器 base 必有槽名。 */
        const char *rname = f->instruction->reads[0].name;
        if (rname && rname[0] == '/') {
            blen = strlen(rname);
            if (blen + 1 > cap)
                goto slow;
            memcpy(buf, rname, blen + 1);
        } else {
            goto slow;
        }
    } else if (kvlangXvalueIsCharKind(kvlangXvalueKind(base))) {
        blen = kvspace_key_into(base, buf, cap);
        if (blen == (size_t)-1)
            goto slow;
    } else {
        return member_not_container(base, err, errsz);
    }
    for (int i = 1; i < n; i++) {
        if (blen + MEMBER_SEP_LEN + 2 >= cap)
            goto slow;
        memcpy(buf + blen, MEMBER_SEP, MEMBER_SEP_LEN);
        blen += MEMBER_SEP_LEN;
        size_t slen = kvspace_key_into(&in[i], buf + blen, cap - blen);
        if (slen == (size_t)-1)
            goto slow;
        blen += slen;
        buf[blen] = 0;
    }
    return buf;

slow:;
    char *bp = NULL;
    if (kvlangXvalueIsPtr(base)) {
        bp = kvlangXvaluePtrTarget(base);
    } else if (base_is_container(base)) {
        const char *fr = f->frame_root;
        char *owned_fr = NULL;
        if (!fr) {
            owned_fr = kvlangKeytreeFrameRoot(f->pc);
            fr = owned_fr;
        }
        bp = kvlangBuiltinResolveWriteSlot(f->kvspace, fr,
                                           f->instruction->reads[0].name);
        free(owned_fr);
    } else {
        bp = kvlangXvalueValueString(base);
    }
    for (int i = 1; i < n; i++) {
        char *kk = kvlangKvspaceKey(&in[i]);
        char *next = kvlangKeytreeMember(bp, kk);
        free(kk);
        free(bp);
        bp = next;
    }
    *owned_out = bp;
    return bp;
}

static char *member_path(kvlangFrame_t *f, const kvlangXvalue_t *in, int n,
                         char *err, size_t errsz) {
    char buf[KV_KEY_MAX];
    char *owned = NULL;
    const char *p =
        member_path_buf(f, in, n, buf, sizeof buf, &owned, err, errsz);
    if (!p)
        return NULL;
    return owned ? owned : strdup(p);
}

int kvlangBuiltinKvspaceGet(kvlangFrame_t *f) {
    kvlangXvalue_t in_small[16], *in = in_small;
    kvlangXvalue_t *in_big = NULL;
    int rcap = f->instruction->read_count <= 16 ? 16 : MAX_PARAMS;
    if (rcap > 16)
        in = in_big = malloc(sizeof *in * MAX_PARAMS);
    int n = kvlangBuiltinReadInputs(f, in, rcap);
    char merr[512] = {0};
    char keybuf[KV_KEY_MAX];
    char *owned_key = NULL;
    const char *key =
        f->instruction->read_count >= 2
            ? member_path_buf(f, in, n, keybuf, sizeof keybuf, &owned_key, merr,
                              sizeof merr)
            : (n >= 1 ? (owned_key = path_arg(f, 0, in)) : NULL);
    if (!key) {
        kvlangBuiltinFreeInputs(in, n);
        free(in_big);
        return kvlangBuiltinSetError(f, "TypeError: kvspace·get: %s",
                                     merr[0] ? merr : "requires a path");
    }
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    kvlangKvspaceGetOne(f->kvspace, key, &v);
    int rc = kvlangBuiltinWriteResult(f, &v);
    free(owned_key);
    kvlangBuiltinFreeInputs(in, n);
    free(in_big);
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
    if (base[0] == '/') {
        kvlangXvalue_t cur;
        kvlangXvalueZero(&cur);
        kvlangKvspaceGetOne(kv, base, &cur);
        return !kvlangXvalueNone(&cur) ? 0 : -1;
    }
    char *path = kvlangBuiltinResolveWriteSlot(kv, frame_root, base);
    kvlangXvalue_t cur;
    kvlangXvalueZero(&cur);
    kvlangKvspaceGetOne(kv, path, &cur);
    int ok = !kvlangXvalueNone(&cur);
    free(path);
    return ok ? 0 : -1;
}

int kvlangBuiltinKvspaceSet(kvlangFrame_t *f) {
    kvlangXvalue_t in_small[16], *in = in_small;
    kvlangXvalue_t *in_big = NULL;
    int rcap = f->instruction->read_count <= 16 ? 16 : MAX_PARAMS;
    if (rcap > 16)
        in = in_big = malloc(sizeof *in * MAX_PARAMS);
    int n = kvlangBuiltinReadInputs(f, in, rcap);
    char keybuf[KV_KEY_MAX];
    char *owned_key = NULL;
    const char *key = NULL;
    kvlangXvalue_t *val;
    char merr[512];
    if (f->instruction->read_count >= 3) {
        const char *base = f->instruction->reads[0].name;
        int ok = 0;
        if (base && base[0] && strncmp(base, "/lib", 4) != 0 &&
            kvlangXvalueNone(&in[0])) {
            const char *fr = f->frame_root;
            char *owned_fr = NULL;
            if (!fr) {
                owned_fr = kvlangKeytreeFrameRoot(f->pc);
                fr = owned_fr;
            }
            ok = kvlangBuiltinCheckMemoryHead(f->kvspace, fr, base);
            free(owned_fr);
        }
        if (ok != 0) {
            kvlangBuiltinFreeInputs(in, n);
            free(in_big);
            return kvlangBuiltinSetError(
                f,
                "TypeError: memhead %s does not exist — declare the container "
                "first (e.g. `%s:T = {}`)",
                base, base);
        }
        key = member_path_buf(f, in, n - 1, keybuf, sizeof keybuf, &owned_key,
                              merr, sizeof merr);
        val = &in[n - 1];
    } else {
        key = n >= 1 ? (owned_key = path_arg(f, 0, in)) : NULL;
        merr[0] = 0;
        val = &in[1];
    }
    if (!key || (f->instruction->read_count < 3 && n < 2)) {
        free(owned_key);
        kvlangBuiltinFreeInputs(in, n);
        free(in_big);
        return kvlangBuiltinSetError(f, "TypeError: kvspace·set: %s",
                                     merr[0] ? merr
                                             : "requires path and value");
    }
    kvlangKvPair_t p = {(char *)key, *val};
    char err[256];
    int rc = kvlangKvspaceSet(f->kvspace, &p, 1, err, sizeof err);
    free(owned_key);
    kvlangBuiltinFreeInputs(in, n);
    free(in_big);
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

/* 绝对路径 / 路径字符串直取，否则裸标识符解析为本帧槽位 key（对齐 kvspace·listlen/listat 的裸变量处理）。 */
static char *resolve_path_arg(kvlangFrame_t *f, int idx,
                              const kvlangXvalue_t *in) {
    char *p = path_arg(f, idx, in);
    if (p)
        return p;
    if (!f->instruction->reads[idx].name)
        return NULL; /* 字面量且不是绝对路径：没有槽路径，调用方报 TypeError */
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
    int rc = kvlangBuiltinEmitResult(f, &r);
    free(p);
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}

/* 解析 obj/map 成员目录 key（<frame>/<name>.），供 kvspace·list/listlen/listn 共用。 */
static char *list_dir_key(kvlangFrame_t *f, kvlangXvalue_t *in, int n) {
    char *key = n >= 1 ? path_arg(f, 0, in) : NULL;
    if (!key && f->instruction->read_count >= 1 &&
        f->instruction->reads[0].name) {
        /* 字面量无槽：list 长度为 0 / listat 为 None（调用方按 NULL key 处理）。 */
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
        int cnt = 0;
        if (kvlangKvspaceListLen(f->kvspace, key, false, false, &cnt) == 0)
            count = cnt;
        free(key);
    }
    int rc = kvlangBuiltinEmitInt64(f, count);
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}

int kvlangBuiltinKvspaceListAt(kvlangFrame_t *f) {
    kvlangXvalue_t in[2];
    int n = kvlangBuiltinReadInputs(f, in, 2);
    char *key = list_dir_key(f, in, n);
    int idx = n >= 2 ? (int)kvlangScalarI64(kvlangXvalueScalar(&in[1])) : -1;
    kvlangXvalue_t r;
    kvlangXvalueZero(&r);
    if (key && idx >= 0) {
        char *name = NULL;
        if (kvlangKvspaceListAt(f->kvspace, key, false, false, idx, &name) == 0) {
            kvlangXvalueNewCharUtf8(&r, name);
            free(name);
        }
        free(key);
    }
    int rc = kvlangBuiltinEmitResult(f, &r);
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
    free(key);
    kvlangBuiltinFreeInputs(in, n);
    return rc;
}
