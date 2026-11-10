/* 坐标段工具：`[1,2]` 这类成员名的判定与比较。
 *
 * 这是**唯一一份** `cmp_coord`。存储层（`kvspaceList`）按契约返回后端顺序、不排序；
 * 需要坐标序的调用方（runtimec / layout / CLI）一律走这里，不各写一套——两套比较器
 * 迟早对「小数坐标 / 字符串坐标 / 目录尾斜杠」这类边角判不同，症状是「同一个目录，
 * 两个算子取到不同的第 i 项」。 */
#include <kvlang/xvalue.h>

#include <stdbool.h>
#include <string.h>

int kvlangCoordIsCoord(const char *name) {
    if (!name || name[0] != '[')
        return 0;
    size_t n = strlen(name);
    if (n < 3 || name[n - 1] != ']')
        return 0;
    for (size_t i = 1; i + 1 < n; i++)
        if (name[i] == '[' || name[i] == ']')
            return 0;
    return 1;
}

/* Parse integer coordinates; return -1 on mismatch. */
int kvlangParseCoord(const char *name, int64_t *coords, int maxn) {
    if (!name || name[0] != '[')
        return -1;
    int n = 0;
    int64_t cur = 0;
    bool has = false;
    for (const char *p = name + 1; *p; p++) {
        if (*p >= '0' && *p <= '9') {
            cur = cur * 10 + (*p - '0');
            has = true;
        } else if (*p == ',') {
            if (!has || n >= maxn)
                return -1;
            coords[n++] = cur;
            cur = 0;
            has = false;
        } else if (*p == ']') {
            if (!has || n >= maxn)
                return -1;
            coords[n++] = cur;
            return (p[1] == '\0') ? n : -1;
        } else {
            return -1;
        }
    }
    return -1;
}

int kvlangCoordCmp(const char *a, const char *b) {
    int ia = kvlangCoordIsCoord(a), ib = kvlangCoordIsCoord(b);
    if (!ia && !ib)
        return strcmp(a, b);
    if (!ia)
        return 1; /* 非坐标段排后 */
    if (!ib)
        return -1;
    int64_t ca[8], cb[8];
    int na = kvlangParseCoord(a, ca, 8);
    int nb = kvlangParseCoord(b, cb, 8);
    if (na < 0 || nb < 0)
        return strcmp(a, b); /* 含小数/字符串坐标 → 字典序 */
    for (int i = 0; i < na && i < nb; i++) {
        if (ca[i] != cb[i])
            return ca[i] < cb[i] ? -1 : 1;
    }
    if (na != nb)
        return na < nb ? -1 : 1;
    return 0;
}
