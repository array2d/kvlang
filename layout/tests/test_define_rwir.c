
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

#define HEAD_LEN 16u

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
        fprintf(stderr, "usage: %s <libpathlanglayout.so> <libpathspace.so>\n", argv[0]);
        return 2;
    }
    void *layout_dl = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!layout_dl) {
        fprintf(stderr, "dlopen layout 失败: %s\n", dlerror());
        return 2;
    }
    define_rwir = (define_rwir_fn)dlsym(layout_dl, "pathlangLayoutDefineRwir");
    if (!define_rwir) {
        fprintf(stderr, "layout 未导出 pathlangLayoutDefineRwir\n");
        return 2;
    }
    void *kv_dl = dlopen(argv[2], RTLD_NOW | RTLD_LOCAL);
    if (!kv_dl) {
        fprintf(stderr, "dlopen pathspace 失败: %s\n", dlerror());
        return 2;
    }
    kv_connect = (connect_fn)dlsym(kv_dl, "pathspaceConnect");
    kv_close = (close_fn)dlsym(kv_dl, "pathspaceClose");
    kv_resolve = (resolve_fn)dlsym(kv_dl, "pathspaceResolveOffset");
    kv_inproc = (inproc_fn)dlsym(kv_dl, "pathspaceXvalueInProcessAddress");
    if (!kv_connect || !kv_close || !kv_resolve || !kv_inproc) {
        fprintf(stderr, "pathspace 缺少所需符号\n");
        return 2;
    }

    char dsn[256];
    snprintf(dsn, sizeof dsn, "shm:///tmp/pathlang_layout_defrwir_%ld.shm", (long)getpid());
    char err[512];

    const char *reads1[] = {"int64"};
    const char *writes1[] = {"int64"};
    memset(err, 0, sizeof err);
    CHECK(define_rwir(dsn, "t·f", reads1, 1, writes1, 1, NULL, 0, err, sizeof err) == 0,
          "基本声明应成功: %s", err);

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

    {
        void *kv = kv_connect(dsn);
        if (kv) {
            uint8_t wire[512];
            uint64_t total = read_wire(kv, "/lib/t·f", wire, sizeof wire);
            CHECK(total != 0, "/lib/t·f 路由头读不到");
            if (total) {
                const uint8_t *body = NULL;
                uint64_t bl = body_of(wire, total, &body);
                
                CHECK(bl > 0, "/lib/t·f 路由头 body 不该为空");
            }
            kv_close(kv);
        }
    }

    memset(err, 0, sizeof err);
    CHECK(define_rwir(dsn, "", reads1, 1, writes1, 1, NULL, 0, err, sizeof err) == -1,
          "空 opcode 应返回 -1");
    CHECK(err[0] != '\0', "空 opcode 应写 err");

    memset(err, 0, sizeof err);
    CHECK(define_rwir(dsn, "t/bad", reads1, 1, writes1, 1, NULL, 0, err, sizeof err) == -1,
          "opcode 含 '/' 应返回 -1（err=%s）", err);
    CHECK(err[0] != '\0', "opcode 含 '/' 应写 err");

    memset(err, 0, sizeof err);
    CHECK(define_rwir(dsn, "t·null", NULL, 2, writes1, 1, NULL, 0, err, sizeof err) == -1,
          "read_count=2 但数组为 NULL 应返回 -1（err=%s）", err);

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

    const char *writes3[] = {"int64"};
    const uint8_t bitmap3[1] = {1};
    memset(err, 0, sizeof err);
    CHECK(define_rwir(dsn, "t·ip", reads1, 1, writes3, 1, bitmap3, 0, err, sizeof err) == 0,
          "带 inplace 的声明应成功: %s", err);
    {
        void *kv = kv_connect(dsn);
        if (kv) {
            
            int line = 0;
            expect_slot(kv, "/lib/t·ip/[0,1]", "int64 inplace", &line);
            kv_close(kv);
        }
    }

    const char *reads_multi[] = {"int64", "[]char/utf8"};
    memset(err, 0, sizeof err);
    CHECK(define_rwir(dsn, "t·va", reads_multi, 2, writes1, 1, NULL, 1, err, sizeof err) == 0,
          "变参声明应成功: %s", err);
    {
        void *kv = kv_connect(dsn);
        if (kv) {
            int line = 0;
            expect_slot(kv, "/lib/t·va/[0,-1]", "int64", &line);
            
            expect_slot(kv, "/lib/t·va/[0,-2]", "[]char/utf8", &line);
            kv_close(kv);
        }
    }

    memset(err, 0, sizeof err);
    CHECK(define_rwir(dsn, "t·vb", NULL, 0, NULL, 0, NULL, 1, err, sizeof err) == -1,
          "无读参却声明变参应返回 -1（err=%s）", err);

    char path[256];
    snprintf(path, sizeof path, "/tmp/pathlang_layout_defrwir_%ld.shm", (long)getpid());
    unlink(path);

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("ok");
    return 0;
}
