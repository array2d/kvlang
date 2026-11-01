#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include <kvspace/kvspace.h>
#include <kvspace/xvalue_head.h>

#include "vmkv.hpp"
#include <cinttypes>

#include "value.hpp"
#include "valueio.hpp"

namespace kvlang::runtimec2 {
namespace {

// 与 C runtime kvlangFormatFloat 逐字一致：%.16g → 不可回读则 %.17g，
// 再补 ".0"、去尾零。
void format_float(double v, char* out, std::size_t cap) {
    char s[64];
    std::snprintf(s, sizeof s, "%.16g", v);
    if (std::strtod(s, nullptr) != v)
        std::snprintf(s, sizeof s, "%.17g", v);
    std::snprintf(out, cap, "%s", s);
    if (std::strchr(out, 'e'))
        return;
    char* dot = std::strchr(out, '.');
    if (!dot) {
        std::size_t l = std::strlen(out);
        if (l + 2 < cap) {
            out[l] = '.';
            out[l + 1] = '0';
            out[l + 2] = 0;
        }
        return;
    }
    char* p = out + std::strlen(out) - 1;
    while (p > dot && *p == '0')
        *p-- = 0;
    if (p == dot) {
        p[1] = '0';
        p[2] = 0;
    }
}

char* dup(const std::string& s) {
    char* p = static_cast<char*>(std::malloc(s.size() + 1));
    if (p)
        std::memcpy(p, s.c_str(), s.size() + 1);
    return p;
}

std::string func_key(void* h, const Pc& p, int s1) {
    std::string dir;
    if (frame_func_dir(h, p.vid, p.depth, &dir))
        return g_vmkv.param_key(dir, p.s0, s1);
    return g_vmkv.param_key(g_vmkv.frame_root(p.vid, p.depth), p.s0, s1);
}

}  // namespace

// Format `opcode` + one line per read/write operand (empty line for literals; matches C `kvlangRwirextParams`).
char* params_impl(void* h, const char* pc) {
    Pc p;
    if (!h || !pc || !parse_pc(pc, &p))
        return dup("");
    std::string dir;
    if (!frame_func_dir(h, p.vid, p.depth, &dir)) {
        return dup("");
    }
    std::string op;
    int nr = 0, nw = 0;
    if (!read_rwir(h, g_vmkv.param_key(dir, p.s0, 0), &op, &nr, &nw))
        return dup("");
    auto slot_name = [&](int s1) -> std::string {
        const std::string sk = g_vmkv.param_key(dir, p.s0, s1);
        std::string v;
        int dnr = 0, dnw = 0;
        if (read_rwir(h, sk, &v, &dnr, &dnw)) {
            const auto sp = v.find(' ');
            return v.substr(0, sp);
        }
        return {};
    };
    std::string out = op;
    for (int i = 0; i < nr; i++) {
        out += "\n";
        out += slot_name(-(i + 1));
    }
    for (int i = 0; i < nw; i++) {
        out += "\n";
        out += slot_name(i + 1);
    }
    return dup(out);
}

char* resolve_read_impl(void* h, const char* pc, int idx) {
    Pc p;
    if (!h || !pc || !parse_pc(pc, &p) || idx < 0)
        return dup("");
    const std::string key = func_key(h, p, -(idx + 1));
    std::string var;
    int nr = 0, nw = 0;
    if (read_rwir(h, key, &var, &nr, &nw)) {
        // 变量操作数：按 * 描述符/裸名解析到帧栈成员，再取文本值。
        std::string target;
        const std::string frame = g_vmkv.frame_root(p.vid, p.depth);
        if (!var.empty() && var[0] == '*') {
            const std::string mk = frame + "/" + var.substr(1);
            const std::uint8_t* d = nullptr;
            std::uint32_t n = 0;
            if (!read_wire_raw(h, mk, &d, &n))  // Ptr 本体，禁穿透
                return dup("");
            Wire w{d, n};
            std::string t;
            if (!v_ptr_target(w, &t))
                return dup("");
            if (var.rfind("*[0,", 0) == 0) {
                const std::uint8_t* d2 = nullptr;
                std::uint32_t n2 = 0;
                if (!read_wire_raw(h, t, &d2, &n2))
                    return dup("");
                Wire w2{d2, n2};
                // 写参 Ptr 链：arg 槽 Ptr → 调用方槽（两跳）；读参传值：arg 槽即值槽
                // （单跳，本帧 view 缓存）。对齐 resolve_desc 的 fallback 语义。
                if (v_ptr_target(w2, &target)) {
                    /* 两跳 */
                } else {
                    target = t;
                }
            } else {
                target = t;
            }
        } else if (!var.empty() && var[0] == '/') {
            target = var;
        } else {
            target = frame + "/" + var;
        }
        // 值可能是标量：按 C 的显示规则格式化为十进制。
        const std::uint8_t* vd = nullptr;
        std::uint32_t vn = 0;
        std::string v;
        std::string lt;
        if (read_langtype(h, target, &lt) && read_wire(h, target, &vd, &vn)) {
            Wire w{vd, vn};
            Scalar sc{};
            if (v_scalar(w, lt, &sc)) {
                char buf[64];
                switch (sc.k) {
                case Scalar::K::Int:
                    std::snprintf(buf, sizeof buf, "%lld", (long long)sc.i);
                    return dup(buf);
                case Scalar::K::Bool:
                    return dup(sc.u ? "true" : "false");
                case Scalar::K::Uint:
                    std::snprintf(buf, sizeof buf, "%llu", (unsigned long long)sc.u);
                    return dup(buf);
                case Scalar::K::Float: {
                    format_float(sc.f, buf, sizeof buf);
                    return dup(buf);
                }
                default:
                    break;
                }
            }
        }
        if (!read_text(h, target, &v))
            return dup("");
        return dup(v);
    }
    std::string v;
    std::string lt;
    const std::uint8_t* kd = nullptr;
    std::uint32_t kn = 0;
    if (read_langtype(h, key, &lt) && read_wire(h, key, &kd, &kn)) {
        Wire w{kd, kn};
        Scalar sc{};
        if (v_scalar(w, lt, &sc)) {
            char buf[64];
            switch (sc.k) {
            case Scalar::K::Int:
                std::snprintf(buf, sizeof buf, "%lld", (long long)sc.i);
                return dup(buf);
            case Scalar::K::Bool:
                return dup(sc.u ? "true" : "false");
            case Scalar::K::Uint:
                std::snprintf(buf, sizeof buf, "%llu", (unsigned long long)sc.u);
                return dup(buf);
            case Scalar::K::Float:
                format_float(sc.f, buf, sizeof buf);
                return dup(buf);
            default:
                break;
            }
        }
    }
    if (!read_text(h, key, &v))
        return dup("");
    return dup(v);
}

// 读参 KV 路径：解析读参描述符 → 帧槽/调用方实例路径（供容器成员枚举
// read_str_list：networld/proc·exec(args) 读 stringkeymap 容器参数）。
char* resolve_read_path_impl(void* h, const char* pc, int idx) {
    Pc p;
    if (!h || !pc || !parse_pc(pc, &p) || idx < 0)
        return dup("");
    const std::string key = func_key(h, p, -(idx + 1));
    std::string var;
    int nr = 0, nw = 0;
    if (!read_rwir(h, key, &var, &nr, &nw))
        return dup("");
    const std::string frame = g_vmkv.frame_root(p.vid, p.depth);
    std::string target;
    if (!var.empty() && var[0] == '*') {
        // 参数 Ptr：arg 槽 Ptr → 调用方实例（容器参数 `args:*[int64]·[]char/utf32`）。
        const std::string mk = frame + "/" + var.substr(1);
        const std::uint8_t* d = nullptr;
        std::uint32_t n = 0;
        if (!read_wire_raw(h, mk, &d, &n))
            return dup("");
        Wire w{d, n};
        std::string t;
        if (!v_ptr_target(w, &t) || t.empty())
            return dup("");
        if (var.rfind("*[0,", 0) == 0) {
            const std::uint8_t* d2 = nullptr;
            std::uint32_t n2 = 0;
            if (!read_wire_raw(h, t, &d2, &n2))
                return dup("");
            Wire w2{d2, n2};
            if (v_ptr_target(w2, &target)) {
                /* 两跳：arg → 写参槽 → 调用方实例 */
            } else {
                target = t;
            }
        } else {
            target = t;
        }
    } else if (!var.empty() && var[0] == '/') {
        target = var;
    } else {
        target = frame + "/" + var;
    }
    return dup(target);
}

namespace {

bool format_member_key_float_ext(double value, std::uint8_t width,
                                 std::string* out) {
    char text[64];
    if (width == 4) {
        const float narrowed = static_cast<float>(value);
        if (!std::isfinite(narrowed))
            return false;
        for (int p = 1; p <= 9; p++) {
            std::snprintf(text, sizeof text, "%.*g", p,
                          static_cast<double>(narrowed));
            if (std::strtof(text, nullptr) == narrowed)
                break;
        }
    } else if (width == 8) {
        if (!std::isfinite(value))
            return false;
        for (int p = 1; p <= 17; p++) {
            std::snprintf(text, sizeof text, "%.*g", p, value);
            if (std::strtod(text, nullptr) == value)
                break;
        }
    } else {
        return false;
    }
    *out = text;
    if (out->find_first_of(".eE") == std::string::npos)
        out->append(".0");
    return true;
}

bool format_member_key_slot(void* h, const std::string& slot_key,
                            std::string* out) {
    const std::uint8_t* vd = nullptr;
    std::uint32_t vn = 0;
    if (!read_wire(h, slot_key, &vd, &vn) || !vd || !vn)
        return false;
    std::string lt;
    const std::uint8_t* ltp = nullptr;
    const std::uint16_t ltl = wire_langtype_view(vd, vn, &ltp);
    if (ltp && ltl > 0)
        lt.assign(reinterpret_cast<const char*>(ltp), ltl);
    else
        read_langtype(h, slot_key, &lt);
    if (!lt.empty() && lt.find("char/") != std::string::npos)
        return wire_text(vd, vn, lt, out);
    if (!lt.empty() && lt[0] == '[')
        return false;
    Scalar sc{};
    if (v_scalar(Wire{vd, vn}, lt, &sc)) {
        switch (sc.k) {
        case Scalar::K::Int:
            *out = std::to_string(sc.i);
            return true;
        case Scalar::K::Uint:
            *out = std::to_string(sc.u);
            return true;
        case Scalar::K::Bool:
            *out = sc.u ? "true" : "false";
            return true;
        case Scalar::K::Float:
            return format_member_key_float_ext(sc.f, sc.width, out);
        default:
            break;
        }
    }
    return wire_text(vd, vn, lt, out);
}

int ext_param_slot_by_name(void* h, const Pc& p, std::string_view name) {
    std::string dir;
    if (!frame_func_dir(h, p.vid, p.depth, &dir))
        return 0;
    while (!dir.empty() && dir.back() == '/')
        dir.pop_back();
    for (int sign = 1; sign >= -1; sign -= 2) {
        for (int k = 1; k <= 16; k++) {
            const int slot = sign * k;
            const std::uint8_t* d = nullptr;
            std::uint32_t n = 0;
            std::string txt;
            if (!read_wire(h, dir + ".[0," + std::to_string(slot) + "]", &d, &n))
                break;
            if (wire_text(d, n, "def langtype", &txt) &&
                std::string_view(txt).substr(0, txt.find('\0')) == name)
                return slot;
        }
    }
    return 0;
}

std::string ext_param_value_key(void* h, const std::string& frame, int slot) {
    std::string key = frame + "/\xE2\x80\xA5" "arg[0," + std::to_string(slot) + "]";
    const std::uint8_t* d = nullptr;
    std::uint32_t n = 0;
    if (!read_wire_raw(h, key, &d, &n) || !d || !n)
        return {};
    std::string t;
    if (v_ptr_target(Wire{d, n}, &t) && !t.empty())
        return t;
    return key;
}

bool ext_frame_local_exists(void* h, const std::string& frame,
                            std::string_view name) {
    const std::string key = frame + "/" + std::string(name);
    const std::uint8_t* d = nullptr;
    std::uint32_t n = 0;
    return read_wire_raw(h, key, &d, &n) && d && n && !v_is_none(Wire{d, n});
}

std::string resolve_slot_ext(void* h, const Pc& p, const std::string& frame,
                             std::string_view raw_var, bool allow_param_name);

std::string resolve_dyn_or_static_member_ext(void* h, const Pc& p,
                                             const std::string& frame,
                                             std::string_view desc,
                                             std::size_t mid) {
    const std::string_view base_part = desc.substr(0, mid);
    const std::size_t after_dot = mid + 2;
    const bool dyn = after_dot < desc.size() && desc[after_dot] == '*';
    const std::string_view rest = desc.substr(after_dot + (dyn ? 1 : 0));

    std::string base_key = resolve_slot_ext(h, p, frame, base_part, true);
    if (base_key.empty())
        return {};
    // Dereference `base` if it holds a Ptr or `/`-prefixed path string.
    {
        const std::uint8_t* bd = nullptr;
        std::uint32_t bn = 0;
        if (read_wire_raw(h, base_key, &bd, &bn) && bd && bn) {
            std::string bt;
            if (v_ptr_target(Wire{bd, bn}, &bt) && !bt.empty()) {
                base_key = std::move(bt);
            } else {
                std::string blt;
                if (read_langtype(h, base_key, &blt) &&
                    blt.find("char/") != std::string::npos &&
                    wire_text(bd, bn, blt, &bt) && !bt.empty() && bt[0] == '/') {
                    base_key = std::move(bt);
                }
            }
        }
    }

    std::string kstr;
    if (dyn) {
        if (rest.size() >= 2 && rest.front() == '"' && rest.back() == '"') {
            kstr.assign(rest.substr(1, rest.size() - 2));
        } else if (!rest.empty()) {
            const std::string kslot = resolve_slot_ext(h, p, frame, rest, true);
            if (kslot.empty() || !format_member_key_slot(h, kslot, &kstr))
                return {};
        }
    } else {
        kstr.assign(rest);
    }
    if (kstr.empty())
        return {};
    return base_key + "\xC2\xB7" + kstr;
}

std::string resolve_slot_ext(void* h, const Pc& p, const std::string& frame,
                             std::string_view raw_var, bool allow_param_name) {
    const auto sp = raw_var.find(' ');
    const std::string_view var = raw_var.substr(0, sp);
    if (var.empty())
        return {};
    const auto mid = var.find("\xC2\xB7");
    if (mid != std::string_view::npos && mid > 0) {
        if (var[0] == '/') {
            if (mid + 2 < var.size() && var[mid + 2] == '*')
                return resolve_dyn_or_static_member_ext(h, p, frame, var, mid);
            return std::string(var);
        }
        return resolve_dyn_or_static_member_ext(h, p, frame, var, mid);
    }
    if (var[0] == '/')
        return std::string(var);
    if (var[0] == '*') {
        const std::string mk = frame + "/" + std::string(var.substr(1));
        const std::uint8_t* d = nullptr;
        std::uint32_t n = 0;
        std::string target;
        if (read_wire_raw(h, mk, &d, &n)) {
            std::string t;
            if (v_ptr_target(Wire{d, n}, &t))
                target = std::move(t);
        }
        if (!target.empty() && var.substr(0, 4) == "*[0," && var.size() > 4 &&
            var[4] != '-') {
            const std::uint8_t* d2 = nullptr;
            std::uint32_t n2 = 0;
            std::string t2;
            if (read_wire_raw(h, target, &d2, &n2) &&
                v_ptr_target(Wire{d2, n2}, &t2) && !t2.empty())
                target = std::move(t2);
        }
        return target.empty() ? mk : target;
    }
    if (allow_param_name && var[0] != '[' &&
        !ext_frame_local_exists(h, frame, var)) {
        const int slot = ext_param_slot_by_name(h, p, var);
        if (slot != 0) {
            std::string pk = ext_param_value_key(h, frame, slot);
            if (!pk.empty())
                return pk;
        }
    }
    return frame + "/" + std::string(var);
}

}  // namespace

char* resolve_write_impl(void* h, const char* pc, int idx) {
    Pc p;
    if (!h || !pc || !parse_pc(pc, &p) || idx < 0)
        return dup("");
    // 写槽 [s0,+k] 的描述符 name（对齐 C kvlangRwirextResolveWrite：
    // 解码 rwir → writes[idx].name → ResolveWriteSlot 解析为帧槽路径）。
    const std::string key = func_key(h, p, idx + 1);
    std::string var;
    int nr = 0, nw = 0;
    if (!read_rwir(h, key, &var, &nr, &nw) || var.empty())
        return dup("");
    const std::string frame = g_vmkv.frame_root(p.vid, p.depth);
    return dup(resolve_slot_ext(h, p, frame, var, false));
}

char* next_pc_impl(const char* pc) {
    Pc p;
    if (!pc || !parse_pc(pc, &p))
        return nullptr;
    return dup(g_vmkv.next_pc(p));
}

}  // namespace kvlang::runtimec2
