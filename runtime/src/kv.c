#include "runtime_internal.h"

/* kv 访问统一走 kvspace-durable 兼容 C ABI（kvspace*）。
 * 后端由链接的 kvspace 库决定（kvspace-durable / kvspace-c 均导出同一 ABI）。 */

static kvlangRefEnt_t *ref_find(kvlangKv_t *k, const char *key) {
    for (int i = 0; i < k->nref; i++) {
        if (k->ref[i].key && strcmp(k->ref[i].key, key) == 0)
            return &k->ref[i];
    }
    return NULL;
}

static void ref_put(kvlangKv_t *k, const char *key, const kvspaceRef_t *r) {
    kvlangRefEnt_t *e = ref_find(k, key);
    if (!e) {
        if (k->nref < KVLANG_REF_CAP) e = &k->ref[k->nref++];
        else { e = &k->ref[0]; free(e->key); }
        e->key = strdup(key);
    }
    e->block_id = r->block_id;
    e->gen = r->gen;
}

static int ref_ok(kvlangKv_t *k) {
    return k->ref_on && kvspaceResolveRef && kvspaceGetByRef;
}

static kvlangRefEnt_t *pref_find(kvlangKv_t *k, const char *dir, size_t dl) {
    for (int i = 0; i < k->npref; i++) {
        const char *pk = k->pref[i].key;
        if (pk && k->pref[i].gen && k->pref[i].klen == dl && memcmp(pk, dir, dl) == 0)
            return &k->pref[i];
    }
    return NULL;
}

static void hot_clear(kvlangKv_t *k) {
    for (int i = 0; i < k->nhot; i++) {
        free(k->hot[i].name);
        free(k->hot[i].key);
        k->hot[i].name = k->hot[i].key = NULL;
        k->hot[i].block_id = k->hot[i].gen = k->hot[i].dlen = 0;
    }
    k->nhot = 0;
}

static int hot_name_ok(const char *name) {
    char c;
    if (!name || name[1] != 0)
        return 0;
    c = name[0];
    return c == 'a' || c == 'i' || c == 'n';
}

static inline int hot_get(kvlangKv_t *k, const char *dir, const char *name,
                   uint8_t **d, uint32_t *len) {
    if (!dir || !hot_name_ok(name))
        return 0;
    for (int i = 0; i < k->nhot; i++) {
        uint32_t dl;
        if (!k->hot[i].name || k->hot[i].name[0] != name[0] || k->hot[i].name[1] != 0)
            continue;
        dl = k->hot[i].dlen;
        if (!k->hot[i].key || strncmp(dir, k->hot[i].key, dl) != 0 || dir[dl] != 0)
            continue;
        kvspaceRef_t r = { k->hot[i].block_id, k->hot[i].gen, 0, 0 };
        if (kvspaceGetByRef(k->h, &r, k->hot[i].key, d, len) == 0 && *d && *len > 0) {
            k->hot[i].block_id = r.block_id;
            k->hot[i].gen = r.gen;
            return 1;
        }
    }
    return 0;
}

/* Leaf-only (gen==0), one-char names, skip /lib/. Parent refs stick GetMember on a walk. */
static void hot_put(kvlangKv_t *k, const char *name, const char *key,
                    uint32_t block_id, uint32_t gen) {
    kvlangHotEnt_t *e;
    size_t nl, kl;
    if (!hot_name_ok(name) || !key || !block_id || gen != 0)
        return;
    if (strncmp(key, "/lib/", 5) == 0)
        return;
    nl = 1;
    kl = strlen(key);
    if (kl < nl)
        return;
    for (int i = 0; i < k->nhot; i++) {
        if (k->hot[i].name && k->hot[i].name[0] == name[0] && k->hot[i].name[1] == 0) {
            if (k->hot[i].key && strcmp(k->hot[i].key, key) == 0) {
                k->hot[i].block_id = block_id;
                k->hot[i].gen = gen;
                return;
            }
            /* Same name, other frame: keep the first key; `n` may use a second slot. */
            if (name[0] != 'n' || k->nhot >= KVLANG_HOT_CAP)
                return;
            break;
        }
    }
    if (k->nhot < KVLANG_HOT_CAP)
        e = &k->hot[k->nhot++];
    else {
        e = &k->hot[KVLANG_HOT_CAP - 1];
        free(e->name);
        free(e->key);
    }
    e->name = strdup(name);
    e->key = strdup(key);
    if (!e->name || !e->key) {
        free(e->name);
        free(e->key);
        e->name = e->key = NULL;
        if (k->nhot > 0 && e == &k->hot[k->nhot - 1])
            k->nhot--;
        return;
    }
    e->block_id = block_id;
    e->gen = gen;
    e->dlen = (uint32_t)(kl - nl);
}

