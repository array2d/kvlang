#include "value.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

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

/* v2 固定 8 字节头：[len:u48 LE@0-5][storetype@6][ptr@7][body@8]。 */
static std::uint64_t xh_len(Wire w) noexcept {
    if (!w.d || w.n < KVSPACE_XVALUE_HEAD_LEN)
        return 0;
    std::uint64_t v = 0;
    for (int i = 5; i >= 0; i--)
        v = (v << 8) | w.d[i];
    return v;
}

std::uint16_t wire_langtype_view(const std::uint8_t* d, std::uint64_t total_len,
                                 const std::uint8_t** out_p) noexcept {
    if (out_p)
        *out_p = nullptr;
    if (!d || total_len < KVSPACE_XVALUE_HEAD_LEN + 2u)
        return 0;
    const std::uint64_t body_len = kvspaceXvalueHeadLen(
        reinterpret_cast<const kvspaceXvalueHead*>(d));
    if (body_len > total_len - KVSPACE_XVALUE_HEAD_LEN - 2u)
        return 0;
    const std::uint64_t attr_off = KVSPACE_XVALUE_HEAD_LEN + body_len;
    const std::uint32_t attr_len =
        static_cast<std::uint32_t>(d[attr_off]) |
        (static_cast<std::uint32_t>(d[attr_off + 1]) << 8);
    if (attr_len < 2u || attr_off + attr_len > total_len)
        return 0;
    const std::uint8_t* p = nullptr;
    const std::uint16_t n = kvspaceXvalueHeadLangtype(d, body_len, &p);
    if (!n || !p)
        return 0;
    if (out_p)
        *out_p = p;
    return n;
}

bool v_is_none(Wire w) noexcept {
    if (!w.d || w.n < KVSPACE_XVALUE_HEAD_LEN)
        return true;
    if (w.d[7] == KVSPACE_XVALUE_PTR_PTR || w.d[7] == KVSPACE_XVALUE_PTR_EXT ||
        xh_len(w) != 0)
        return false;
    /* Empty inline body: only empty `langtype`, `"None"`, or `*T` (null pointer in a Ptr slot)
       is `None`; empty-body typed values (struct/map markers, empty strings/arrays, `def` headers) are concrete. */
    if (w.n < KVSPACE_XVALUE_HEAD_LEN + 2)
        return true;
    const std::uint16_t alen = static_cast<std::uint16_t>(
        w.d[KVSPACE_XVALUE_HEAD_LEN] | (w.d[KVSPACE_XVALUE_HEAD_LEN + 1] << 8));
    if (alen < 2 || KVSPACE_XVALUE_HEAD_LEN + static_cast<std::uint32_t>(alen) > w.n)
        return true;
    const std::uint8_t* ltp = nullptr;
    const std::uint16_t ltl = wire_langtype_view(w.d, w.n, &ltp);
    if (!ltl || !ltp)
        return true;
    const std::string_view lt(reinterpret_cast<const char*>(ltp), ltl);
    return lt == "None" || lt[0] == '*';
}

bool is_num_scalar_type(std::string_view lt) noexcept {
    return lt == "int8" || lt == "int16" || lt == "int32" || lt == "int64" ||
           lt == "uint8" || lt == "uint16" || lt == "uint32" || lt == "uint64" ||
           lt == "float32" || lt == "float64";
}

bool v_ptr(Wire w) noexcept {
    return w.d && w.n >= KVSPACE_XVALUE_HEAD_LEN &&
           w.d[7] == KVSPACE_XVALUE_PTR_PTR;
}

bool v_ptr_target(Wire w, std::string* out) noexcept {
    if (!w.d || w.n < KVSPACE_XVALUE_HEAD_LEN ||
        w.d[7] != KVSPACE_XVALUE_PTR_PTR)
        return false;
    const std::uint64_t len = xh_len(w);
    const std::uint8_t* b = w.d + KVSPACE_XVALUE_HEAD_LEN;
    out->assign(reinterpret_cast<const char*>(b),
                static_cast<std::size_t>(len));
    return true;
}

