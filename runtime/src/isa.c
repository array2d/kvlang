/* ISA registry（P1a）：控制面独立 kvspace。runtime-c 启动时发布最小 core
 * manifest 到 `/‥isa/‥core` 与 `‥meta`；layout 在 `/‥isa/func<func-path>` 写
 * per-func isa-ref。控制 store 与数据 store 分离（redis 共用同一库），保证
 * 数据 store 的 /lib 热键不因新增控制键而漂移。内部函数，不导出 ABI。
 *
 * manifest body（UTF-8，行 \n、字段 \t）：
 *   ver\t1 / count\tN / hash\t<32hex> / 记录行
 * 记录：opcode nr nw dynamic rp_1..rp_nr wp_1..wp_nw storetype headlen width
 *       effects cost class owner
 * hash = hex(SHA-256(记录区)[0:16])，记录按 opcode 字节序升序。 */

#include "runtime_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int strcmp_ptr(const void *a, const void *b);

/* ── SHA-256（FIPS 180-4，仅用于 manifest hash） ─────────────────────── */

typedef struct {
    uint32_t h[8];
    uint64_t n;
    uint8_t buf[64];
    size_t buflen;
} sha256_t;

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(sha256_t *s, const uint8_t *p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
               ((uint32_t)p[i * 4 + 2] << 8) | p[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t a = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t b = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + a + w[i - 7] + b;
    }
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3];
    uint32_t e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t s1 = ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + ch + K[i] + w[i];
        uint32_t s0 = ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22);
        uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + mj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d;
    s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}

static void sha256_init(sha256_t *s) {
    static const uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372,
                                     0xa54ff53a, 0x510e527f, 0x9b05688c,
                                     0x1f83d9ab, 0x5be0cd19};
    memcpy(s->h, init, sizeof init);
    s->n = 0;
    s->buflen = 0;
}

static void sha256_update(sha256_t *s, const uint8_t *p, size_t n) {
    s->n += n;
    while (n > 0) {
        size_t take = 64 - s->buflen;
        if (take > n)
            take = n;
        memcpy(s->buf + s->buflen, p, take);
        s->buflen += take;
        p += take;
        n -= take;
        if (s->buflen == 64) {
            sha256_block(s, s->buf);
            s->buflen = 0;
        }
    }
}