static inline kvlangHotEnt_t *hot_find_key(kvlangKv_t *k, const char *key) {
    if (!key)
        return NULL;
    for (int i = 0; i < k->nhot; i++) {
        if (k->hot[i].key && k->hot[i].gen == 0 &&
            strcmp(k->hot[i].key, key) == 0)
            return &k->hot[i];
    }
    return NULL;
}

static void parent_clear(kvlangKv_t *k) {
    for (int i = 0; i < k->npref; i++)
        free(k->pref[i].key);
    k->npref = 0;
    k->pref_i = 0;
    free(k->fpar.key);
    k->fpar.key = NULL;
    k->fpar.block_id = k->fpar.gen = k->fpar.klen = 0;
    hot_clear(k);
}

static int dir_is_member(const char *dir, size_t dl) {
    return dl >= MEMBER_SEP_LEN &&
           (unsigned char)dir[dl - 2] == 0xC2 &&
           (unsigned char)dir[dl - 1] == 0xB7;
}

static void parent_put(kvlangKv_t *k, const char *dir, size_t dl, const kvspaceRef_t *rr) {
    kvlangRefEnt_t *e;
    if (!dir || !dl || !rr->parent_id || !rr->depth)
        return;
    /* Stdlib `/lib/…` would occupy a slot for the whole process. */
    if (dl >= 5 && memcmp(dir, "/lib/", 5) == 0)
        return;
    if (!dir_is_member(dir, dl)) {
        if (k->fpar.key && (k->fpar.klen != dl || memcmp(k->fpar.key, dir, dl) != 0)) {
            free(k->fpar.key);
            k->fpar.key = NULL;
        }
        if (!k->fpar.key) {
            k->fpar.key = malloc(dl + 1);
            if (!k->fpar.key)
                return;
            memcpy(k->fpar.key, dir, dl);
            k->fpar.key[dl] = 0;
        }
        k->fpar.block_id = rr->parent_id;
        k->fpar.gen = rr->depth;
        k->fpar.klen = (uint32_t)dl;
        return;
    }
    e = pref_find(k, dir, dl);
    if (!e) {
        if (k->npref < KVLANG_PREF_CAP)
            e = &k->pref[k->npref++];
        else {
            e = &k->pref[k->pref_i];
            free(e->key);
            k->pref_i = (k->pref_i + 1) % KVLANG_PREF_CAP;
        }
        e->key = malloc(dl + 1);
        if (!e->key)
            return;
        memcpy(e->key, dir, dl);
        e->key[dl] = 0;
        e->klen = (uint32_t)dl;
    }
    e->block_id = rr->parent_id;
    e->gen = rr->depth;
    e->klen = (uint32_t)dl;
}

/* Rightmost '/' or member '·'; only the last path component is scanned for '·'. */
static int last_dir_sep(const char *key, size_t *seplen) {
    const char *slash = strrchr(key, '/');
    const char *start = slash ? slash + 1 : key;
    const char *mid = NULL;
    for (const char *p = start; *p; p++) {
        if ((unsigned char)p[0] == 0xC2 && (unsigned char)p[1] == 0xB7)
            mid = p;
    }
    if (mid) {
        *seplen = MEMBER_SEP_LEN;
        return (int)(mid - key);
    }
    if (slash && slash > key) {
        *seplen = 1;
        return (int)(slash - key);
    }
    *seplen = 0;
    return -1;
}

static int parent_prefix_ok(const kvlangRefEnt_t *e, const char *key) {
    size_t dl;
    const char *p;
    if (!e || !e->key || !e->gen || !key)
        return 0;
    dl = e->klen ? e->klen : strlen(e->key);
    if (memcmp(key, e->key, dl) != 0 || !key[dl])
        return 0;
    for (p = key + dl; *p; p++) {
        if (*p == '/' ||
            ((unsigned char)p[0] == 0xC2 && (unsigned char)p[1] == 0xB7))
            return 0;
    }
    return 1;
}

