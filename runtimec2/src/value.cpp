#include "value.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <kvspace/kvspace.h>
#include <kvspace/xvalue_head.h>

namespace kvlang::runtimec2 {

bool v_is_none(Wire w) noexcept {
    if (!w.d || w.n == 0)
        return true;
    // 对齐 C 的 xh_is_none：flags 无 class/ptr 位 且 langtype 首字节为 0。
    return w.n >= 19 && w.d[1] == 0 && w.d[18] == 0;
}

bool v_ptr(Wire w) noexcept {
    kvspaceXh xh{};
    if (!w.d || kvspaceXhDecode(w.d, w.n, &xh) != 0)
        return false;
    return (xh.kind & KVSPACE_XH_PTR_FLAG) != 0;
}

bool v_ptr_target(Wire w, std::string* out) noexcept {
    kvspaceXh xh{};
    if (!w.d || kvspaceXhDecode(w.d, w.n, &xh) != 0)
        return false;
    if ((xh.kind & KVSPACE_XH_PTR_FLAG) == 0 || !xh.body)
        return false;
    out->assign(reinterpret_cast<const char*>(xh.body), xh.content_len);
    return true;
}

bool v_langtype(Wire w, std::string* out) noexcept {
    kvspaceXh xh{};
    if (!w.d || kvspaceXhDecode(w.d, w.n, &xh) != 0)
        return false;
    out->assign(reinterpret_cast<const char*>(xh.langtype), xh.langtype_len);
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

bool v_scalar(Wire w, Scalar* out) noexcept {
    kvspaceXh xh{};
    if (!w.d || kvspaceXhDecode(w.d, w.n, &xh) != 0)
        return false;
    std::string lt(reinterpret_cast<const char*>(xh.langtype), xh.langtype_len);
    if (!kind_of_langtype(lt, out))
        return false;
    if (out->k == Scalar::K::Char)
        return false;  // 字符串按文本读取，不走标量通道
    if (!xh.body || xh.content_len < out->width)
        return false;
    std::uint64_t raw = 0;
    std::memcpy(&raw, xh.body, out->width);
    switch (out->k) {
    case Scalar::K::Int:
        out->i = (out->width == 1)   ? (std::int8_t)raw
                 : (out->width == 2) ? (std::int16_t)raw
                 : (out->width == 4) ? (std::int32_t)raw
                                     : (std::int64_t)raw;
        break;
    case Scalar::K::Uint:
    case Scalar::K::Bool:
        out->u = raw;
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
        break;
    case Scalar::K::Uint:
    case Scalar::K::Bool:
        s.u = raw;
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

bool v_encode(const char* kind, std::uint8_t width, const Scalar& s,
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
    return kvspaceXvalueEncode(kind, raw, width, nullptr, 0, out, out_len) == 0;
}

bool v_write_scalar(void* h, const std::string& key, const char* kind,
                    std::uint8_t width, const Scalar& s) noexcept {
    std::uint8_t* buf = nullptr;
    std::uint32_t len = 0;
    if (!v_encode(kind, width, s, &buf, &len))
        return false;
    char err[192] = {};
    int rc = kvspaceSetValue(h, key.c_str(), buf, len, 0, 0, err, sizeof err);
    std::free(buf);
    return rc == 0;
}

}  // namespace kvlang::runtimec2
