/* test_define_rwir.c — kvlangLayoutDefineRwir 的 C ABI 测试。
 *
 * 验的是**跨 ABI 边界**：符号能 dlsym 到、参数按 C 约定传对、写进 kvspace 的
 * 内容能被新存储 ABI 原样读出来。
 *
 * 读回用新 ABI（kvspaceResolveOffset + kvspaceXvalueInProcessAddress），不经过 layout
 * 自己的 printlib——那会把「写对了」和「layout 读得懂」混成一个判据。
 */

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;

#define CHECK(condition, ...)                                                  \
    do {                                                                       \
        if (!(condition)) {                                                    \
            failures++;                                                        \
            fprintf(stderr, "  FAIL %s:%d: ", __FILE__, __LINE__);             \
            fprintf(stderr, __VA_ARGS__);                                      \
            fprintf(stderr, "\n");                                             \
        }                                                                      \
    } while (0)

typedef int (*define_rwir_fn)(const char *dsn, const char *opcode,
                              const char *const *read_langtypes,
                              uint32_t read_count,
                              const char *const *write_langtypes,
                              uint32_t write_count, const uint8_t *write_inplace,
                              int variadic, char *err, uint32_t err_cap);

typedef void *(*connect_fn)(const char *dsn);
typedef void (*close_fn)(void *h);
typedef int (*resolve_fn)(void *h, const char *key, int64_t anchor,
                          uint64_t *out_offset, char *err, uint32_t err_cap);
typedef uint8_t *(*inproc_fn)(void *h);

static define_rwir_fn define_rwir;
static connect_fn kv_connect;
static close_fn kv_close;
static resolve_fn kv_resolve;
static inproc_fn kv_inproc;

/* head 布局：bodylen:u64@0, attrslen:u16@8, padding:6@10-15, body@16, attr 紧随。 */
#define HEAD_LEN 16u

/* 读一个 key 的整条 wire，拷进 out（调用方给缓冲）。返回总字节数，取不到返回 0。 */
static uint64_t read_wire(void *h, const char *key, uint8_t *out, uint64_t cap) {
    uint64_t off = 0;
    char err[256] = {0};
    if (kv_resolve(h, key, 0, &off, err, sizeof err) != 0)
        return 0;
    uint8_t *base = kv_inproc(h);
    if (!base)
        return 0;
    const uint8_t *wire = base + off;
    uint64_t body_len = 0;
    for (int i = 7; i >= 0; i--)
        body_len = (body_len << 8) | wire[i];
    uint32_t attrs_len = (uint32_t)wire[8] | ((uint32_t)wire[9] << 8);
    uint64_t total = HEAD_LEN + body_len + attrs_len;
    if (total > cap)
        return 0;
    memcpy(out, wire, (size_t)total);
    return total;
}

/* body 与 attr 区之间取 body；返回 body 长度。 */
static uint64_t body_of(const uint8_t *wire, uint64_t total, const uint8_t **body) {
    if (total < HEAD_LEN) {
        *body = NULL;
        return 0;
    }
    uint64_t body_len = 0;
    for (int i = 7; i >= 0; i--)
        body_len = (body_len << 8) | wire[i];
    *body = wire + HEAD_LEN;
    return body_len;
}