/* Covering `·` map parent, or NULL. Does not use the sticky frame `/` slot. */
static kvlangRefEnt_t *pref_cover(kvlangKv_t *k, const char *key) {
    if (!key)
        return NULL;
    for (int i = 0; i < k->npref; i++) {
        if (parent_prefix_ok(&k->pref[i], key)) {
            if (i != 0) {
                kvlangRefEnt_t tmp = k->pref[0];
                k->pref[0] = k->pref[i];
                k->pref[i] = tmp;
            }
            return &k->pref[0];
        }
    }
    return NULL;
}

static int parent_hit_ent(kvlangKv_t *k, const kvlangRefEnt_t *e, const char *key,
                          uint8_t **d, uint32_t *len) {
    if (!parent_prefix_ok(e, key))
        return 0;
    {
        kvspaceRef_t r = { e->block_id, e->gen, 0, 0 };
        return kvspaceGetByRef(k->h, &r, key, d, len) == 0 && *d && *len > 0;
    }
}

static int parent_hit(kvlangKv_t *k, const char *key, uint8_t **d, uint32_t *len) {
    return parent_hit_ent(k, pref_cover(k, key), key, d, len);
}

void kvlangKvInvalidateFrame(kvlangKv_t *k, const char *fr) {
    if (!k || !k->ref_on || !fr || !fr[0]) return;
    size_t n = strlen(fr);
    int w = 0;
    for (int i = 0; i < k->nref; i++) {
        char *key = k->ref[i].key;
        if (key && strncmp(key, fr, n) == 0 && (key[n] == 0 || key[n] == '/')) {
            free(key);
            continue;
        }
        if (w != i) k->ref[w] = k->ref[i];
        w++;
    }
    k->nref = w;
    {
        int pw = 0;
        for (int i = 0; i < k->npref; i++) {
            char *key = k->pref[i].key;
            if (key && strncmp(key, fr, n) == 0 && (key[n] == 0 || key[n] == '/')) {
                free(key);
                continue;
            }
            if (pw != i)
                k->pref[pw] = k->pref[i];
            pw++;
        }
        k->npref = pw;
        if (k->pref_i >= k->npref)
            k->pref_i = 0;
    }
    if (k->fpar.key && strncmp(k->fpar.key, fr, n) == 0 &&
        (k->fpar.key[n] == 0 || k->fpar.key[n] == '/')) {
        free(k->fpar.key);
        k->fpar.key = NULL;
        k->fpar.block_id = k->fpar.gen = k->fpar.klen = 0;
    }
    {
        int hw = 0;
        for (int i = 0; i < k->nhot; i++) {
            char *key = k->hot[i].key;
            if (key && strncmp(key, fr, n) == 0 && (key[n] == 0 || key[n] == '/')) {
                free(k->hot[i].name);
                free(key);
                continue;
            }
            if (hw != i)
                k->hot[hw] = k->hot[i];
            hw++;
        }
        k->nhot = hw;
    }
}

kvlangKv_t *kvlangKvConnect(const char *dsn) {
    kvlangKv_t *k = calloc(1, sizeof(*k));
    k->h = kvspaceConnect(dsn);
    if (!k->h) {
        free(k);
        return NULL;
    }
    k->ref_on = 1;
    return k;
}

void kvlangKvDisconnect(kvlangKv_t *k) {
    if (!k)
        return;
    for (int i = 0; i < k->nref; i++) free(k->ref[i].key);
    parent_clear(k);
    if (k->h)
        kvspaceClose(k->h);
    free(k);
}

/* 借用读（resolve=0，raw）：out 直接借 kvspace 常驻/借用池指针（borrowed=1，不 free、不入
 * kvlangKvSet 前不跨写）。空值 → out len=0。 */
int kvlangKvGetOne(kvlangKv_t *k, const char *key, kvlangXvalue_t *out) {
    kvlangXvalueZero(out);
    uint8_t *d;
    uint32_t len;
    /* kv.get map slots (`base·k`); frame locals go through GetMember. */
    if (ref_ok(k) && k->npref && key) {
        kvlangRefEnt_t *pe = (k->npref == 1)
                                 ? (parent_prefix_ok(&k->pref[0], key) ? &k->pref[0] : NULL)
                                 : pref_cover(k, key);
        if (pe) {
            kvspaceRef_t r = { pe->block_id, pe->gen, 0, 0 };
            if (kvspaceGetByRef(k->h, &r, key, &d, &len) == 0 && d && len > 0) {
                out->data = d;
                out->len = len;
                out->borrowed = 1;
                return 0;
            }
        }
    }
    if (kvspaceGet(k->h, key, 0, &d, &len) != 0)
        return -1;
    if (d && len > 0) {
        out->data = d;
        out->len = len;
        out->borrowed = 1;
    }
    return 0;
}

