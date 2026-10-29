#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <kvspace/kvspace.h>

#include "vmkv.hpp"
#include <cinttypes>

#include "value.hpp"
#include "valueio.hpp"

namespace kv::rt {
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

bool param_exists(void* h, const Pc& p, int s1) {
    const std::uint8_t* d = nullptr;
    std::uint32_t n = 0;
    return read_wire(h, func_key(h, p, s1), &d, &n);
}

}  // namespace

// opcode + 每个读参一行（字面量参名为空行）。
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
    std::string out = op;
    for (int i = 0; i < nr; i++)
        out += "\n";
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
        if (read_wire(h, target, &vd, &vn)) {
            Wire w{vd, vn};
            Scalar sc{};
            if (v_scalar(w, &sc)) {
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
        if (read_wire(h, target, &vd, &vn)) {
            Wire w{vd, vn};
            Scalar sc{};
            if (v_scalar(w, &sc)) {
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
        if (!read_text(h, target, &v))
            return dup("");
        return dup(v);
    }
    std::string v;
    const std::uint8_t* kd = nullptr;
    std::uint32_t kn = 0;
    if (read_wire(h, key, &kd, &kn)) {
        Wire w{kd, kn};
        Scalar sc{};
        if (v_scalar(w, &sc)) {
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

// 读参 KV 路径：M2 接 operand 解码后实现；M1 一律空（走 ResolveRead 取值）。
char* resolve_read_path_impl(void* h, const char* pc, int idx) {
    (void)h;
    (void)pc;
    (void)idx;
    return dup("");
}

char* resolve_write_impl(void* h, const char* pc, int idx) {
    Pc p;
    if (!h || !pc || !parse_pc(pc, &p) || idx < 0)
        return dup("");
    // 写槽 [s0,+k] 的描述符 name（对齐 C kvlangRwirextResolveWrite：
    // 解码 rwir → writes[idx].name → ResolveWriteSlot 解析为帧槽路径）。
    const std::string key = func_key(h, p, idx + 1);
    std::string var;
    int nr = 0, nw = 0;
    if (!read_rwir(h, key, &var, &nr, &nw))
        return dup("");
    const std::string frame = g_vmkv.frame_root(p.vid, p.depth);
    std::string target;
    if (var.empty())
        return dup("");
    if (var[0] == '/') {
        // 绝对路径（可能带类型后缀）：剥空格后段。
        const auto sp = var.find(' ');
        target = var.substr(0, sp);
    } else if (var[0] == '*') {
        // Ptr 写槽：帧成员 `*name` → Ptr target；参数 `*[0,k]` → arg 槽链。
        const std::string mk = frame + "/" + var.substr(1);
        const std::uint8_t* d = nullptr;
        std::uint32_t n = 0;
        if (read_wire_raw(h, mk, &d, &n)) {
            std::string t;
            if (v_ptr_target(Wire{d, n}, &t))
                target = t;
        }
        if (target.empty())
            target = mk;
    } else {
        target = frame + "/" + var;
    }
    return dup(target);
}

char* next_pc_impl(const char* pc) {
    Pc p;
    if (!pc || !parse_pc(pc, &p))
        return nullptr;
    return dup(g_vmkv.next_pc(p));
}

}  // namespace kv::rt
