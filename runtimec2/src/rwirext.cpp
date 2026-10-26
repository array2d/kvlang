#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <kvspace/kvspace.h>

#include "model.hpp"
#include "valueio.hpp"

namespace kv::rt {
namespace {

char* dup(const std::string& s) {
    char* p = static_cast<char*>(std::malloc(s.size() + 1));
    if (p)
        std::memcpy(p, s.c_str(), s.size() + 1);
    return p;
}

std::string func_key(void* h, const Pc& p, int s1) {
    std::string dir;
    if (frame_func_dir(h, p.vid, p.depth, &dir))
        return param_key(dir, p.s0, s1);
    return param_key(frame_root(p.vid, p.depth), p.s0, s1);
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
    if (!read_rwir(h, param_key(dir, p.s0, 0), &op, &nr, &nw))
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
    std::string v;
    if (!read_text(h, func_key(h, p, -(idx + 1)), &v))
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
    (void)h;
    (void)pc;
    (void)idx;
    return dup("");
}

char* next_pc_impl(const char* pc) {
    Pc p;
    if (!pc || !parse_pc(pc, &p))
        return nullptr;
    return dup(next_pc(p));
}

}  // namespace kv::rt