/* Frame member: dir 直连 name 组键，借用读（resolve=0：拿 Ptr 本体，不穿透 link——
 * 解引用由 runtime 显式按 target 形态判别，见 ResolveReadValue/ResolveWriteSlot）；
 * 空值 → out len=0。 */
int kvlangKvGetMember(kvlangKv_t *k, const char *dir, const char *name, kvlangXvalue_t *out) {
    kvlangXvalueZero(out);
    if (!name || !name[0])
        return 0;
    uint8_t *d;
    uint32_t len;
    if (ref_ok(k) && dir && k->nhot && hot_get(k, dir, name, &d, &len)) {
        out->data = d;
        out->len = len;
        out->borrowed = 1;
        return 0;
    }
    size_t dl = strlen(dir), nl = strlen(name);
    char stack[256];
    char *heap = NULL;
    char *key = stack;
    if (dl + nl + 1 > sizeof stack) {
        heap = malloc(dl + nl + 1);
        if (!heap)
            return -1;
        key = heap;
    }
    memcpy(key, dir, dl);
    memcpy(key + dl, name, nl);
    key[dl + nl] = 0;
    /* Non-a/i/n frame siblings: ART parent before the 64-slot leaf scan. */
    if (ref_ok(k) && !hot_name_ok(name) && k->fpar.key &&
        memcmp(k->fpar.key, dir, k->fpar.klen) == 0 && dir[k->fpar.klen] == 0) {
        kvspaceRef_t r = { k->fpar.block_id, k->fpar.gen, 0, 0 };
        if (kvspaceGetByRef(k->h, &r, key, &d, &len) == 0 && d && len > 0) {
            out->data = d;
            out->len = len;
            out->borrowed = 1;
            free(heap);
            return 0;
        }
    }
    kvlangRefEnt_t *e = ref_ok(k) ? ref_find(k, key) : NULL;
    if (e) {
        kvspaceRef_t r = { e->block_id, e->gen, 0, 0 };
        if (kvspaceGetByRef(k->h, &r, key, &d, &len) == 0 && d && len > 0) {
            e->block_id = r.block_id;
            e->gen = r.gen;
            hot_put(k, name, key, r.block_id, r.gen);
            out->data = d;
            out->len = len;
            out->borrowed = 1;
            free(heap);
            return 0;
        }
    }
    if (ref_ok(k)) {
        int hit = 0;
        if (nl >= MEMBER_SEP_LEN && memchr(name, 0xC2, nl)) {
            if (k->npref)
                hit = parent_hit(k, key, &d, &len);
        } else if (hot_name_ok(name) && k->fpar.key &&
                   memcmp(k->fpar.key, dir, k->fpar.klen) == 0 && dir[k->fpar.klen] == 0) {
            kvspaceRef_t r = { k->fpar.block_id, k->fpar.gen, 0, 0 };
            hit = kvspaceGetByRef(k->h, &r, key, &d, &len) == 0 && d && len > 0;
        }
        if (hit) {
            out->data = d;
            out->len = len;
            out->borrowed = 1;
            free(heap);
            return 0;
        }
    }
    if (kvspaceGet(k->h, key, 0, &d, &len) == 0 && d && len > 0) {
        out->data = d;
        out->len = len;
        out->borrowed = 1;
    }
    free(heap);
    return 0;
}

/* 指令边界回收读借用池：VM 每条指令末调一次。 */
void kvlangKvReadReset(kvlangKv_t *k) { kvspaceReadReset(k->h); }

/* 定位读：借用读 key 值的 [off, off+len) 字节 → out(borrowed=1)。空/越界 → out len=0。 */
int kvlangKvGetPart(kvlangKv_t *k, const char *key, uint32_t off, uint32_t len, kvlangXvalue_t *out) {
    kvlangXvalueZero(out);
    uint8_t *d;
    uint32_t got;
    if (kvspaceGetPart(k->h, key, off, len, &d, &got) != 0)
        return -1;
    if (d && got > 0) {
        out->data = d;
        out->len = got;
        out->borrowed = 1;
    }
    return 0;
}