namespace {

// langtype（可能带 [dims] 前缀）→ 标量类别与宽度。
bool kind_of_langtype(const std::string& lt, Scalar* s) {
    std::string k = lt;
    auto bracket = k.rfind(']');
    if (!k.empty() && k[0] == '[' && bracket != std::string::npos)
        k = k.substr(bracket + 1);
    s->width = 0;
    if (k == "bool") {
        s->k = Scalar::K::Bool;
        s->width = 1;
    } else if (k == "int8" || k == "int16" || k == "int32" || k == "int64") {
        s->k = Scalar::K::Int;
        s->width = k == "int8" ? 1 : k == "int16" ? 2 : k == "int32" ? 4 : 8;
    } else if (k == "uint8" || k == "uint16" || k == "uint32" || k == "uint64") {
        s->k = Scalar::K::Uint;
        s->width = k == "uint8" ? 1 : k == "uint16" ? 2 : k == "uint32" ? 4 : 8;
    } else if (k == "float32" || k == "float64") {
        s->k = Scalar::K::Float;
        s->width = k == "float32" ? 4 : 8;
    } else if (k.rfind("char/", 0) == 0) {
        s->k = Scalar::K::Char;
        s->width = 0;
    } else {
        return false;
    }
    return true;
}

}  // namespace

bool v_scalar(Wire w, std::string_view langtype, Scalar* out) noexcept {
    if (!w.d || w.n < KVSPACE_XVALUE_HEAD_LEN)
        return false;
    std::string lt(langtype);
    if (!kind_of_langtype(lt, out))
        return false;
    if (out->k == Scalar::K::Char)
        return false;  // 字符串按文本读取，不走标量通道
    const std::uint64_t clen = xh_len(w);
    const std::uint8_t* body = w.d + KVSPACE_XVALUE_HEAD_LEN;
    if (clen < out->width)
        return false;
    std::uint64_t raw = 0;
    std::memcpy(&raw, body, out->width);
    switch (out->k) {
    case Scalar::K::Int:
        out->i = (out->width == 1)   ? (std::int8_t)raw
                 : (out->width == 2) ? (std::int16_t)raw
                 : (out->width == 4) ? (std::int32_t)raw
                                     : (std::int64_t)raw;
        out->u = static_cast<std::uint64_t>(out->i);
        break;
    case Scalar::K::Uint:
    case Scalar::K::Bool:
        out->u = raw;
        out->i = static_cast<std::int64_t>(raw);
        break;
    case Scalar::K::Float:
        if (out->width == 4) {
            float f;
            std::uint32_t u32 = (std::uint32_t)raw;
            std::memcpy(&f, &u32, 4);
            out->f = f;
        } else {
            std::memcpy(&out->f, &raw, 8);
        }
        break;
    default:
        return false;
    }
    return true;
}

bool v_scalar_view(const std::uint8_t* langtype, std::size_t lt_len,
                   const std::uint8_t* body, std::uint64_t cap,
                   Scalar* out) noexcept {
    const char* p = reinterpret_cast<const char*>(langtype);
    std::string lt(p, ::strnlen(p, lt_len));
    Scalar s{};
    if (!kind_of_langtype(lt, &s))
        return false;
    if (!v_scalar_raw(s.k, s.width, body, cap, &s))
        return false;
    *out = s;
    return true;
}

bool v_scalar_raw(Scalar::K k, std::uint8_t width, const std::uint8_t* body,
                  std::uint64_t cap, Scalar* out) noexcept {
    if (!body || cap < width || width == 0)
        return false;
    std::uint64_t raw = 0;
    std::memcpy(&raw, body, width);
    Scalar s{};
    s.k = k;
    s.width = width;
    switch (k) {
    case Scalar::K::Int:
        s.i = width == 1 ? (std::int8_t)raw
              : width == 2 ? (std::int16_t)raw
              : width == 4 ? (std::int32_t)raw
                           : (std::int64_t)raw;
        s.u = static_cast<std::uint64_t>(s.i);
        break;
    case Scalar::K::Uint:
    case Scalar::K::Bool:
        s.u = raw;
        s.i = static_cast<std::int64_t>(raw);
        break;
    case Scalar::K::Float:
        if (width == 4) {
            float f;
            std::uint32_t u32 = (std::uint32_t)raw;
            std::memcpy(&f, &u32, 4);
            s.f = f;
        } else {
            std::memcpy(&s.f, &raw, 8);
        }
        break;
    default:
        return false;
    }
    *out = s;
    return true;
}

