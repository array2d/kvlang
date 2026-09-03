#include "runtime_internal.h"

/* ── 签名 kindexpr（runtime篇-07，修订：无家族简写）──────────────────
 * type   = atom ("|" atom)*
 * atom   = [dims] ( any | kind )
 * dims   = "[]" | "[" dim ("," dim)* "]"
 * dim    = integer | "?"
 * any    = "any"           # 通配，匹配任意 kind
 * kind   = 精确 kind 串    # 见 known_kind
 *
 * 铁律：不提供 int/uint/float/num 数值家族（位宽开放集合，int4/fp8/fp16…），
 * 也不提供 char 编码简写（编码须写明确，如 char/utf8、char/utf32）。
 * 多态靠显式 "|" 枚举（如 int8|int16|int32|int64）。
 */

static bool kind_eq(const char *s, size_t len, const char *k) {
    size_t kl = strlen(k);
    return len == kl && memcmp(s, k, len) == 0;
}

/* 精确 kind 集合（对齐 runtime kind 常量，不含 None）。 */
static bool known_kind(const char *s, size_t len) {
    return kind_eq(s, len, KVSPACE_KIND_BOOL) ||
           kind_eq(s, len, KVSPACE_KIND_INT8) || kind_eq(s, len, KVSPACE_KIND_INT16) ||
           kind_eq(s, len, KVSPACE_KIND_INT32) || kind_eq(s, len, KVSPACE_KIND_INT64) ||
           kind_eq(s, len, KVSPACE_KIND_UINT8) || kind_eq(s, len, KVSPACE_KIND_UINT16) ||
           kind_eq(s, len, KVSPACE_KIND_UINT32) || kind_eq(s, len, KVSPACE_KIND_UINT64) ||
           kind_eq(s, len, KVSPACE_KIND_FLOAT32) || kind_eq(s, len, KVSPACE_KIND_FLOAT64) ||
           kind_eq(s, len, KVSPACE_KIND_CHAR) || kind_eq(s, len, KVSPACE_KIND_CHAR_UTF8) ||
           kind_eq(s, len, KVSPACE_KIND_CHAR_ASCII) ||
           kind_eq(s, len, KVSPACE_KIND_OBJ) || kind_eq(s, len, KVSPACE_KIND_MAP) ||
           kind_eq(s, len, KVSPACE_KIND_INDEX) || kind_eq(s, len, KVSPACE_KIND_EXT_INDEX) ||
           kind_eq(s, len, KVSPACE_KIND_RWIR) || kind_eq(s, len, KVSPACE_KIND_RWFUNC) ||
           kind_eq(s, len, KVSPACE_KIND_SCOPE) || kind_eq(s, len, KVSPACE_KIND_STRUCT) ||
           kind_eq(s, len, KVSPACE_KIND_TIME) ||
           kind_eq(s, len, KVSPACE_KIND_DURATION);
}

/* structref = "/" path：指向 /lib 下 struct 定义节点的完整路径（实例 kind / 字段类型）。
 * 仅语法承认（`/` + 合法路径段），存在性/字段一致性留给 runtime 判定。 */
static bool valid_structref(const char *s, size_t len) {
    if (len < 2 || s[0] != '/') return false;
    size_t seg = 0;
    for (size_t i = 1; i < len; i++) {
        if (s[i] == '/') {
            if (seg == 0) return false;
            seg = 0;
            continue;
        }
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_'))
            return false;
        seg++;
    }
    return seg > 0;
}

/* base = any | kind（kind 为精确合法 kind 串） */
static bool valid_base(const char *s, size_t len) {
    if (len == 0) return false;
    if (len == 3 && strncmp(s, "any", 3) == 0) return true;
    return known_kind(s, len);
}

