/*
 * 02_cpp — C++ RAII 封装：extern "C" 直接调用 libkvspace dispatch 前端
 *
 * head 固定 16 字节 [bodylen:u64@0][attrslen:u16@8][padding:6@10-15]，body 偏移恒 16，
 * langtype 在 body 之后的 attr 区。bodylen 恒为有效字节数，容量归存储后端。
 *
 * 编译: g++ -std=c++17 02_cpp.cpp -lkvspace
 */

extern "C" {
    #include <kvspace/kvspace.h>
    #include <kvspace/kvspace_xvalue.h>
    #include <kvlangxvalue.h>
#include <kvlangxvalue.h>
}

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

struct KV {
    void *kv;
    std::string path;

    KV(const char *p) : path(p) {
        ::unlink(p);
        std::string dsn = "shm://" + path;
        kv = kvspaceConnect(dsn.c_str());
        if (!kv) throw std::runtime_error("connect failed");
    }
    ~KV() { kvspaceClose(kv); ::unlink(path.c_str()); }

    // 写入：realloc 出 body+attr 空间，再直接写 base+offset。
    void set(const char *key, const char *langtype, const uint8_t *body, uint32_t len) {
        const uint16_t lt_len = (langtype && langtype[0]) ? (uint16_t)strlen(langtype) : 0;
        const uint64_t attr_len = lt_len ? 3ull + lt_len : 0ull;
        uint64_t offset = 0; char err[256];
        if (kvspaceRealloc(kv, key, len, attr_len, &offset, err, sizeof(err)) != KVSPACE_OK)
            throw std::runtime_error(std::string("set ") + key + ": " + err);
        uint8_t *base = kvspaceXvalueInProcessAddress(kv);
        if (!base) throw std::runtime_error("no in-process address space");
        uint8_t *wire = base + offset;
        if (len && body) memcpy(wire + KVSPACE_XVALUE_HEAD_LEN, body, len);
        if (lt_len) {
            uint8_t *a = wire + KVSPACE_XVALUE_HEAD_LEN + len;
            a[0] = KVLANG_XVALUE_ATTR_TAG_LANGTYPE;
            a[1] = (uint8_t)lt_len;
            a[2] = (uint8_t)(lt_len >> 8);
            memcpy(a + 3, langtype, lt_len);
        }
    }
    // 读：resolve 拿 offset，在 base+offset 处解 head/body/attr。
    bool get(const char *key, std::string &kind, std::string &raw) {
        uint64_t offset = 0; char err[256];
        if (kvspaceResolveOffset(kv, key, 0, &offset, err, sizeof(err)) != KVSPACE_OK)
            return false;
        const uint8_t *base = kvspaceXvalueInProcessAddress(kv);
        if (!base) return false;
        const uint8_t *wire = base + offset;
        const auto *h = reinterpret_cast<const kvspaceXvalueHead *>(wire);
        const uint64_t total = kvspaceXvalueHeadTotalLen(h);
        if (total == 0) return false;
        const uint8_t *lt = nullptr;
        /* 返回字节数；0 表示没有 langtype。早先这里把返回值丢掉了，lt_len 一直
         * 是 0，于是 kind 永远为空——「取到了却不用」比取不到更难察觉。 */
        const uint16_t lt_len = kvlangXvalueAttrLangtypeBounded(wire, total, &lt);
        if (lt_len == 0 || lt == nullptr) return false;
        const char *k = reinterpret_cast<const char *>(lt);
        uint16_t k_len = lt_len;
        if (k != nullptr && k_len != 0) {
            // 去掉 `*`/`@` 前缀与 `[dims]` 形状段，只留基种类。
            if (k[0] == '*' || k[0] == '@') { k++; k_len--; }
            if (k_len != 0 && k[0] == '[') {
                const char *close = static_cast<const char *>(memchr(k, ']', k_len));
                if (close != nullptr) {
                    const size_t skip = static_cast<size_t>(close - k) + 1U;
                    k += skip;
                    k_len = static_cast<uint16_t>(k_len - skip);
                }
            }
        }
        kind.assign(k != nullptr ? k : "", k_len);
        raw.assign(reinterpret_cast<const char *>(wire + KVSPACE_XVALUE_HEAD_LEN),
                   static_cast<size_t>(kvspaceXvalueHeadLen(h)));
        return true;
    }
    void del(const char *key) { char err[256]; kvspaceFree(kv, key, err, sizeof(err)); }
    void deltree(const char *p) { char err[256]; kvspaceDeltree(kv, p, err, sizeof(err)); }
    void mkindex(const char *p) { kvspaceMkindex(kv, p, nullptr, 0); }
    // 目录：listlen 给出 N/M，list 一次取全部名字（NUL 分隔），拷入 vector。
    std::vector<std::string> list(const char *prefix) {
        std::vector<std::string> r;
        int64_t count = 0; uint32_t max_name = 0; char err[256];
        if (kvspaceListlen(kv, prefix, &count, &max_name, err, sizeof(err)) != KVSPACE_OK ||
            count <= 0)
            return r;
        std::vector<char> names(static_cast<size_t>(count) * (max_name + 1));
        uint32_t length = 0, listed = 0; uint64_t generation = 0;
        if (kvspaceList(kv, prefix, names.data(), static_cast<uint32_t>(names.size()),
                        &length, &listed, &generation, err, sizeof(err)) != KVSPACE_OK)
            return r;
        for (uint32_t i = 0; i < length;) {
            const char *name = names.data() + i;
            const size_t n = strlen(name);
            if (n) r.emplace_back(name, n);
            i += static_cast<uint32_t>(n) + 1;
        }
        return r;
    }
};

int main() {
    auto xv_int = [](int64_t v) {
        uint8_t *b = static_cast<uint8_t *>(malloc(sizeof v));
        memcpy(b, &v, sizeof v);
        return std::pair<uint8_t *, uint32_t>{b, static_cast<uint32_t>(sizeof v)};
    };
    auto xv_str = [](const char *s) {
        const uint32_t n = static_cast<uint32_t>(strlen(s));
        uint8_t *b = static_cast<uint8_t *>(malloc(n ? n : 1));
        if (n) memcpy(b, s, n);
        return std::pair<uint8_t *, uint32_t>{b, n};
    };

    KV kv("/tmp/kvspace_cpp.shm");
    kv.mkindex("/cpp/");

    auto [vi, li] = xv_int(42);
    kv.set("/cpp/a", "int64", vi, li);
    free(vi);

    auto [vs, ls] = xv_str("hello");
    kv.set("/cpp/b", "char/utf8", vs, ls);
    free(vs);

    std::string kind, raw;
    if (kv.get("/cpp/a", kind, raw))
        printf("/cpp/a  kind=%s val=%ld\n", kind.c_str(), *(const int64_t *)raw.data());
    if (kv.get("/cpp/b", kind, raw))
        printf("/cpp/b  kind=%s val=%.*s\n", kind.c_str(), (int)raw.size(), raw.data());

    auto ns = kv.list("/cpp/");
    printf("list /cpp/:");
    for (auto &n : ns) printf(" %s", n.c_str());
    printf(" (count=%zu)\n", ns.size());

    kv.deltree("/cpp/");
    printf("deltree ok, get /cpp/a: %s\n", kv.get("/cpp/a", kind, raw) ? "EXISTS" : "NULL");

    printf("PASS cpp\n");
    return 0;
}
