#include "kvcpu_internal.h"

/* xvalue 初始化类 rwir：以真实值给一个 xvalue 初值（构造/设值），参数豁免 `*T` 约定——
 * 它们依赖真实值本身，见 #329 评论「关于五.7 指针」。其余 rwir 参数一律须 `*T`（一层指针）。 */
static bool rwir_init_by_value(const char *opcode) {
    return strcmp(opcode, "input") == 0 || strcmp(opcode, "json·from") == 0;
}

/* rwir 参数类型串规范化（读参/写参共用）：参数默认 `*T`——逐 branch 恰一层指针
 * （拒 T / **T / ***T），剥 `*` 后写入 out（供校验/比对）；exempt=1 时参数即真实值 T，
 * 原样写入 out（xvalue 初始化类 rwir 的读参）。违反约定 → 置 TypeError 并返回 -1。
 * where 是「读参」/「写参」，slot 从 1 起。 */
static int rwir_normalize_param(kvspace_t *kv, const char *vtid, const char *pc,
                           const char *opcode, const char *where, int slot,
                           bool exempt, const char *exp, char *out, size_t cap) {
    if (exempt) {
        snprintf(out, cap, "%s", exp);
        return 0;
    }
    const char *q = exp;
    size_t o = 0;
    while (*q) {
        int depth = 0;
        while (*q == '*') {
            depth++;
            q++;
        }
        if (depth != 1) {
            char msg[256];
            snprintf(msg, sizeof msg,
                     "TypeError: %s %s %d: rwir 参数须为 *T（一层指针），got %s",
                     opcode, where, slot, exp);
            kvlangVthreadSetError(kv, vtid, pc, msg);
            return -1;
        }
        while (*q && *q != '|' && o + 1 < cap)
            out[o++] = *q++;
        if (*q == '|' && o + 1 < cap)
            out[o++] = *q++;
    }
    out[o] = 0;
    return 0;
}

/* 派发期读参类型校验（runtime篇-07 第八节）：把每个实参的 kind 逐一匹配
 * rwir/rwfunc 定义的读参 kindexp。def_sig 为读参 kindexp 在前的 \n 分隔列表，
 * def_nr 为定义读参数，dynamic=1 表末读参变参吸收其后全部实参。空 kindexp / any 跳过。
 * XValue 头只携带 array_len 不含多维 shape，故仅校验 kind 层。
 * 不匹配 → 置 TypeError，返回 -1；通过返回 0。 */
int check_read_types(kvspace_t *kv, const char *vtid, const char *pc,
                            const char *opcode, const char *def_sig, int def_nr,
                            int dynamic, kvlangParam_t *args, int nargs) {
    if (def_nr <= 0 || !def_sig || !*def_sig)
        return 0;
    char *dup = strdup(def_sig);
    char *reads[128];
    int rn = 0;
    for (char *s = dup; rn < def_nr && rn < 128;) {
        reads[rn++] = s;
        char *nl = strchr(s, '\n');
        if (!nl)
            break;
        *nl = 0;
        s = nl + 1;
    }
    bool var_last = rn > 0 && dynamic;
    int min_args = var_last ? rn - 1 : rn;
    char *fr = kvlangKeytreeFrameRoot(pc);
    int rc = 0;
    if (nargs < min_args) {
        char msg[256];
        snprintf(msg, sizeof msg, "TypeError: %s expects %d args, got %d",
                 opcode, min_args, nargs);
        kvlangVthreadSetError(kv, vtid, pc, msg);
        rc = -1;
    }
    for (int i = 0; rc == 0 && i < nargs; i++) {
        const char *exp = i < rn ? reads[i] : (var_last ? reads[rn - 1] : NULL);
        if (!exp) {
            char msg[256];
            snprintf(msg, sizeof msg, "TypeError: %s expects %d args, got %d",
                     opcode, rn, nargs);
            kvlangVthreadSetError(kv, vtid, pc, msg);
            rc = -1;
            break;
        }
        if (!exp[0])
            continue; /* 空 kindexp 跳过 */
        /* rwir 参数默认 *T（见 #329 评论「关于五.7 指针」）；剥 * 后按底层类型比对。 */
        char norm[256];
        if (rwir_normalize_param(kv, vtid, pc, opcode, "读参", i + 1,
                            rwir_init_by_value(opcode), exp, norm,
                            sizeof norm) != 0) {
            rc = -1;
            break;
        }
        if (!norm[0] || !kvlangLangtypeValid(norm))
            continue; /* 动态/非法 kindexp 跳过 */
        kvlangXvalue_t v;
        kvlangXvalueZero(&v);
        kvlangBuiltinResolveReadValue(kv, fr, args[i].name, &args[i].value, &v);
        const char *k = kvlangXvalueKind(&v);
        kvlangLangtype kx;
        kvlangLangtypeParse(xh_langtype_of(&v), (int32_t)xh_langtype_len_of(&v), &kx);
        bool ok = kvlangLangtypeMatch(norm, k, kx.ndim, kx.dims);
        char kbuf[40];
        snprintf(kbuf, sizeof kbuf, "%s", k[0] ? k : "None");
        kvlangXvalueFree(&v);
        if (!ok) {
            char msg[256];
            snprintf(msg, sizeof msg,
                     "TypeError: %s arg %d: expected %s, got %s", opcode, i + 1,
                     exp, kbuf);
            kvlangVthreadSetError(kv, vtid, pc, msg);
            rc = -1;
        }
    }
    free(fr);
    free(dup);
    return rc;
}

