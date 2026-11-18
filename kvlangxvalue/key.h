/* key.h — kvspace 键名：坐标段判定、比较与排序。
 *
 * 存储层按后端顺序返回名字（`kvspaceList` 不排序）；需要稳定顺序的调用方
 * （CLI、dump、diff）一律走这里，不各写一套——两套比较器迟早对「小数坐标 /
 * 字符串坐标 / 目录尾斜杠」这类边角判不同，症状是「同一个目录，两个算子取到
 * 不同的第 i 项」。
 *
 * 排序规则：坐标段按数值比（`[2]` 在 `[10]` 前），非坐标段按字典序排在其后。
 * 键名语义不依赖任何值的构造，所以本头只依赖 libc。
 */

#ifndef KVLANG_XVALUE_KEY_H
#define KVLANG_XVALUE_KEY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* `[1]`、`[1,2]` 形态（方括号包裹、内部无嵌套方括号）。 */
static inline int kvlangCoordIsCoord(const char *name) {
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

/* 解析整数坐标；不匹配返回 -1。 */
static inline int kvlangParseCoord(const char *name, int64_t *coords, int maxn) {
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

/* 坐标序比较：负数 a<b，0 相等，正数 a>b。 */
static inline int kvlangCoordCmp(const char *a, const char *b) {
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

/* qsort 的比较器：把 const char ** 解成两个键名，交给唯一比较器。 */
static inline int kvlangKeyOrder(const void *left, const void *right) {
    const char *left_name = *(const char *const *)left;
    const char *right_name = *(const char *const *)right;
    return kvlangCoordCmp(left_name, right_name);
}

/* 原地排序 count 个键名指针。names 由调用方持有，本函数不分配、不释放名字。 */
static inline void kvlangKeySort(char **names, size_t count) {
    if (names != NULL && count > 1) {
        qsort(names, count, sizeof *names, kvlangKeyOrder);
    }
}

#endif