/* 定位写：就地写 buf 到 key 值的 [off, off+buf_len)（key 须已存在、不改结构）。 */
int kvlangKvSetPart(kvlangKv_t *k, const char *key, uint32_t off, const uint8_t *buf, uint32_t buf_len,
                     char *err, uint32_t err_cap) {
    return kvspaceSetPart(k->h, key, off, buf, buf_len, err, err_cap);
}

/* 读 head：只读值前缀解码三正交轴 head（不取 body）。空/不存在 → 非 0。 */
int kvlangKvGetHead(kvlangKv_t *k, const char *key, kvspaceHead_t *out) {
    return kvspaceGetHead(k->h, key, out);
}

/* 写即构造：逐条解 head 取 (langtype, body)——同 body_len 就地(WriteInPlace)，否则新位置
 * (WriteNewPlace)——向 kvspace 要 body 偏移指针后直接写字节，无预合并缓冲。 */
int kvlangKvSet(kvlangKv_t *k, const kvlangKvPair_t *pairs, int n, char *err, uint32_t err_cap) {
    /* 借用值全程有效：durable 惰性写不再清读池，读借用池由 VM 在指令边界统一 ReadReset 回收，
     * 故写期直接用 v->data，不再需要防御性快照。 */
    int rc = 0;
    if (n == 1 && ref_ok(k) && kvspaceSetPartByRef && pairs[0].key && pairs[0].val.data &&
        pairs[0].val.len) {
        const char *key = pairs[0].key;
        kvlangHotEnt_t *he = k->nhot ? hot_find_key(k, key) : NULL;
        if (he) {
            kvspaceRef_t r = { he->block_id, he->gen, 0, 0 };
            if (kvspaceSetPartByRef(k->h, &r, key, 0, pairs[0].val.data,
                                    pairs[0].val.len, err, err_cap) == 0) {
                he->block_id = r.block_id;
                he->gen = r.gen;
                return 0;
            }
        }
        kvlangRefEnt_t *e = ref_find(k, key);
        if (e) {
            kvspaceRef_t r = { e->block_id, e->gen, 0, 0 };
            if (kvspaceSetPartByRef(k->h, &r, key, 0, pairs[0].val.data,
                                    pairs[0].val.len, err, err_cap) == 0) {
                e->block_id = r.block_id;
                e->gen = r.gen;
                {
                    const char *slash = strrchr(key, '/');
                    const char *nm = slash ? slash + 1 : key;
                    hot_put(k, nm, key, r.block_id, r.gen);
                }
                return 0;
            }
        }
    }
    for (int i = 0; i < n; i++) {
        const kvlangXvalue_t *v = &pairs[i].val;
        if (!v->data || v->len == 0) { /* None → 删键，令该槽读回 None（不可静默跳过留旧值） */
            const char *dk[1] = {pairs[i].key};
            kvspaceDel(k->h, dk, 1, err, err_cap);
            continue;
        }
        kvspaceHead_t h;
        if (kvspaceDecodeHead(v->data, v->len, &h) != 0 || !h.langtype[0])
            continue;
        uint32_t body_len = h.body_len < 0 ? 0 : (uint32_t)h.body_len;
        const uint8_t *body = v->data + h.body_offset;
        uint8_t *dst = NULL;
        if (kvspaceWriteInPlace(k->h, pairs[i].key, 0, body_len, &dst, err, err_cap) != 0) {
            if (kvspaceWriteNewPlace(k->h, pairs[i].key, h.ref, h.storetype, h.ro, h.vid, (const char *)h.langtype, body_len, &dst, err, err_cap) != 0) {
                rc = -1;
                break;
            }
        }
        if (body_len > 0 && dst)
            memcpy(dst, body, body_len);
        if (rc == 0 && n == 1 && ref_ok(k) && pairs[i].key) {
            const char *key = pairs[i].key;
            const char *slash = strrchr(key, '/');
            const char *rest = slash ? slash + 1 : key;
            const char *mid = NULL;
            if (rest) {
                for (const char *p = rest; *p; p++) {
                    if ((unsigned char)p[0] == 0xC2 && (unsigned char)p[1] == 0xB7)
                        mid = p;
                }
            }
            int is_member = mid != NULL;
            /* Unique `·` slots share one ART parent; do not fill the leaf table
             * (that walk + 64-slot scan was the unique-key miss). */
            if (is_member && (pref_cover(k, key) ||
                              (key[0] == '/' && strncmp(key, "/lib/", 5) == 0)))
                continue;
            kvspaceRef_t rr;
            if (kvspaceResolveRef(k->h, key, &rr) == 0) {
                if (!is_member) {
                    ref_put(k, key, &rr);
                    hot_put(k, rest, key, rr.block_id, rr.gen);
                }
                if (is_member) {
                    parent_put(k, key, (size_t)(mid - key) + MEMBER_SEP_LEN, &rr);
                } else if (!k->fpar.key) {
                    size_t seplen = 0;
                    int si = last_dir_sep(key, &seplen);
                    if (si >= 0)
                        parent_put(k, key, (size_t)si + seplen, &rr);
                }
            }
        }
    }
    return rc;
}

