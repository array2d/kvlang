#include "valueio.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <unordered_map>

#include <kvspace/kvspace.h>
#include <kvspace/xvalue_head.h>

/* v2 head 构造器：系统头装 v2 前由本地声明桥接（v2 头已带 KVSPACE_XVALUE_HEAD_LEN）。 */
#ifndef KVSPACE_XVALUE_HEAD_LEN
extern "C" {
int kvspaceXvalueHeadNewNone(uint8_t **out, uint64_t *out_len);
int kvspaceXvalueHeadNewScalar(uint8_t storetype, const uint8_t *raw,
                               uint32_t raw_len, uint8_t **out,
                               uint64_t *out_len);
int kvspaceXvalueHeadNewSlack(const uint8_t *data, uint64_t data_len,
                              uint64_t cap, uint8_t **out, uint64_t *out_len);
int kvspaceXvalueHeadNewTensor(const uint64_t *dims, uint32_t ndim,
                               const uint8_t *raw, uint64_t raw_len,
                               uint8_t **out, uint64_t *out_len);
int kvspaceXvalueHeadNewCode(const uint8_t *body, uint64_t body_len,
                             uint64_t cap, uint8_t **out, uint64_t *out_len);
}
#endif

namespace kvlang::runtimec2 {
namespace {

std::string keyz(std::string_view k) { return std::string(k); }

void put_utf8(std::string* out, std::uint32_t cp) {
    if (cp < 0x80) {
        out->push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out->push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out->push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

}  // namespace

bool read_wire(void* h, std::string_view key, const std::uint8_t** data,
               std::uint32_t* len) noexcept {
    *data = nullptr;
    *len = 0;
    auto k = keyz(key);
    std::uint8_t* d = nullptr;
    std::uint32_t n = 0;
    if (kvspaceGet(h, k.c_str(), 1, &d, &n) != 0)
        return false;
    *data = d;
    *len = n;
    return d != nullptr && n > 0;
}

namespace {

// Process-private key -> view token cache (P2 protocol). Values stay in
// kvspace; a hit costs one ViewCheck plus a head compare instead of a walk.
struct SvHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view s) const noexcept {
        return std::hash<std::string_view>{}(s);
    }
};

struct ViewEnt {
    kvspaceViewToken_t tok{};
    std::uint32_t total = 0;  // 0 = not cacheable, read through kvspaceGet
    bool is_bool = false;     // kvspaceGet rejects a bool body > 1; so must a hit
    std::uint8_t hn = 0;
    std::uint8_t head[128];
};

struct ViewCache {
    void* h = nullptr;
    int state = 0;  // 0 = idle, 1 = live, -1 = backend has no views
    std::uint32_t epoch = 0;
    std::unordered_map<std::string, ViewEnt, SvHash, std::equal_to<>> m;
};

thread_local ViewCache g_vc;
constexpr std::size_t kViewCacheMax = 1u << 16;

const char* zkey(std::string_view k) {
    thread_local std::string b;
    b.assign(k);
    return b.c_str();
}

void vc_ack(void* h) {
    if (kvspaceViewAck(h, &g_vc.epoch) == 1) {
        g_vc.m.clear();
        kvspaceViewAck(h, &g_vc.epoch);
    }
}

bool vc_live(void* h) {
    if (g_vc.h != h) {
        view_cache_idle(g_vc.h);
        g_vc.h = h;
        g_vc.state = 0;
    }
    if (g_vc.state == 0) {
        if (kvspaceViewQuiesce(h, 0) != 0) {
            g_vc.state = -1;
            return false;
        }
        g_vc.state = 1;
        vc_ack(h);
    }
    return g_vc.state > 0;
}

// Whole wire length for scalars and tensors, whose validity kvspaceGet decides
// from the head alone; 0 = read through kvspaceGet.
std::uint32_t wire_total(const kvspaceViewToken_t& t) {
    if (t.headlen != KVSPACE_XVALUE_HEAD_LEN)
        return 0;
    const std::uint8_t* hd = t.head;
    const std::uint8_t st = hd[6];
    /* 只缓存 SHORT/LARGE 定长值（SLACK 长度可变，视图缓存会 stale）。 */
    if (st != KVSPACE_XVALUE_ST_SHORT && st != KVSPACE_XVALUE_ST_LARGE)
        return 0;
    std::uint64_t len = kvspaceXvalueHeadLen(
        reinterpret_cast<const kvspaceXvalueHead*>(hd));
    const std::uint64_t total = (std::uint64_t)t.headlen + len;
    if (total > t.cap || total > 0x7fffffffu)
        return 0;
    return static_cast<std::uint32_t>(total);
}

}  // namespace

void view_cache_tick() noexcept {
    if (g_vc.state > 0)
        vc_ack(g_vc.h);
}

void view_cache_idle_all() noexcept { view_cache_idle(g_vc.h); }

void view_cache_idle(void* h) noexcept {
    if (h && g_vc.h == h && g_vc.state == 1) {
        g_vc.m.clear();
        kvspaceViewQuiesce(h, 1);
        g_vc.state = 0;
        g_vc.h = nullptr;
    }
}

bool read_wire_raw(void* h, std::string_view key, const std::uint8_t** data,
                   std::uint32_t* len) noexcept {
    *data = nullptr;
    *len = 0;
    // System vars (`‥`) are volatile by contract: always read through.
    const bool cacheable =
        vc_live(h) && key.find("\xE2\x80\xA5") == std::string_view::npos;
    if (cacheable) {
        auto it = g_vc.m.find(key);
        if (it != g_vc.m.end()) {
            ViewEnt& e = it->second;
            if (e.total == 0)
                goto slow;
            if (kvspaceViewCheck(h, &e.tok) == 0 &&
                std::memcmp(e.tok.head, e.head, e.hn) == 0 &&
                !(e.is_bool && e.tok.body[0] > 1)) {
                *data = e.tok.head;
                *len = e.total;
                return true;
            }
            g_vc.m.erase(it);
        }
        if (g_vc.m.size() >= kViewCacheMax)
            g_vc.m.clear();
        ViewEnt e;
        if (kvspaceViewResolve(h, zkey(key), &e.tok) == 0)
            e.total = wire_total(e.tok);
        if (e.total) {
            e.hn = static_cast<std::uint8_t>(e.tok.headlen < sizeof e.head
                                                 ? e.tok.headlen
                                                 : sizeof e.head);
            std::memcpy(e.head, e.tok.head, e.hn);
            e.is_bool = false; /* v2 head 不含 langtype，无法从 wire 判 bool */
            *data = e.tok.head;
            *len = e.total;
            g_vc.m.emplace(std::string(key), e);
            return true;
        }
        g_vc.m.emplace(std::string(key), e);
    }
slow:
    std::uint8_t* d = nullptr;
    std::uint32_t n = 0;
    if (kvspaceGet(h, zkey(key), 0, &d, &n) != 0)
        return false;
    *data = d;
    *len = n;
    return d != nullptr && n > 0;
}

bool read_langtype(void* h, std::string_view key, std::string* out) noexcept {
    out->clear();
    auto k = keyz(key);
    kvspaceHead_t head{};
    if (kvspaceGetHead(h, k.c_str(), &head) != 0 || head.langtype_len <= 0)
        return false;
    out->assign(head.langtype, static_cast<std::size_t>(head.langtype_len));
    return true;
}

bool read_text(void* h, std::string_view key, std::string* out) noexcept {
    std::string lt;
    // 系统键（末段含 ‥：status/pc/msg/‥lib/…）无 attr——无类型，按纯字节文本读。
    // 有 attr 则按 langtype 解码（char/utf32 → UTF-8）。缺失 attr 不是错误。
    read_langtype(h, key, &lt);
    const std::uint8_t* d = nullptr;
    std::uint32_t n = 0;
    if (!read_wire(h, key, &d, &n))
        return false;
    return wire_text(d, n, lt, out);
}

bool wire_text(const std::uint8_t* d, std::uint32_t n,
               std::string_view langtype, std::string* out) noexcept {
    if (!d || n < KVSPACE_XVALUE_HEAD_LEN)
        return false;
    std::uint64_t len = kvspaceXvalueHeadLen(
        reinterpret_cast<const kvspaceXvalueHead*>(d));
    const std::uint8_t* b = d + KVSPACE_XVALUE_HEAD_LEN;
    out->clear();
    if (langtype.find("char/utf32") != std::string_view::npos) {
        const auto* p = reinterpret_cast<const std::uint32_t*>(b);
        for (std::uint64_t i = 0; i < len / 4; i++)
            put_utf8(out, p[i]);
        return true;
    }
    out->assign(reinterpret_cast<const char*>(b), static_cast<std::size_t>(len));
    return true;
}

bool write_text(void* h, std::string_view key, std::string_view text) noexcept {
    std::uint8_t* buf = nullptr;
    std::uint64_t len = 0;
    if (kvspaceXvalueHeadNewSlack(
            reinterpret_cast<const std::uint8_t*>(text.data()),
            static_cast<std::uint64_t>(text.size()),
            static_cast<std::uint64_t>(text.size()), &buf, &len) != 0 ||
        !buf)
        return false;
    auto k = keyz(key);
    char err[192] = {};
    int rc = kvspaceSetValue(h, k.c_str(), buf, static_cast<std::uint32_t>(len), err, sizeof err);
    std::free(buf);
    return rc == 0;
}

bool read_rwir(void* h, std::string_view key, std::string* op, int* nr,
               int* nw) noexcept {
    std::string lt;
    if (!read_langtype(h, key, &lt) || lt != "rwir")
        return false;
    const std::uint8_t* d = nullptr;
    std::uint32_t n = 0;
    if (!read_wire(h, key, &d, &n))
        return false;
    if (!d || n < KVSPACE_XVALUE_HEAD_LEN)
        return false;
    std::uint64_t clen = kvspaceXvalueHeadLen(
        reinterpret_cast<const kvspaceXvalueHead*>(d));
    if (clen < 5)
        return false;
    const std::uint8_t* b = d + KVSPACE_XVALUE_HEAD_LEN;
    *nr = static_cast<int>(b[0] | (b[1] << 8));
    *nw = static_cast<int>(b[2] | (b[3] << 8));
    const char* opb = reinterpret_cast<const char*>(b + 5);
    std::size_t opn = 0, cap = static_cast<std::size_t>(clen - 5);
    while (opn < cap && opb[opn] != '\0')
        opn++;
    op->assign(opb, opn);
    return true;
}

bool read_rwfunc(void* h, std::string_view key, std::string* name, int* nr,
                 int* nw) noexcept {
    std::string lt;
    if (!read_langtype(h, key, &lt) || lt != "rwfunc")
        return false;
    const std::uint8_t* d = nullptr;
    std::uint32_t n = 0;
    if (!read_wire(h, key, &d, &n))
        return false;
    if (!d || n < KVSPACE_XVALUE_HEAD_LEN)
        return false;
    std::uint64_t clen = kvspaceXvalueHeadLen(
        reinterpret_cast<const kvspaceXvalueHead*>(d));
    if (clen < 5)
        return false;
    const std::uint8_t* b = d + KVSPACE_XVALUE_HEAD_LEN;
    *nr = static_cast<int>(b[0] | (b[1] << 8));
    *nw = static_cast<int>(b[2] | (b[3] << 8));
    const char* nb = reinterpret_cast<const char*>(b + 5);
    std::size_t cap = static_cast<std::size_t>(clen - 5), len = 0;
    while (len < cap && nb[len] != '\0')
        len++;
    name->assign(nb, len);
    return true;
}

bool frame_func_dir(void* h, std::string_view vid, int depth, std::string* out) noexcept {
    std::string key = "/vthread/";
    key.append(vid);
    key += "/[";
    key += std::to_string(depth);
    key += "]/\xE2\x80\xA5lib";
    out->clear();
    return read_text(h, key, out) && !out->empty();
}

bool read_int(void* h, std::string_view key, std::int64_t* out) noexcept {
    std::string s;
    if (!read_text(h, key, &s) || s.empty())
        return false;
    char* end = nullptr;
    long long v = std::strtoll(s.c_str(), &end, 10);
    if (end == s.c_str())
        return false;
    *out = v;
    return true;
}

bool write_int(void* h, std::string_view key, std::int64_t v) noexcept {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(v));
    return write_text(h, key, buf);
}

}  // namespace kvlang::runtimec2
