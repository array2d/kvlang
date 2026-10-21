#include "runtime_internal.h"

struct kvlangArenaBlock {
    kvlangArenaBlock *next;
    size_t cap, used;
    uint8_t data[];
};

kvlangArena_t *kvlangCurrentArena = NULL;

#define ARENA_BLOCK_MIN (64u * 1024u)

static kvlangArenaBlock *block_new(size_t need) {
    size_t cap = need > ARENA_BLOCK_MIN ? need : ARENA_BLOCK_MIN;
    kvlangArenaBlock *b = malloc(sizeof *b + cap);
    if (!b)
        kvlangDie("out of memory allocating arena block");
    b->next = NULL;
    b->cap = cap;
    b->used = 0;
    return b;
}

void kvlangArenaInit(kvlangArena_t *a) {
    memset(a, 0, sizeof *a);
    a->head = block_new(0);
}

void kvlangArenaReset(kvlangArena_t *a) {
    for (size_t i = 0; i < a->nowned; i++)
        free(a->owned[i]);
    a->nowned = 0;
    kvlangArenaBlock *b = a->head->next;
    while (b) {
        kvlangArenaBlock *n = b->next;
        free(b);
        b = n;
    }
    a->head->next = NULL;
    a->head->used = 0;
}

void kvlangArenaDestroy(kvlangArena_t *a) {
    if (!a || !a->head)
        return;
    kvlangArenaReset(a);
    free(a->head);
    free(a->owned);
    memset(a, 0, sizeof *a);
}

void *kvlangArenaAlloc(kvlangArena_t *a, size_t n) {
    if (!a)
        return malloc(n ? n : 1);
    n = (n + 15) & ~(size_t)15;
    if (n == 0)
        n = 16;
    kvlangArenaBlock *b = a->head;
    while (b && b->used + n > b->cap)
        b = b->next;
    if (!b) {
        kvlangArenaBlock *last = a->head;
        while (last->next)
            last = last->next;
        b = block_new(n);
        last->next = b;
    }
    void *p = b->data + b->used;
    b->used += n;
    return p;
}

char *kvlangArenaStrdup(kvlangArena_t *a, const char *s) {
    size_t n = strlen(s) + 1;
    char *p = kvlangArenaAlloc(a, n);
    memcpy(p, s, n);
    return p;
}

void *kvlangArenaOwn(kvlangArena_t *a, void *p) {
    if (!a || !p)
        return p;
    if (a->nowned == a->capowned) {
        size_t nc = a->capowned ? a->capowned * 2 : 16;
        a->owned = realloc(a->owned, nc * sizeof *a->owned);
        if (!a->owned)
            kvlangDie("out of memory growing arena ownership table");
        a->capowned = nc;
    }
    a->owned[a->nowned++] = p;
    return p;
}

void kvlangXvalueCopyToArena(kvlangXvalue_t *v) {
    if (!v->bytes || !v->length)
        return;
    uint8_t *p = kvlangArenaAlloc(kvlangCurrentArena, v->length);
    memcpy(p, v->bytes, v->length);
    v->bytes = p;
}