/* 写参校验：写参恒是「写到哪」= 指针靶，无一是 xvalue 初始化类（已用实际调用形态确认
 * ——`fs·size(p) -> n`、`read_text(p) -> (s,n)` 等写参都绑到局部变量/键），故**永不豁免**。
 * 且写参初值恒为 None（读参只读、写参可写可读、初值 None），值匹配无意义——只校验
 * 「参数默认 *T」这一声明纪律（逐 branch 恰一层指针，见 #329 评论「关于五.7 指针」）。
 * def_sig 为 \n 分隔的写参 kindexp-list。违反 → 置 TypeError，返回 -1。 */
int check_write_types(kvspace_t *kv, const char *vtid, const char *pc,
                             const char *opcode, const char *def_sig, int def_nw) {
    if (def_nw <= 0 || !def_sig || !*def_sig)
        return 0;
    char *dup = strdup(def_sig);
    char *slots[128];
    int wn = 0;
    for (char *s = dup; wn < def_nw && wn < 128;) {
        slots[wn++] = s;
        char *nl = strchr(s, '\n');
        if (!nl)
            break;
        *nl = 0;
        s = nl + 1;
    }
    int rc = 0;
    for (int i = 0; rc == 0 && i < wn; i++) {
        if (!slots[i][0])
            continue;
        char norm[256];
        if (rwir_normalize_param(kv, vtid, pc, opcode, "写参", i + 1, false,
                            slots[i], norm, sizeof norm) != 0)
            rc = -1;
    }
    free(dup);
    return rc;
}

/* 读签名行 [0,x] 槽（def langtype）的 body 为 langtype 串（malloc；无槽返 NULL）。
 * dir 带尾 /；x<0 读参、x>0 写参。 */
static char *read_signature_slot(kvspace_t *kv, const char *dir, int x) {
    char sk[KV_KEY_MAX];
    kvlangKeyFormat(sk, sizeof sk, "%s[0,%d]", dir, x);
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    kvlangKvspaceGetOne(kv, sk, &v);
    char *s = NULL;
    if (!kvlangXvalueNone(&v)) {
        const uint8_t *b = xh_body_of(&v);
        int32_t bl = xh_content_len_of(&v);
        s = malloc((size_t)bl + 1);
        memcpy(s, b, (size_t)bl);
        s[bl] = 0;
    }
    kvlangXvalueFree(&v);
    return s;
}

/* \n 连接 [0,±1..±n] 各 def langtype 槽。dir 带尾 /。malloc 返回。 */
static char *join_signature(kvspace_t *kv, const char *dir, int n, bool neg) {
    if (n <= 0)
        return strdup("");
    char **parts = malloc(sizeof(char *) * (size_t)n);
    if (!parts)
        kvlangDie("out of memory joining signature");
    size_t total = 1;
    for (int i = 1; i <= n; i++) {
        parts[i - 1] = read_signature_slot(kv, dir, neg ? -i : i);
        total += strlen(parts[i - 1] ? parts[i - 1] : "") + (i > 1 ? 1 : 0);
    }
    char *out = malloc(total);
    if (!out)
        kvlangDie("out of memory joining signature");
    size_t off = 0;
    for (int i = 1; i <= n; i++) {
        if (i > 1)
            out[off++] = '\n';
        const char *s = parts[i - 1] ? parts[i - 1] : "";
        size_t sl = strlen(s);
        memcpy(out + off, s, sl);
        off += sl;
        free(parts[i - 1]);
    }
    out[off] = 0;
    free(parts);
    return out;
}

static char *join_read_signature(kvspace_t *kv, const char *dir, int nr) {
    return join_signature(kv, dir, nr, true);
}

static char *join_write_signature(kvspace_t *kv, const char *dir, int nw) {
    return join_signature(kv, dir, nw, false);
}

/* 主槽计数头 [nr:u16 LE][nw:u16 LE][dynamic:u8] → nr/nw/dynamic。无定义返回 -1。 */
static int load_def_counts(kvspace_t *kv, const char *key, int *out_nr,
                           int *out_nw, int *out_dyn) {
    *out_nr = 0;
    *out_nw = 0;
    *out_dyn = 0;
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    kvlangKvspaceGetOne(kv, key, &v);
    if (kvlangXvalueNone(&v)) {
        kvlangXvalueFree(&v);
        return -1;
    }
    const uint8_t *b = xh_body_of(&v);
    int32_t bl = xh_content_len_of(&v);
    if (bl < 5) {
        kvlangXvalueFree(&v);
        return -1;
    }
    *out_nr = b[0] | (b[1] << 8);
    *out_nw = b[2] | (b[3] << 8);
    *out_dyn = b[4];
    kvlangXvalueFree(&v);
    return 0;
}

/* 读取 rwir/rwfunc 定义的读参签名：读参 langtype 逐条落在签名行 [0,-i] 槽（def langtype）。
 * 返回 \n 连接的 kindexp-list（调用方 free）并置 *out_nr；无定义返回 NULL。 */
char *load_def_reads(kvspace_t *kv, const char *key, int *out_nr,
                            int *out_dyn) {
    int nw;
    if (load_def_counts(kv, key, out_nr, &nw, out_dyn) != 0)
        return NULL;
    char dir[KV_KEY_MAX];
    kvlangKeyFormat(dir, sizeof dir, "%s/", key);
    return join_read_signature(kv, dir, *out_nr);
}

/* 读取 rwir 定义的写参签名：写参 langtype 逐条落在签名行 [0,i] 槽（def langtype）。
 * 返回 \n 连接的 kindexp-list（调用方 free）并置 *out_nw；无定义返回 NULL。 */
char *load_def_writes(kvspace_t *kv, const char *key, int *out_nw) {
    int nr, dyn;
    if (load_def_counts(kv, key, &nr, out_nw, &dyn) != 0)
        return NULL;
    char dir[KV_KEY_MAX];
    kvlangKeyFormat(dir, sizeof dir, "%s/", key);
    return join_write_signature(kv, dir, *out_nw);
}
