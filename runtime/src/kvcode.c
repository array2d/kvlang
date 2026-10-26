#include "runtime_internal.h"
#include "kvcode.h"
#include <string.h>

/* FNV-1a 64；code_hash 字段自身按 0 参与，避免写入校验值时无法自校验。 */
uint64_t kvlangCodeHash(const uint8_t *buf, uint32_t len) {
    const size_t skip = offsetof(kvlangCodeHeader, code_hash);
    uint64_t h = 1469598103934665603ULL;
    for (uint32_t i = 0; i < len; i++) {
        uint8_t b = (i >= skip && i < skip + sizeof(uint64_t)) ? 0 : buf[i];
        h ^= b;
        h *= 1099511628211ULL;
    }
    return h;
}

static bool section_ok(uint32_t off, uint32_t count, size_t esz,
                       uint32_t total) {
    if (count == 0)
        return true;
    if (off < sizeof(kvlangCodeHeader) || (off & 7u))
        return false;
    return (uint64_t)off + (uint64_t)count * esz <= total;
}

static bool strings_ok(const kvlangCodeHeader *h) {
    if (h->strings_len == 0)
        return true;
    if (h->off_strings < sizeof(kvlangCodeHeader) || (h->off_strings & 7u))
        return false;
    if ((uint64_t)h->off_strings + h->strings_len > h->total_size)
        return false;
    return true;
}

static const char *str_at(const kvlangCode *c, uint32_t off) {
    if (!c->strings || off >= c->h.strings_len)
        return NULL;
    return c->strings + off;
}

int kvlangCodeParse(const uint8_t *buf, uint32_t len, kvlangCode *out,
                    char *err, size_t err_cap) {
    memset(out, 0, sizeof *out);
    if (len < sizeof(kvlangCodeHeader)) {
        snprintf(err, err_cap, "code: truncated header");
        return -1;
    }
    memcpy(&out->h, buf, sizeof out->h);
    const kvlangCodeHeader *h = &out->h;
    if (h->magic != KVCODE_MAGIC) {
        snprintf(err, err_cap, "code: bad magic");
        return -1;
    }
    if (h->version != KVCODE_VERSION) {
        snprintf(err, err_cap, "code: unsupported version %u", h->version);
        return -1;
    }
    if (h->total_size < sizeof(kvlangCodeHeader) || h->total_size > len) {
        snprintf(err, err_cap, "code: bad total_size");
        return -1;
    }
    if (h->nregs == 0 || h->nregs > 128) {
        snprintf(err, err_cap, "code: bad nregs %u", h->nregs);
        return -1;
    }
    if (h->ninsn == 0) {
        snprintf(err, err_cap, "code: empty insn");
        return -1;
    }
    if (!section_ok(h->off_insn, h->ninsn, sizeof(uint32_t), h->total_size) ||
        !section_ok(h->off_const, h->nconst, sizeof(kvlangConst),
                    h->total_size) ||
        !section_ok(h->off_slot, h->nslot, sizeof(kvlangSlot), h->total_size) ||
        !section_ok(h->off_complex, h->ncomplex, sizeof(kvlangComplex),
                    h->total_size) ||
        !section_ok(h->off_checkpoint, h->ncheckpoint, sizeof(kvlangCheckpoint),
                    h->total_size) ||
        !section_ok(h->off_pc_map, h->ninsn, sizeof(uint16_t), h->total_size) ||
        !section_ok(h->off_init, h->ninit, sizeof(kvlangInitSlot),
                    h->total_size) ||
        !strings_ok(h)) {
        snprintf(err, err_cap, "code: bad section offset/size");
        return -1;
    }
    if (kvlangCodeHash(buf, h->total_size) != h->code_hash) {
        snprintf(err, err_cap, "code: hash mismatch");
        return -1;
    }
    out->insn = h->ninsn ? (const uint32_t *)(buf + h->off_insn) : NULL;
    out->consts = h->nconst ? (const kvlangConst *)(buf + h->off_const) : NULL;
    out->slots = h->nslot ? (const kvlangSlot *)(buf + h->off_slot) : NULL;
    out->complex =
        h->ncomplex ? (const kvlangComplex *)(buf + h->off_complex) : NULL;
    out->checkpoints = h->ncheckpoint
                           ? (const kvlangCheckpoint *)(buf + h->off_checkpoint)
                           : NULL;
    out->pc_map = h->ninsn ? (const uint16_t *)(buf + h->off_pc_map) : NULL;
    out->init = h->ninit ? (const kvlangInitSlot *)(buf + h->off_init) : NULL;
    out->strings = h->strings_len ? (const char *)(buf + h->off_strings) : NULL;
    for (uint16_t i = 0; i < h->nslot; i++) {
        const char *p = str_at(out, out->slots[i].path_off);
        if (h->strings_len && (!p || !memchr(p, 0, h->strings_len -
                                                      out->slots[i].path_off))) {
            snprintf(err, err_cap, "code: slot %u bad path_off", i);
            return -1;
        }
    }
    for (uint16_t i = 0; i < h->ncheckpoint; i++) {
        const kvlangCheckpoint *cp = &out->checkpoints[i];
        if (cp->insn >= h->ninsn ||
            (h->strings_len && cp->kv_pc_off >= h->strings_len)) {
            snprintf(err, err_cap, "code: checkpoint %u out of range", i);
            return -1;
        }
    }
    for (uint16_t i = 0; i < h->ninsn; i++) {
        if (out->pc_map[i] != KVCODE_NO_CHECKPOINT &&
            out->pc_map[i] >= h->ncheckpoint) {
            snprintf(err, err_cap, "code: pc_map[%u] out of range", i);
            return -1;
        }
    }
    out->blob = NULL;
    out->blob_len = 0;
    return 0;
}