static void sha256_final(sha256_t *s, uint8_t out[32]) {
    uint64_t bits = s->n * 8;
    uint8_t pad = 0x80;
    sha256_update(s, &pad, 1);
    uint8_t zero = 0;
    while (s->buflen != 56)
        sha256_update(s, &zero, 1);
    uint8_t len[8];
    for (int i = 0; i < 8; i++)
        len[i] = (uint8_t)(bits >> (56 - 8 * i));
    memcpy(s->buf + 56, len, 8);
    sha256_block(s, s->buf);
    for (int i = 0; i < 8; i++) {
        out[i * 4] = (uint8_t)(s->h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(s->h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(s->h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)s->h[i];
    }
}

static void sha256_hex16(const char *data, size_t n, char out[33]) {
    sha256_t s;
    sha256_init(&s);
    sha256_update(&s, (const uint8_t *)data, n);
    uint8_t d[32];
    sha256_final(&s, d);
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 16; i++) {
        out[2 * i] = hex[d[i] >> 4];
        out[2 * i + 1] = hex[d[i] & 0xf];
    }
    out[32] = 0;
}

/* P1b1：全量 manifest（core 237 + ext 63 = 300）。签名列 isa_sig 与 myrwircaps
 * 同序，发布期做形状校验；不另列 opcode 清单。 */

static char g_isa_hash[33];

static bool numeric_family(const char *s, size_t n) {
    static const char *const kinds[] = {"int8", "int16", "int32", "int64",
                                        "uint8", "uint16", "uint32", "uint64",
                                        "float32", "float64"};
    for (size_t i = 0; i < sizeof kinds / sizeof kinds[0]; i++)
        if (strlen(kinds[i]) == n && strncmp(s, kinds[i], n) == 0)
            return true;
    return false;
}

static int kind_width(const char *k) {
    if (strcmp(k, "bool") == 0 || strcmp(k, "int8") == 0 ||
        strcmp(k, "uint8") == 0)
        return 1;
    if (strcmp(k, "int16") == 0 || strcmp(k, "uint16") == 0)
        return 2;
    if (strcmp(k, "int32") == 0 || strcmp(k, "uint32") == 0 ||
        strcmp(k, "float32") == 0)
        return 4;
    if (strcmp(k, "int64") == 0 || strcmp(k, "uint64") == 0 ||
        strcmp(k, "float64") == 0)
        return 8;
    return -1;
}

static bool cast_kind(const char *k) {
    static const char *const kinds[] = {
        "bool",  "int8",   "int16",  "int32", "int64",  "uint8",
        "uint16", "uint32", "uint64", "float32", "float64",
        "char/utf32", "char/utf8", "char/ascii"};
    for (size_t i = 0; i < sizeof kinds / sizeof kinds[0]; i++)
        if (strcmp(k, kinds[i]) == 0)
            return true;
    return false;
}

/* effects 串按字母序输出（spec 1.3）。 */
static void effects_str(uint8_t mask, char *out, size_t cap) {
    static const struct {
        uint8_t bit;
        const char *name;
    } tab[] = {{KV_EFF_CONTROL, "control"}, {KV_EFF_DEBUG, "debug"},
               {KV_EFF_IO, "io"},           {KV_EFF_PURE, "pure"},
               {KV_EFF_READ, "read"},       {KV_EFF_WRITE, "write"}};
    size_t p = 0;
    out[0] = 0;
    for (size_t i = 0; i < sizeof tab / sizeof tab[0]; i++) {
        if (!(mask & tab[i].bit))
            continue;
        int k = snprintf(out + p, cap - p, "%s%s", p ? "," : "", tab[i].name);
        if (k < 0 || (size_t)k >= cap - p)
            return;
        p += (size_t)k;
    }
}

#define ISA_ANY "any"

/* 构造全量 core manifest body（返回 malloc 字符串）。形状不符 -> NULL（p0）。 */
static char *isa_build_core(uint32_t *count_out) {
    const kvlangRwirCap *caps = kvlangRwirCaps();
    int route = kvlangRwirRouteId();
    uint32_t nsig = 0;
    const kvlangIsaSig *sigs = kvlangIsaSigs(&nsig);
    if (!caps || route <= 0 || (uint32_t)route != nsig)
        return NULL;
    char **rec = calloc((size_t)route, sizeof *rec);
    if (!rec)
        return NULL;
    uint32_t n = 0;
    bool ok = true;
    for (int i = 0; i < route && ok; i++) {
        const char *op = caps[i].opcode;
        const kvlangIsaSig *g = &sigs[i];
        if (!op || !op[0] || g->nr > MAX_PARAMS || g->nw > MAX_PARAMS) {
            ok = false;
            break;
        }
        int nr = g->nr, nw = g->nw;
        char fam[32];
        char str_kind[32];
        const char *rp[8] = {0}, *wp[4] = {0};
        int st = -1, hl = -1, w = -1;
        switch (g->sig) {
        case KV_ISA_BIN:
        case KV_ISA_CMP:
        case KV_ISA_UN: {
            const char *dot = strstr(op, MEMBER_SEP);
            if (!dot || (size_t)(dot - op) >= sizeof fam) {
                ok = false;
                break;
            }
            size_t flen = (size_t)(dot - op);
            memcpy(fam, op, flen);
            fam[flen] = 0;
            const char *want = g->sig == KV_ISA_CMP ? KVSPACE_KIND_BOOL : fam;
            if (!numeric_family(fam, flen) || !caps[i].result_kind ||
                strcmp(caps[i].result_kind, want) != 0) {
                ok = false;
                break;
            }
            int arity = g->sig == KV_ISA_UN ? 1 : 2;
            if (nr != arity || nw != 1) {
                ok = false;
                break;
            }
            for (int j = 0; j < nr; j++)
                rp[j] = fam;
            wp[0] = g->sig == KV_ISA_CMP ? KVSPACE_KIND_BOOL : fam;
            st = 0;
            hl = 32;
            w = (int)caps[i].result_width;
            break;
        }
        case KV_ISA_CAST:
            if (!cast_kind(op) || nr != 1 || nw != 1) {
                ok = false;
                break;
            }
            rp[0] = ISA_ANY;
            wp[0] = op;
            if (strncmp(op, "char/", 5) == 0) {
                /* 字符串槽 kindexpr 恒为 []char（见 kindexpr 规范）。 */
                snprintf(str_kind, sizeof str_kind, "[]%s", op);
                wp[0] = str_kind;
                st = 1;
                hl = 64;
            } else {
                st = 0;
                hl = 32;
                w = kind_width(op);
            }
            break;
        case KV_ISA_ANY:
            for (int j = 0; j < nr; j++)
                rp[j] = ISA_ANY;
            for (int j = 0; j < nw; j++)
                wp[j] = ISA_ANY;
            if (g->dynamic && nr > 0)
                rp[nr - 1] = ISA_ANY "...";
            if (strcmp(op, "kvlang" MEMBER_SEP "abs") == 0) {
                st = 1;
                hl = 64;
            }
            break;
        default:
            ok = false;
            break;
        }
        if (!ok)
            break;
        char eff[32];
        effects_str(g->effects, eff, sizeof eff);
        size_t cap = (size_t)(11 + nr + nw) * 40 + strlen(op) + 160;
        char *line = malloc(cap);
        if (!line) {
            ok = false;
            break;
        }
        int p = snprintf(line, cap, "%s\t%d\t%d\t%d", op, nr, nw,
                         g->dynamic ? 1 : 0);
        for (int j = 0; j < nr; j++)
            p += snprintf(line + p, cap - (size_t)p, "\t%s", rp[j]);
        for (int j = 0; j < nw; j++)
            p += snprintf(line + p, cap - (size_t)p, "\t%s", wp[j]);
        snprintf(line + p, cap - (size_t)p,
                 "\t%d\t%d\t%d\t%s\t-1\t%s\truntime-c", st, hl, w, eff,
                 g->cls == KV_ISA_EXT ? "ext" : "core");
        rec[n++] = line;
    }
    if (!ok) {
        for (uint32_t i = 0; i < n; i++)
            free(rec[i]);
        free(rec);
        return NULL;
    }
    qsort(rec, n, sizeof *rec, (int (*)(const void *, const void *))strcmp_ptr);
    size_t total = 128;
    for (uint32_t i = 0; i < n; i++)
        total += strlen(rec[i]) + 1;
    char *body = malloc(total);
    if (body) {
        size_t p = (size_t)snprintf(body, total, "ver\t1\ncount\t%u\nhash\t",
                                    n);
        char *records = malloc(total);
        if (!records) {
            free(body);
            body = NULL;
        } else {
            size_t rl = 0;
            for (uint32_t i = 0; i < n; i++)
                rl += (size_t)snprintf(records + rl, total - rl, "%s\n", rec[i]);
            char h[33];
            sha256_hex16(records, rl, h);
            p += (size_t)snprintf(body + p, total - p, "%s\n", h);
            if (p + rl < total)
                memcpy(body + p, records, rl);
            p += rl;
            snprintf(g_isa_hash, sizeof g_isa_hash, "%s", h);
            free(records);
        }
    }
    for (uint32_t i = 0; i < n; i++)
        free(rec[i]);
    free(rec);
    if (!body)
        return NULL;
    *count_out = n;
    return body;
}

static int strcmp_ptr(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* 控制 store 视图：handle 指向 isa_handle（未设则即数据 store）。 */
static kvspace_t isa_view(kvspace_t *kv) {
    kvspace_t v = *kv;
    if (kv->isa_handle) {
        v.handle = kv->isa_handle;
        v.isa_handle = NULL;
        v.isa_shared = false;
    }
    return v;
}

/* P1a：控制 store DSN 只由 KVSPACE 派生——shm/fs 为 `<dsn>.isa`，redis 与数据
 * 同库；其它 scheme 硬失败（没有环境变量可覆盖）。 */
__attribute__((visibility("hidden"))) bool kvlangIsaCtlDsn(const char *dsn,
                                                          char *out,
                                                          size_t cap) {
    if (!dsn)
        return false;
    if (strncmp(dsn, "shm://", 6) == 0 || strncmp(dsn, "fs://", 5) == 0) {
        size_t n = strlen(dsn);
        while (n > 0 && dsn[n - 1] == '/')
            n--;
        return snprintf(out, cap, "%.*s.isa", (int)n, dsn) < (int)cap;
    }
    if (strncmp(dsn, "redis://", 8) == 0)
        return snprintf(out, cap, "%s", dsn) < (int)cap;
    return false;
}

/* P1a 裁决 C：manifest/meta 以 `[n]char/utf8` SLACK 存储；内容才是语义来源。 */
static int isa_set(kvspace_t *kv, const char *key, const char *body, uint32_t len,
                   char *err, uint32_t cap) {
    kvspace_t v = isa_view(kv);
    uint8_t *xv = NULL;
    uint32_t xl = 0;
    if (kvspaceNewChar((const uint8_t *)body, len, &xv, &xl) != 0)
        return -1;
    int rc = kvspaceSetValue(v.handle, key, xv, xl, 1, 0, err, cap);
    free(xv);
    return rc;
}

__attribute__((visibility("hidden"))) int kvlangIsaPublishCore(kvspace_t *kv) {
    if (!kv)
        return -1;
    kvspace_t v = isa_view(kv);
    kvspace_t *store = &v;
    uint32_t count = 0;
    char *body = isa_build_core(&count);
    if (!body)
        return -1;
    char err[256] = {0};
    if (kvlangKvspaceMakeIndex(store, "/‥isa/", 0, err, sizeof err) != 0) {
        free(body);
        return -1;
    }
    kvlangXvalue_t cur;
    kvlangXvalueZero(&cur);
    kvlangKvspaceGetOne(store, "/‥isa/‥core", &cur);
    bool same = false;
    if (!kvlangXvalueNone(&cur)) {
        size_t l = (size_t)xh_content_len(cur.bytes);
        same = l == strlen(body) && memcmp(xh_body(cur.bytes), body, l) == 0;
    }
    if (!same && isa_set(store, "/‥isa/‥core", body, (uint32_t)strlen(body), err,
                         sizeof err) != 0) {
        free(body);
        return -1;
    }
    char meta[256];
    snprintf(meta, sizeof meta, "ver\t1\ncount\t%u\nhash\t%s\nowners\truntime-c\n",
             count, g_isa_hash);
    free(body);
    if (isa_set(store, "/‥isa/‥meta", meta, (uint32_t)strlen(meta), err,
                sizeof err) != 0)
        return -1;
    return 0;
}

__attribute__((visibility("hidden"))) int kvlangIsaCheck(kvspace_t *kv,
                                                         const char *func_dir,
                                                         char *err,
                                                         uint32_t err_cap) {
    if (!kv || !func_dir)
        return -1;
    char key[KV_KEY_MAX];
    size_t n = strlen(func_dir);
    if (func_dir[0] != '/') {
        snprintf(err, err_cap, "RuntimeError: stale ISA layout: bad func dir");
        return -1;
    }
    while (n > 1 && func_dir[n - 1] == '/')
        n--;
    /* 成员分隔符 '·'(U+00B7) 在 durable 里是 memitem 分隔符，需容器声明；
     * 控制键里统一折成 '/'，得到纯路径 key。layout 侧用同一规则。 */
    static const char prefix[] = "/‥isa/func";
    size_t plen = sizeof prefix - 1;
    if (plen + n + 1 > sizeof key) {
        snprintf(err, err_cap, "RuntimeError: stale ISA layout: func dir too long");
        return -1;
    }
    memcpy(key, prefix, plen);
    size_t o = plen;
    for (size_t i = 0; i < n; i++) {
        if (i + 1 < n && (unsigned char)func_dir[i] == 0xC2 &&
            (unsigned char)func_dir[i + 1] == 0xB7) {
            key[o++] = '/';
            i++;
        } else {
            key[o++] = func_dir[i];
        }
    }
    key[o] = 0;
    kvspace_t view = isa_view(kv);
    kvlangXvalue_t v;
    kvlangXvalueZero(&v);
    kvlangKvspaceGetOne(&view, key, &v);
    if (kvlangXvalueNone(&v)) {
        snprintf(err, err_cap, "RuntimeError: stale ISA layout: missing ‥isa for %s",
                 func_dir);
        return -1;
    }
    char *txt = kvlangXvalueValueString(&v);
    const char *h = txt ? strstr(txt, "hash\t") : NULL;
    bool ok = h && strlen(h + 5) >= 32 && memcmp(h + 5, g_isa_hash, 32) == 0;
    if (!ok) {
        char got[33] = {0};
        if (h)
            memcpy(got, h + 5, 32);
        snprintf(err, err_cap,
                 "RuntimeError: stale ISA layout: expects %.32s, runtime %.32s",
                 got, g_isa_hash);
    }
    free(txt);
    return ok ? 0 : -1;
}
