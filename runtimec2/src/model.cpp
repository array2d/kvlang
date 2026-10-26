#include "model.hpp"

#include <charconv>

namespace kv::rt {
namespace {

constexpr std::string_view kMemberSep = "\xE2\x80\xA5";  // ‥

}  // namespace

bool parse_pc(std::string_view pc, Pc* out) noexcept {
    // /vthread/<vid>/[d]/[s0,s1]
    constexpr std::string_view pfx = "/vthread/";
    if (pc.size() < pfx.size() || pc.substr(0, pfx.size()) != pfx)
        return false;
    std::string_view rest = pc.substr(pfx.size());
    auto slash = rest.find('/');
    if (slash == std::string_view::npos || slash == 0)
        return false;
    out->vid.assign(rest.substr(0, slash));
    rest = rest.substr(slash + 1);
    auto frame_end = rest.find(']');
    if (rest.size() < 3 || rest[0] != '[' || frame_end == std::string_view::npos)
        return false;
    if (rest.substr(1, frame_end - 1).find(',') != std::string_view::npos)
        return false;
    int depth = 0;
    auto fr = std::from_chars(rest.data() + 1, rest.data() + frame_end, depth);
    if (fr.ec != std::errc() || depth < 1)
        return false;
    rest = rest.substr(frame_end + 1);
    if (rest.size() < 3 || rest[0] != '/' || rest[1] != '[')
        return false;
    rest = rest.substr(2);
    auto close = rest.find(']');
    if (close == std::string_view::npos)
        return false;
    auto comma = rest.substr(0, close).find(',');
    if (comma == std::string_view::npos)
        return false;
    int s0 = 0, s1 = 0;
    auto a = std::from_chars(rest.data(), rest.data() + comma, s0);
    auto b = std::from_chars(rest.data() + comma + 1, rest.data() + close, s1);
    if (a.ec != std::errc() || b.ec != std::errc())
        return false;
    out->depth = depth;
    out->s0 = s0;
    out->s1 = s1;
    return true;
}

std::string vthread_root(std::string_view vid) {
    std::string s = "/vthread/";
    s.append(vid);
    return s;
}

std::string frame_root(std::string_view vid, int depth) {
    std::string s = vthread_root(vid);
    s += "/[";
    s += std::to_string(depth);
    s += "]";
    return s;
}

std::string entry_pc(std::string_view vid, int depth) {
    std::string s = frame_root(vid, depth);
    s += "/[1,0]";
    return s;
}

std::string param_key(std::string_view fr, int s0, int s1) {
    std::string s(fr);
    s += "/[";
    s += std::to_string(s0);
    s += ",";
    s += std::to_string(s1);
    s += "]";
    return s;
}

std::string instr_key(const Pc& pc) {
    return param_key(frame_root(pc.vid, pc.depth), pc.s0, pc.s1);
}

std::string next_pc(const Pc& pc) {
    return param_key(frame_root(pc.vid, pc.depth), pc.s0 + 1, 0);
}

std::string fold_op(std::string_view op) {
    std::string s;
    s.reserve(op.size());
    // `·` 是 UTF-8 E2 80 A5；与 `/` 一起折叠成 `/`。
    for (size_t i = 0; i < op.size();) {
        if (i + 3 <= op.size() && static_cast<unsigned char>(op[i]) == 0xE2 &&
            static_cast<unsigned char>(op[i + 1]) == 0x80 &&
            static_cast<unsigned char>(op[i + 2]) == 0xA5) {
            s += '/';
            i += 3;
        } else if (op[i] == '/') {
            s += '/';
            ++i;
        } else {
            s += op[i++];
        }
    }
    return s;
}

}  // namespace kv::rt
