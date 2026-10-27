#include <cstdlib>
#include "runtime.hpp"

#include <cstdio>

#include <cstring>

#include <kvspace/kvspace.h>

#include "model.hpp"
#include "ops.hpp"
#include "vmkv.hpp"
#include "status.hpp"
#include "valueio.hpp"

namespace kv::rt {
using bin::FrameCtx;
using bin::RunResult;
namespace {

constexpr std::string_view kMemberSep = "\xE2\x80\xA5";  // ‥
constexpr std::string_view kVthreadRoot = "/vthread";

// read_opcode 探测到的函数目录缓存（进程级，单线程：能力判定与执行同线程）。
static std::string g_dir_hint;

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

// 从 pc 取当前指令 opcode：经段缓存直取指令（预解码，不再每次解析 rwir body）。
bool read_opcode(void* h, const std::string& pc, std::string* op) {
    Pc p;
    if (!parse_pc(pc, &p))
        return false;
    const std::string frame = frame_root(p.vid, p.depth);
    std::string dir;
    if (!frame_func_dir(h, p.vid, p.depth, &dir))
        return false;
    const CInsn* insn = nullptr;
    if (cached_insn(dir, p.s0, &insn, nullptr)) {
        *op = insn->name;
        g_dir_hint.assign(dir);  // 段已存在：把函数目录带给 run_frame
        return true;
    }
    const Segment* seg = cached_segment(h, dir);
    if (!seg)
        return false;
    const auto i = p.s0 - 1;
    if (i < 0 || i >= static_cast<int>(seg->insns.size()))
        return false;
    *op = seg->insns[static_cast<std::size_t>(i)].name;
    g_dir_hint.assign(dir);
    return true;
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
    // 帧级缓存：key/pc 只构造一次（P0：消灭每步字符串构造）。
    const std::string st_key = status_key(vid);
    const std::string pc_k = pc_key(vid);
    const std::string msg_key = join(vthread_root(vid), join("/", join(kMemberSep, "msg")));
    const std::string vid_root = vthread_root(vid);
    for (;;) {
        std::string st;
        if (!read_text(h, st_key, &st) || st.empty())
            return -1;
        if (st != "init" && st != "running" && st != "wait")
            return 0;  // done / error — 状态由写入方决定
        std::string pc;
        if (!read_text(h, pc_k, &pc) || pc.empty())
            return -1;
        Pc p;
        if (!parse_pc(pc, &p))
            return -1;
        const std::string frame = frame_root(p.vid, p.depth);
        static const bool ev_trace = std::getenv("RC2_TRACE") != nullptr;
        if (ev_trace)
            std::fprintf(stderr, "[ev vid=%s key=%s pc=%s status=%s]\n", vid,
                         pc_k.c_str(), pc.c_str(), st.c_str());
        // 本 runtime 能力判定：经段缓存预解码找 opcode，找到即本地执行。
        std::string op;
        if (!read_opcode(h, pc, &op))
            return -1;
        const ops::Entry* e = ops::find_myrwircap(op.data(),
                                                   static_cast<std::uint32_t>(op.size()));
        static const bool op_trace = std::getenv("RC2_TRACE") != nullptr;
        if (op_trace)
            std::fprintf(stderr, "[op=%s found=%d impl=%d]\n", op.c_str(), e != nullptr,
                         e ? (int)e->impl : -1);
        if (e && e->impl != ops::Impl::Handoff) {
            FrameCtx fctx;
            fctx.h = h;
            fctx.vid = p.vid;
            fctx.depth = p.depth;
            fctx.frame = std::move(frame);
            // 复用 read_opcode 探测到的函数目录（段缓存已持有，免二次解析）。
            if (!g_dir_hint.empty())
                fctx.func_dir = std::move(g_dir_hint);
            else if (!frame_func_dir(h, p.vid, p.depth, &fctx.func_dir))
                return -1;
            std::string err;
            std::string stop_pc;
            RunResult rr = run_frame(fctx, p.s0, &stop_pc, &err);
            if (rr == RunResult::Error) {
                write_status(h, vid, "error");
                write_text(h, msg_key,
                           err.empty() ? "runtimec2: execution failed" : err);
                return -1;
            }
            if (rr == RunResult::Ok) {
                write_status(h, vid, "ok");
                return 0;
            }
            if (ev_trace)
                std::fprintf(stderr, "[rr=%d stop=%s]\n", (int)rr, stop_pc.c_str());
            if (rr == RunResult::Returned) {
                // 子帧返回 → 续跑调用方返回地址（帧已由 run_frame 删除）。
                if (stop_pc.empty() || !write_text(h, pc_k, stop_pc))
                    return -1;
                continue;
            }
            if (out_pc)
                *out_pc = strdup(stop_pc.c_str());
            return 1;
        }
        if (out_pc)
            *out_pc = strdup(pc.c_str());
        return 1;  // 非本 runtime 的 rwir：交驱动
    }
}

}  // namespace kv::rt
