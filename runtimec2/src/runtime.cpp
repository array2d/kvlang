#include "runtime.hpp"

#include <cstdio>

#include <cstring>

#include <kvspace/kvspace.h>

#include "model.hpp"
#include "status.hpp"
#include "valueio.hpp"

namespace kv::rt {
namespace {

constexpr std::string_view kMemberSep = "\xE2\x80\xA5";  // ‥
constexpr std::string_view kVthreadRoot = "/vthread";

std::string join(std::string_view a, std::string_view b) {
    std::string s(a);
    s.append(b);
    return s;
}

std::string status_key(std::string_view vid) {
    return join(vthread_root(vid), join("/", join(kMemberSep, "status")));
}

std::string pc_key(std::string_view vid) {
    return join(vthread_root(vid), join("/", join(kMemberSep, "pc")));
}

std::string callpc_key(std::string_view frame) {
    return join(frame, join(kMemberSep, "callpc"));
}

std::string seglib_key(std::string_view stack) {
    return join(stack, join(kMemberSep, "lib"));
}

std::string alloc_vid(void* h) {
    const std::string seq_key = join(kVthreadRoot, join("/", join(kMemberSep, "seq")));
    std::int64_t seq = 0;
    if (!read_int(h, seq_key, &seq))
        seq = 0;
    seq++;
    if (!write_int(h, seq_key, seq))
        return {};
    return std::to_string(seq);
}

bool write_status(void* h, std::string_view vid, std::string_view st) {
    return write_text(h, status_key(vid), st);
}

// 从 pc 取当前指令的 opcode；经 extindex 解析到函数目录。
std::string instr_func_key(void* h, const Pc& p) {
    std::string dir;
    if (frame_func_dir(h, p.vid, p.depth, &dir))
        return param_key(dir, p.s0, p.s1);
    return instr_key(p);
}

bool read_opcode(void* h, const std::string& pc, std::string* op) {
    Pc p;
    if (!parse_pc(pc, &p))
        return false;
    int nr = 0, nw = 0;
    return read_rwir(h, instr_func_key(h, p), op, &nr, &nw);
}

}  // namespace

Runtime* runtime_of(void* rt) noexcept { return static_cast<Runtime*>(rt); }

// ── 内部入口（供 abi.cpp 调用）──────────────────────────────────────

char* bootstrap_impl(Runtime* r, const char* funcname) {
    if (!r || !funcname || !*funcname)
        return nullptr;
    void* h = r->kv.handle();
    if (!h)
        return nullptr;
    std::string vid = alloc_vid(h);
    if (vid.empty())
        return nullptr;
    std::string func_key = "/lib/" + fold_op(funcname);
    std::string func_dir = func_key + "/";
    std::string frame = frame_root(vid, 1);
    std::string stack = frame + "/";
    char err[192] = {};
    if (kvspaceMkindex(h, stack.c_str(), 0, err, sizeof err) != 0 ||
        kvspaceMkindexExt(h, stack.c_str(), func_dir.c_str(), err, sizeof err) != 0)
        return nullptr;
    std::string entry = entry_pc(vid, 1);
    if (!write_text(h, callpc_key(frame), entry) ||
        !write_text(h, seglib_key(stack), func_key) ||
        !write_text(h, pc_key(vid), entry) || !write_status(h, vid, "init"))
        return nullptr;
    return strdup(vid.c_str());
}

int execute_vthread_impl(Runtime* r, const char* vid, char** out_pc) {
    if (out_pc)
        *out_pc = nullptr;
    if (!r || !vid || !*vid)
        return -1;
    void* h = r->kv.handle();
    for (;;) {
        std::string st;
        if (!read_text(h, status_key(vid), &st) || st.empty())
            return -1;
        if (st != "init" && st != "running" && st != "wait")
            return 0;  // done / error — 状态由写入方决定
        std::string pc;
        if (!read_text(h, pc_key(vid), &pc) || pc.empty())
            return -1;
        std::string op;
        if (!read_opcode(h, pc, &op))
            return -1;
        if (op == "return") {
            write_status(h, vid, "ok");
            return 0;
        }
        if (op.empty()) {
            write_status(h, vid, "error");
            return -1;
        }
        if (out_pc)
            *out_pc = strdup(pc.c_str());
        return 1;  // 非本 runtime 的 rwir：交驱动
    }
}

}  // namespace kv::rt