/* 断言某个签名槽的 body 等于期望 langtype。 */
static void expect_slot(void *kv, const char *key, const char *want, int *line) {
    uint8_t wire[512];
    uint64_t total = read_wire(kv, key, wire, sizeof wire);
    *line = __LINE__;
    if (total == 0) {
        CHECK(0, "%s: 读不到（resolve 失败或后端非映射）", key);
        return;
    }
    const uint8_t *body = NULL;
    uint64_t body_len = body_of(wire, total, &body);
    if (body_len != strlen(want) || (body_len && memcmp(body, want, body_len) != 0)) {
        CHECK(0, "%s: body 应为 \"%s\"，实际 %.*s（%llu 字节）", key, want,
              (int)body_len, body ? (const char *)body : "", (unsigned long long)body_len);
    }
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <libkvlanglayout.so> <libkvspace.so>\n", argv[0]);
        return 2;
    }
    void *layout_dl = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!layout_dl) {
        fprintf(stderr, "dlopen layout 失败: %s\n", dlerror());
        return 2;
    }
    define_rwir = (define_rwir_fn)dlsym(layout_dl, "kvlangLayoutDefineRwir");
    if (!define_rwir) {
        fprintf(stderr, "layout 未导出 kvlangLayoutDefineRwir\n");
        return 2;
    }
    void *kv_dl = dlopen(argv[2], RTLD_NOW | RTLD_LOCAL);
    if (!kv_dl) {
        fprintf(stderr, "dlopen kvspace 失败: %s\n", dlerror());
        return 2;
    }
    kv_connect = (connect_fn)dlsym(kv_dl, "kvspaceConnect");
    kv_close = (close_fn)dlsym(kv_dl, "kvspaceClose");
    kv_resolve = (resolve_fn)dlsym(kv_dl, "kvspaceResolveOffset");
    kv_inproc = (inproc_fn)dlsym(kv_dl, "kvspaceXvalueInProcessAddress");
    if (!kv_connect || !kv_close || !kv_resolve || !kv_inproc) {
        fprintf(stderr, "kvspace 缺少所需符号\n");
        return 2;
    }

    char dsn[256];
    snprintf(dsn, sizeof dsn, "shm:///tmp/kvlang_layout_defrwir_%ld.shm", (long)getpid());
    char err[512];

    /* ── 1. 正常声明：1 读 1 写 ─────────────────────────────────────── */
    const char *reads1[] = {"int64"};
    const char *writes1[] = {"int64"};
    memset(err, 0, sizeof err);
    CHECK(define_rwir(dsn, "t·f", reads1, 1, writes1, 1, NULL, 0, err, sizeof err) == 0,
          "基本声明应成功: %s", err);

    /* ── 2. 用新 ABI 读回，验签名内容 ───────────────────────────────── */
    {
        void *kv = kv_connect(dsn);
        CHECK(kv != NULL, "connect 失败");
        if (kv) {
            int line = 0;
            expect_slot(kv, "/lib/t·f/[0,-1]", "int64", &line);
            expect_slot(kv, "/lib/t·f/[0,1]", "int64", &line);
            kv_close(kv);
        }
    }

    /* ── 2b. 路由头本身：/lib/<op>（不带 /[0,0]），body 是计数头 ────────
     *
     * 只验签名槽不够：路由头才是「这个 opcode 是 def rwir」的声明点。它丢了
     * 或写错 kind，签名槽再对也没人认。 */
    {
        void *kv = kv_connect(dsn);
        if (kv) {
            uint8_t wire[512];
            uint64_t total = read_wire(kv, "/lib/t·f", wire, sizeof wire);
            CHECK(total != 0, "/lib/t·f 路由头读不到");
            if (total) {
                const uint8_t *body = NULL;
                uint64_t bl = body_of(wire, total, &body);
                /* 计数头 layout: nr(2B) | dynamic 标记 | nw(2B)，具体宽度由
                 * kvkind 的 counts_body 定——这里只断言「body 非空且长度稳定」，
                 * 精确字节比对交给 layout 自己的单测（那是 kvkind 的职责）。 */
                CHECK(bl > 0, "/lib/t·f 路由头 body 不该为空");
            }
            kv_close(kv);
        }
    }

    /* ── 3. 空 opcode → 拒绝 ───────────────────────────────────────── */
    memset(err, 0, sizeof err);
    CHECK(define_rwir(dsn, "", reads1, 1, writes1, 1, NULL, 0, err, sizeof err) == -1,
          "空 opcode 应返回 -1");
    CHECK(err[0] != '\0', "空 opcode 应写 err");

    /* ── 4. 非法 opcode 含 `/` → 拒绝 ───────────────────────────────
     *
     * 不能用「write_inplace 长度不符」来测：C 侧只传裸指针 + write_count，
     * 位图长度就是 write_count，**长度不符在 ABI 层不可表达**。硬要测它等于
     * 测一个不存在的前提。
     *
     * 换成两条真正能在边界上判的：
     *   a) opcode 含 `/` —— key 用 `·` 分层，`/` 会造出意料之外的树形； */
    memset(err, 0, sizeof err);
    CHECK(define_rwir(dsn, "t/bad", reads1, 1, writes1, 1, NULL, 0, err, sizeof err) == -1,
          "opcode 含 '/' 应返回 -1（err=%s）", err);
    CHECK(err[0] != '\0', "opcode 含 '/' 应写 err");

    /*   b) 计数>0 却给 NULL 数组 —— 当成 0 处理会让「声明了 3 个」静默变 0 个。 */
    memset(err, 0, sizeof err);
    CHECK(define_rwir(dsn, "t·null", NULL, 2, writes1, 1, NULL, 0, err, sizeof err) == -1,
          "read_count=2 但数组为 NULL 应返回 -1（err=%s）", err);

    /* ── 5. 幂等：同一 opcode 声明两次都成功，且以最后一次为准 ──────── */
    const char *reads_v2[] = {"[]char/utf8"};
    const char *writes_v2[] = {"int64"};
    memset(err, 0, sizeof err);
    CHECK(define_rwir(dsn, "t·f", reads_v2, 1, writes_v2, 1, NULL, 0, err, sizeof err) == 0,
          "重复声明应幂等成功: %s", err);
    {
        void *kv = kv_connect(dsn);
        if (kv) {
            int line = 0;
            expect_slot(kv, "/lib/t·f/[0,-1]", "[]char/utf8", &line);
            kv_close(kv);
        }
    }

    /* ── 6. inplace 位图真的写进 wire ──────────────────────────────── */
    const char *writes3[] = {"int64"};
    const uint8_t bitmap3[1] = {1};
    memset(err, 0, sizeof err);
    CHECK(define_rwir(dsn, "t·ip", reads1, 1, writes3, 1, bitmap3, 0, err, sizeof err) == 0,
          "带 inplace 的声明应成功: %s", err);
    {
        void *kv = kv_connect(dsn);
        if (kv) {
            /* inplace 在 wire 里是 `int64 inplace`。 */
            int line = 0;
            expect_slot(kv, "/lib/t·ip/[0,1]", "int64 inplace", &line);
            kv_close(kv);
        }
    }

    /* ── 7. 变参：variadic=1 只在末读参上加 `...` ───────────────────── */
    const char *reads_multi[] = {"int64", "[]char/utf8"};
    memset(err, 0, sizeof err);
    CHECK(define_rwir(dsn, "t·va", reads_multi, 2, writes1, 1, NULL, 1, err, sizeof err) == 0,
          "变参声明应成功: %s", err);
    {
        void *kv = kv_connect(dsn);
        if (kv) {
            int line = 0;
            expect_slot(kv, "/lib/t·va/[0,-1]", "int64", &line);
            /* 变参标记是签名层 arity，落进 langtype_list 时被 strip，所以这里
             * 期望的是干净的 `[]char/utf8`。 */
            expect_slot(kv, "/lib/t·va/[0,-2]", "[]char/utf8", &line);
            kv_close(kv);
        }
    }

    /* ── 8. variadic=1 但无读参 → 拒绝（变参只能落在末读参上）───────── */
    memset(err, 0, sizeof err);
    CHECK(define_rwir(dsn, "t·vb", NULL, 0, NULL, 0, NULL, 1, err, sizeof err) == -1,
          "无读参却声明变参应返回 -1（err=%s）", err);

    char path[256];
    snprintf(path, sizeof path, "/tmp/kvlang_layout_defrwir_%ld.shm", (long)getpid());
    unlink(path);

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("ok");
    return 0;
}