static bool valid_dim(const char *s, size_t len) {
    if (len == 0) return false;
    if (len == 1 && s[0] == '?') return true;
    for (size_t i = 0; i < len; i++)
        if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

/* dims = ε（空 [] = 1 维任意，等价 [?]） | dim ("," dim)* */
static bool valid_dims(const char *s, size_t len) {
    if (len == 0) return true;
    const char *p = s, *end = s + len;
    while (p < end) {
        const char *comma = memchr(p, ',', (size_t)(end - p));
        size_t seg = comma ? (size_t)(comma - p) : (size_t)(end - p);
        if (!valid_dim(p, seg)) return false;
        p += seg + (comma ? 1 : 0);
    }
    return true;
}

/* atom = [dims] base | structref */
static bool valid_atom(const char *s, size_t len) {
    if (len == 0) return false;
    if (s[0] == '/') return valid_structref(s, len);
    const char *p = s;
    if (*p == '[') {
        const char *end = memchr(p, ']', len);
        if (!end) return false;
        if (!valid_dims(p + 1, (size_t)(end - p - 1))) return false;
        p = end + 1;
        if (p >= s + len) return false;   /* 缺 base */
    }
    return valid_base(p, (size_t)(s + len - p));
}

/* 末参变参标记：A:any... 表 0..N 个同型实参。 */
bool kvlang_rwirextKindexprVariadic(const char *expr) {
    if (!expr) return false;
    size_t n = strlen(expr);
    return n >= 3 && memcmp(expr + n - 3, "...", 3) == 0;
}

/* 去掉尾缀 "..." 后的有效长度。 */
static size_t effective_len(const char *expr) {
    size_t n = strlen(expr);
    return (n >= 3 && memcmp(expr + n - 3, "...", 3) == 0) ? n - 3 : n;
}

/* 类型表达式语法校验（装载期）。允许末参尾缀 "..." 变参。 */
bool kvlang_rwirextKindexprValid(const char *expr) {
    if (!expr || !*expr) return false;
    size_t total = effective_len(expr);
    if (total == 0) return false;
    const char *p = expr;
    const char *end = expr + total;
    for (;;) {
        const char *pipe = memchr(p, '|', (size_t)(end - p));
        size_t len = pipe ? (size_t)(pipe - p) : (size_t)(end - p);
        if (!valid_atom(p, len)) return false;
        if (!pipe) break;
        p = pipe + 1;
        if (p >= end) return false;   /* 尾随 '|' → 空 atom */
    }
    return true;
}

/* any/kind 判定（kind 为运行时实际落盘 kind 串）。 */
static bool base_match(const char *s, size_t len, const char *kind) {
    if (len == 3 && strncmp(s, "any", 3) == 0) return true;
    return kind_eq(s, len, kind);
}

/* match_shape：shape="" 等价 "[?]" → 恰一维（ndim==1）；否则维数须一致且逐维 ?（跳过）或精确相等。 */
static bool match_shape(const char *s, size_t len, int32_t ndim, const int32_t *dims) {
    if (len == 0) return ndim == 1;
    int count = 1;
    for (size_t i = 0; i < len; i++) if (s[i] == ',') count++;
    if (count != ndim) return false;
    const char *p = s, *end = s + len;
    int i = 0;
    while (p < end && i < ndim) {
        const char *comma = memchr(p, ',', (size_t)(end - p));
        size_t seg = comma ? (size_t)(comma - p) : (size_t)(end - p);
        if (!(seg == 1 && p[0] == '?')) {
            long v = 0;
            for (size_t j = 0; j < seg; j++) v = v * 10 + (p[j] - '0');
            if (v != dims[i]) return false;
        }
        p += seg + (comma ? 1 : 0);
        i++;
    }
    return true;
}

/* match_atom：裸 kind 表单值（shape=[1]，即 ndim==0）；[dims] 前缀强制 shape。
 * 数组/字符串须显式 []kind（[]⟺[?] 一维）或 [n]kind。 */
static bool match_atom(const char *s, size_t len, const char *kind, int32_t ndim, const int32_t *dims) {
    if (len == 3 && strncmp(s, "any", 3) == 0) return true;   /* any：顶类型，任意 kind + 任意 shape */
    if (s[0] == '[') {
        const char *end = memchr(s, ']', len);
        if (!end) return false;
        if (!match_shape(s + 1, (size_t)(end - s - 1), ndim, dims)) return false;
        return base_match(end + 1, (size_t)(s + len - end - 1), kind);
    }
    return base_match(s, len, kind) && ndim == 0;
}

/* 单值（kind/ndim/dims）是否匹配类型表达式：任一 atom 命中即 true。
 * 变参 "..." 按单元素判定（去尾缀后匹配），重复由派发循环处理。 */
bool kvlang_rwirextKindexprMatch(const char *expr, const char *kind, int32_t ndim, const int32_t *dims) {
    if (!expr || !kind) return false;
    const char *end = expr + effective_len(expr);
    const char *p = expr;
    while (p < end) {
        const char *pipe = memchr(p, '|', (size_t)(end - p));
        size_t len = pipe ? (size_t)(pipe - p) : (size_t)(end - p);
        if (match_atom(p, len, kind, ndim, dims)) return true;
        if (!pipe) break;
        p = pipe + 1;
    }
    return false;
}

