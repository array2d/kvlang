
#ifndef KVLANG_XVALUE_KEY_H
#define KVLANG_XVALUE_KEY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

static inline int kvlangCoordCmp(const char *a, const char *b) {
    int ia = kvlangCoordIsCoord(a), ib = kvlangCoordIsCoord(b);
    if (!ia && !ib)
        return strcmp(a, b);
    if (!ia)
        return 1;
    if (!ib)
        return -1;
    int64_t ca[8], cb[8];
    int na = kvlangParseCoord(a, ca, 8);
    int nb = kvlangParseCoord(b, cb, 8);
    if (na < 0 || nb < 0)
        return strcmp(a, b);
    for (int i = 0; i < na && i < nb; i++) {
        if (ca[i] != cb[i])
            return ca[i] < cb[i] ? -1 : 1;
    }
    if (na != nb)
        return na < nb ? -1 : 1;
    return 0;
}

static inline int kvlangKeyOrder(const void *left, const void *right) {
    const char *left_name = *(const char *const *)left;
    const char *right_name = *(const char *const *)right;
    return kvlangCoordCmp(left_name, right_name);
}

static inline void kvlangKeySort(char **names, size_t count) {
    if (names != NULL && count > 1) {
        qsort(names, count, sizeof *names, kvlangKeyOrder);
    }
}

#endif
