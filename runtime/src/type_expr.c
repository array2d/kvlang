#include "runtime_internal.h"

/* ── 签名类型表达式（runtime篇-07，修订：无家族简写）──────────────────
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
    return kind_eq(s, len, "bool") ||
           kind_eq(s, len, "int8") || kind_eq(s, len, "int16") ||
           kind_eq(s, len, "int32") || kind_eq(s, len, "int64") ||
           kind_eq(s, len, "uint8") || kind_eq(s, len, "uint16") ||
           kind_eq(s, len, "uint32") || kind_eq(s, len, "uint64") ||
           kind_eq(s, len, "float32") || kind_eq(s, len, "float64") ||
           kind_eq(s, len, "char/utf32") || kind_eq(s, len, "char/utf8") ||
           kind_eq(s, len, "char/ascii") ||
           kind_eq(s, len, "dict") || kind_eq(s, len, "index") ||
           kind_eq(s, len, "extindex") ||
           kind_eq(s, len, "rwir") || kind_eq(s, len, "rwfunc") ||
           kind_eq(s, len, "scope") || kind_eq(s, len, "time") ||
           kind_eq(s, len, "duration");
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

/* atom = [dims] base */
static bool valid_atom(const char *s, size_t len) {
    if (len == 0) return false;
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

/* 类型表达式语法校验（装载期）。 */
bool type_expr_valid(const char *expr) {
    if (!expr || !*expr) return false;
    const char *p = expr;
    for (;;) {
        const char *pipe = strchr(p, '|');
        size_t len = pipe ? (size_t)(pipe - p) : strlen(p);
        if (!valid_atom(p, len)) return false;
        if (!pipe) break;
        p = pipe + 1;
        if (*p == '\0') return false;   /* 尾随 '|' → 空 atom */
    }
    return true;
}

/* any/kind 判定（kind 为运行时实际落盘 kind 串）。 */
static bool base_match(const char *s, size_t len, const char *kind) {
    if (len == 3 && strncmp(s, "any", 3) == 0) return true;
    return kind_eq(s, len, kind);
}

/* match_shape：shape="" → ndim>=1；否则维数须一致且逐维 ?（跳过）或精确相等。 */
static bool match_shape(const char *s, size_t len, int32_t ndim, const int32_t *dims) {
    if (len == 0) return ndim >= 1;
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

/* match_atom：ndim = -1 表示「已消费 dims，不再判 ndim」（递归哨兵）。 */
static bool match_atom(const char *s, size_t len, const char *kind, int32_t ndim, const int32_t *dims) {
    if (s[0] == '[') {
        const char *end = memchr(s, ']', len);
        if (!end) return false;
        if (!match_shape(s + 1, (size_t)(end - s - 1), ndim, dims)) return false;
        return match_atom(end + 1, (size_t)(s + len - end - 1), kind, -1, NULL);
    }
    if (ndim >= 0 && ndim != 0) return false;   /* 标量 */
    return base_match(s, len, kind);
}

/* 值（kind/ndim/dims）是否匹配类型表达式：任一 atom 命中即 true。 */
bool type_expr_match(const char *expr, const char *kind, int32_t ndim, const int32_t *dims) {
    if (!expr || !kind) return false;
    const char *p = expr;
    while (p && *p) {
        const char *pipe = strchr(p, '|');
        size_t len = pipe ? (size_t)(pipe - p) : strlen(p);
        if (match_atom(p, len, kind, ndim, dims)) return true;
        p = pipe ? pipe + 1 : NULL;
    }
    return false;
}