int kvlangKvDel(kvlangKv_t *k, const char *key, char *err, uint32_t err_cap) {
    const char *keys[1] = {key};
    return kvspaceDel(k->h, keys, 1, err, err_cap);
}

int kvlangKvDelTree(kvlangKv_t *k, const char *prefix, char *err, uint32_t err_cap) {
    kvlangKvInvalidateFrame(k, prefix);
    return kvspaceDelTree(k->h, prefix, err, err_cap);
}

int kvlangKvCp(kvlangKv_t *k, const char *src, const char *dst, char *err, uint32_t err_cap) {
    return kvspaceCp(k->h, src, dst, err, err_cap);
}

int kvlangKvCpTree(kvlangKv_t *k, const char *src, const char *dst, char *err, uint32_t err_cap) {
    return kvspaceCpTree(k->h, src, dst, err, err_cap);
}

int kvlangKvCpList(kvlangKv_t *k, const char *src, const char *dst, char *err, uint32_t err_cap) {
    return kvspaceCpList(k->h, src, dst, err, err_cap);
}

int kvlangKvMkindex(kvlangKv_t *k, const char *path, uint32_t capacity, char *err, uint32_t err_cap) {
    return kvspaceMkindex(k->h, path, capacity, err, err_cap);
}

int kvlangKvExtIndex(kvlangKv_t *k, const char *path, const char *ext, char *err, uint32_t err_cap) {
    return kvspaceMkindexExt(k->h, path, ext, err, err_cap);
}

int kvlangKvDelExtIndex(kvlangKv_t *k, const char *path, char *err, uint32_t err_cap) {
    return kvspaceRmindexExt(k->h, path, err, err_cap);
}

int kvlangKvList(kvlangKv_t *k, const char *prefix, bool expand_ext, bool resolve,
                 char ***out_names, int *out_count) {
    *out_names = NULL;
    *out_count = 0;
    int ex = expand_ext ? 1 : 0, rs = resolve ? 1 : 0;
    int32_t count = 0;
    if (kvspaceListLen(k->h, prefix, ex, rs, &count) != 0)
        return -1;
    if (count <= 0)
        return 0;
    char **names = malloc(sizeof(char *) * (size_t)count);
    for (int32_t i = 0; i < count; i++) {
        uint8_t buf[1024];
        uint32_t len = 0;
        if (kvspaceListAt(k->h, prefix, ex, rs, i, buf, sizeof buf, &len) == 0)
            names[i] = strndup((const char *)buf, len);
        else
            names[i] = strdup("");
    }
    *out_names = names;
    *out_count = (int)count;
    return 0;
}

int kvlangKvWatch(kvlangKv_t *k, const char *key, const kvlangXvalue_t *target, uint64_t tick_ns, kvlangXvalue_t *out) {
    kvlangXvalueZero(out);
    const uint8_t *t = target->data ? target->data : (const uint8_t *)"";
    uint32_t tl = target->len;
    uint8_t *d;
    uint32_t len;
    if (kvspaceWatch(k->h, key, t, tl, tick_ns, &d, &len) != 0)
        return -1;
    if (d && len) {
        out->data = d;
        out->len = len;
        out->borrowed = 1;
        kvlangXvalueMaterialize(out);
    }
    return 0;
}