bool v_to_wire(const char* kind, std::uint8_t width, const Scalar& s,
               std::uint8_t** out, std::uint32_t* out_len) noexcept {
    std::uint8_t raw[8] = {};
    std::uint64_t u = 0;
    switch (s.k) {
    case Scalar::K::Int:
        u = (std::uint64_t)s.i;
        break;
    case Scalar::K::Uint:
    case Scalar::K::Bool:
        u = s.u;
        break;
    case Scalar::K::Float:
        if (width == 4) {
            float f = (float)s.f;
            std::uint32_t u32;
            std::memcpy(&u32, &f, 4);
            u = u32;
        } else {
            std::memcpy(&u, &s.f, 8);
        }
        break;
    default:
        return false;
    }
    std::memcpy(raw, &u, width);
    std::uint8_t* buf = nullptr;
    std::uint64_t total = 0;
    if (kvspaceXvalueHeadNewScalar(0, raw, width, kind, &buf, &total) != 0 ||
        total > UINT32_MAX) {
        *out = nullptr;
        *out_len = 0;
        return false;
    }
    *out = buf;
    *out_len = static_cast<std::uint32_t>(total);
    return true;
}

bool v_write_scalar(void* h, const std::string& key, const char* kind,
                    std::uint8_t width, const Scalar& s) noexcept {
    std::uint8_t* buf = nullptr;
    std::uint32_t len = 0;
    if (!v_to_wire(kind, width, s, &buf, &len))
        return false;
    char err[192] = {};
    int rc = kvspaceSetValue(h, key.c_str(), buf, len, err, sizeof err);
    std::free(buf);
    return rc == 0;
}

bool coerce_scalar_wire(std::string_view dst_type, Wire src,
                        std::uint8_t** owned_buf, Wire* out, std::string* err) {
    *owned_buf = nullptr;
    *out = src;
    const bool is_num = is_num_scalar_type(dst_type);
    const bool is_bool = dst_type == "bool";
    if (!is_num && !is_bool)
        return true;
    if (v_is_none(src)) {
        if (err) {
            *err = is_num ? (std::string("TypeError: cannot write None to ") +
                             std::string(dst_type) + " slot")
                          : std::string("TypeError: cannot assign None to bool");
        }
        return false;
    }
    const std::uint8_t* sltp = nullptr;
    const std::uint16_t sltl = wire_langtype_view(src.d, src.n, &sltp);
    const std::string_view slt =
        (sltp && sltl) ? std::string_view(reinterpret_cast<const char*>(sltp), sltl)
                       : std::string_view();
    if (!v_ptr(src) && slt == dst_type)
        return true;
    std::string_view skind = slt;
    if (v_ptr(src)) {
        skind = slt.empty() ? std::string_view("*") : slt;
    } else if (!slt.empty() && slt.front() == '[') {
        const auto close = slt.find(']');
        if (close != std::string_view::npos &&
            slt.find("char/") != std::string_view::npos)
            skind = slt.substr(close + 1);
    } else if (skind.empty()) {
        skind = "None";
    }
    if (v_ptr(src) || (!slt.empty() && slt.front() == '[') || is_bool) {
        if (err) {
            *err = "TypeError: cannot assign " + std::string(skind) + " to " +
                   std::string(dst_type);
        }
        return false;
    }
    Scalar sc{};
    if (!v_scalar(src, slt, &sc) ||
        (sc.k != Scalar::K::Int && sc.k != Scalar::K::Uint &&
         sc.k != Scalar::K::Float)) {
        if (err) {
            *err = "TypeError: cannot assign " + std::string(skind) + " to " +
                   std::string(dst_type);
        }
        return false;
    }
    Scalar dst{};
    const std::string dst_str(dst_type);
    if (!kind_of_langtype(dst_str, &dst))
        return false;
    if (dst.k == Scalar::K::Float) {
        dst.f = sc.k == Scalar::K::Float  ? sc.f
                : sc.k == Scalar::K::Uint ? static_cast<double>(sc.u)
                                          : static_cast<double>(sc.i);
    } else if (dst.k == Scalar::K::Uint) {
        dst.u = sc.k == Scalar::K::Float
                    ? static_cast<std::uint64_t>(static_cast<std::int64_t>(sc.f))
                : sc.k == Scalar::K::Uint ? sc.u
                                          : static_cast<std::uint64_t>(sc.i);
        dst.i = static_cast<std::int64_t>(dst.u);
    } else {
        dst.i = sc.k == Scalar::K::Float  ? static_cast<std::int64_t>(sc.f)
                : sc.k == Scalar::K::Uint ? static_cast<std::int64_t>(sc.u)
                                          : sc.i;
        dst.u = static_cast<std::uint64_t>(dst.i);
    }
    std::uint32_t out_len = 0;
    if (!v_to_wire(dst_str.c_str(), dst.width, dst, owned_buf, &out_len) ||
        !*owned_buf) {
        if (err)
            *err = "TypeError: cannot assign " + std::string(skind) + " to " +
                   dst_str;
        return false;
    }
    *out = {*owned_buf, out_len};
    return true;
}

}  // namespace kvlang::runtimec2