/* ── code cache ─────────────────────────────────────────────────────── */

typedef struct {
    char *path;
    kvlangCode *code;
} code_entry;

static code_entry *g_cache;
static size_t g_cache_n, g_cache_cap;

static kvlangCode *cache_find(const char *path) {
    for (size_t i = 0; i < g_cache_n; i++)
        if (strcmp(g_cache[i].path, path) == 0)
            return g_cache[i].code;
    return NULL;
}

static void cache_put(const char *path, kvlangCode *code) {
    if (g_cache_n == g_cache_cap) {
        size_t cap = g_cache_cap ? g_cache_cap * 2 : 16;
        code_entry *n = realloc(g_cache, cap * sizeof *n);
        if (!n)
            kvlangDie("out of memory caching kvlangCode");
        g_cache = n;
        g_cache_cap = cap;
    }
    g_cache[g_cache_n].path = strdup(path);
    g_cache[g_cache_n].code = code;
    if (!g_cache[g_cache_n].path)
        kvlangDie("out of memory caching kvlangCode path");
    g_cache_n++;
}

void kvlangCodeFree(kvlangCode *c) {
    if (!c || !c->blob)
        return;
    free(c->blob);
    free(c);
}

void kvlangCodeCacheReset(void) {
    for (size_t i = 0; i < g_cache_n; i++) {
        free(g_cache[i].path);
        kvlangCodeFree(g_cache[i].code);
    }
    free(g_cache);
    g_cache = NULL;
    g_cache_n = g_cache_cap = 0;
}

/* isa/compact hash 的运行时提供者：P1 由 coding3 的 live ISA 落位；
 * 尚未提供时只记录 header 值，不做拒绝（Stage 1 不改执行路径）。 */
__attribute__((weak)) const char *kvlangIsaCoreHash(void) { return NULL; }

static int check_hashes(const kvlangCodeHeader *h, char *err, size_t err_cap) {
    const char *core = kvlangIsaCoreHash();
    if (core && core[0] && strcmp(core, h->isa_hash) != 0) {
        snprintf(err, err_cap, "code: stale ISA (core %s != %s)", core,
                 h->isa_hash);
        return -1;
    }
    return 0;
}

kvlangCode *kvlangCodeLoad(void *kvspace, const char *func_path) {
    kvspace_t *kv = kvspace;
    kvlangCode *hit = cache_find(func_path);
    if (hit)
        return hit;
    char key[KV_KEY_MAX];
    snprintf(key, sizeof key, "%s/" RUNTIME_MEMBER_SEP "code", func_path);
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    if (kvlangKvspaceGetOne(kv, key, &v) != 0 || !v.bytes || v.length == 0) {
        kvlangDie("kvlangCodeLoad: missing %s", key);
        return NULL;
    }
    const uint8_t *body = xh_body(v.bytes);
    uint32_t blen = (uint32_t)xh_content_len(v.bytes);
    if (!body || blen == 0) {
        kvlangDie("kvlangCodeLoad: empty %s", key);
        return NULL;
    }
    uint8_t *blob = malloc(blen);
    if (!blob)
        kvlangDie("out of memory loading kvlangCode");
    memcpy(blob, body, blen);
    kvlangCode parsed;
    char err[256] = {0};
    if (kvlangCodeParse(blob, blen, &parsed, err, sizeof err) != 0) {
        free(blob);
        kvlangDie("%s: %s", key, err[0] ? err : "invalid code");
        return NULL;
    }
    if (check_hashes(&parsed.h, err, sizeof err) != 0) {
        free(blob);
        kvlangDie("%s: %s", key, err);
        return NULL;
    }
    kvlangCode *c = calloc(1, sizeof *c);
    if (!c)
        kvlangDie("out of memory loading kvlangCode");
    *c = parsed;
    c->blob = blob;
    c->blob_len = blen;
    cache_put(func_path, c);
    return c;
}

/* ── 寄存器文件（address-only） ─────────────────────────────────────── */

void kvlangRegFileInit(kvlangRegFile *rf, uint16_t nregs) {
    rf->regs = calloc(nregs ? nregs : 1, sizeof *rf->regs);
    if (!rf->regs)
        kvlangDie("out of memory allocating register file");
    rf->nregs = nregs;
    rf->epoch = 0;
    for (uint16_t i = 0; i < nregs; i++)
        rf->regs[i].addr = KV_REG_NONE;
}

void kvlangRegFileFree(kvlangRegFile *rf) {
    free(rf->regs);
    rf->regs = NULL;
    rf->nregs = 0;
}

void kvlangRegSet(kvlangRegFile *rf, uint16_t r, uint64_t addr, uint8_t kind) {
    if (r >= rf->nregs)
        kvlangDie("register %u out of range (nregs=%u)", r, rf->nregs);
    rf->regs[r].addr = addr;
    rf->regs[r].kind = kind;
    rf->regs[r].gen = rf->epoch;
}

void kvlangRegSetNone(kvlangRegFile *rf, uint16_t r) {
    if (r >= rf->nregs)
        kvlangDie("register %u out of range (nregs=%u)", r, rf->nregs);
    rf->regs[r].addr = KV_REG_NONE;
    rf->regs[r].kind = 0;
    rf->regs[r].gen = rf->epoch;
}

void kvlangRegInvalidate(kvlangRegFile *rf, uint32_t epoch) {
    rf->epoch = epoch;
    for (uint16_t i = 0; i < rf->nregs; i++)
        rf->regs[i].gen = epoch;
}
