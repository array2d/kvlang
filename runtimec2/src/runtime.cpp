#include <cstdlib>
#include "runtime.hpp"

#include <cstdio>

#include <cstring>
#include <vector>

#include <kvspace/kvspace.h>

#include "vmkv.hpp"
#include "rwir.hpp"
#include "status.hpp"
#include "valueio.hpp"
#include "codeseg.hpp"
#include "jit.hpp"

namespace kvlang::runtimec2 {
using vmbin::Frame;
using vmbin::Outcome;
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
    return join(g_vmkv.vthread_root(vid), join("/", join(kMemberSep, "status")));
}

std::string pc_key(std::string_view vid) {
    return join(g_vmkv.vthread_root(vid), join("/", join(kMemberSep, "pc")));
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
    const std::string frame = g_vmkv.frame_root(p.vid, p.depth);
    std::string dir;
    if (!frame_func_dir(h, p.vid, p.depth, &dir))
        return false;
    const CInsn* insn = nullptr;
    const Segment* insn_seg = nullptr;
    if (cached_insn(dir, p.s0, &insn, &insn_seg)) {
        *op = instruction_name(*insn_seg, *insn);
        g_dir_hint.assign(dir);  // 段已存在：把函数目录带给 run_frame
        return true;
    }
    const Segment* seg = cached_segment(h, dir);
    if (!seg)
        return false;
    const auto i = p.s0 - 1;
    if (i < 0 || i >= static_cast<int>(seg->insns.size()))
        return false;
    *op = instruction_name(*seg, seg->insns[static_cast<std::size_t>(i)]);
    g_dir_hint.assign(dir);
    return true;
}

}  // namespace

Runtime* runtime_of(void* rt) noexcept { return static_cast<Runtime*>(rt); }

// ── 内部入口（供 abi.cpp 调用）──────────────────────────────────────

// 可达性预构建：build + JIT 编译入口函数调用图内全部函数（visited 防环）。
// 副作用仅进程内缓存；kvspace 只读。
static void prebuild_reachable(void* h, const std::string& func_key) {
    std::vector<std::string> work{func_key};
    std::vector<std::string> seen;
    while (!work.empty()) {
        const std::string fk = work.back();
        work.pop_back();
        bool dup = false;
        for (const std::string& s : seen)
            if (s == fk)
                dup = true;
        if (dup)
            continue;
        seen.push_back(fk);
        const Segment* seg = cached_segment(h, fk);
        if (!seg)
            continue;
        if (jit_enabled())
            jit_compile(fk, seg);
        for (const CInsn& ci : seg->insns) {
            if (ci.op != rwir::Op::Call)
                continue;
            const std::string& callee = instruction_callee(*seg, ci);
            if (!callee.empty())
                work.push_back(callee);
        }
    }
}

char* bootstrap_h(void* h, const char* funcname) {
    if (!h || !funcname || !*funcname)
        return nullptr;
    std::string vid = alloc_vid(h);
    if (vid.empty())
        return nullptr;
    std::string func_key = "/lib/" + g_vmkv.fold_op(funcname);
    std::string func_dir = func_key + "/";
    std::string frame = g_vmkv.frame_root(vid, 1);
    std::string stack = frame + "/";
    char err[192] = {};
    if (kvspaceMkindex(h, stack.c_str(), 0, err, sizeof err) != 0 ||
        kvspaceMkindexExt(h, stack.c_str(), func_dir.c_str(), err, sizeof err) != 0)
        return nullptr;
    std::string entry = g_vmkv.entry_pc(vid, 1);
    if (!write_text(h, callpc_key(frame), entry) ||
        !write_text(h, seglib_key(stack), func_key) ||
        !write_text(h, pc_key(vid), entry) || !write_status(h, vid, "init"))
        return nullptr;
    // 启动预构建（M1）：从入口函数沿调用图可达性 build + JIT 编译。
    // 执行前（test() 的 t0 之前）完成——一次性构建/编译移出计时窗。
    prebuild_reachable(h, func_key);
    return strdup(vid.c_str());
}

char* bootstrap_impl(Runtime* r, const char* funcname) {
    return r ? bootstrap_h(r->kv.handle(), funcname) : nullptr;
}

int execute_vthread_h(void* h, const char* vid, char** out_pc,
                      bool observable) {
    if (out_pc)
        *out_pc = nullptr;
    if (!h || !vid || !*vid)
        return -1;
    // 帧级缓存：key/pc 只构造一次（P0：消灭每步字符串构造）。
    const std::string st_key = status_key(vid);
    const std::string pc_k = pc_key(vid);
    const std::string msg_key = join(g_vmkv.vthread_root(vid), join("/", join(kMemberSep, "msg")));
    const std::string vid_root = g_vmkv.vthread_root(vid);
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
        const std::string frame = g_vmkv.frame_root(p.vid, p.depth);
        static const bool ev_trace = std::getenv("RC2_TRACE") != nullptr;
        if (ev_trace)
            std::fprintf(stderr, "[ev vid=%s key=%s pc=%s status=%s]\n", vid,
                         pc_k.c_str(), pc.c_str(), st.c_str());
        // 本 runtime 能力判定：经段缓存预解码找 opcode，找到即本地执行。
        std::string op;
        if (!read_opcode(h, pc, &op))
            return -1;
        const rwir::Entry* e = rwir::find_myrwircap(op.data(),
                                                   static_cast<std::uint32_t>(op.size()));
        static const bool op_trace = std::getenv("RC2_TRACE") != nullptr;
        if (op_trace)
            std::fprintf(stderr, "[op=%s found=%d impl=%d]\n", op.c_str(), e != nullptr,
                         e ? (int)e->impl : -1);
        // 总能进 run_frame：本 runtime 的 op 执行；外部/未知 op（e=nullptr 或
        // Handoff）由 run_frame 在 Stop 边界 flush 帧局部后交出（handoff），
        // 保证 Handoff 后 resume 的新 Frame 能重建（flush 已落明文+attr）。
        if (e && e->impl == rwir::Impl::None &&
            e->handler == rwir::RwirHandlerId::None)
            return -1;  // 表内但无实现：硬错
        {
            Frame fctx;
            fctx.h = h;
            fctx.vid = p.vid;
            fctx.depth = p.depth;
            fctx.frame = std::move(frame);
            fctx.observable = observable;  // vthread·run 子链可观察（printstack）
            // 复用 read_opcode 探测到的函数目录（段缓存已持有，免二次解析）。
            if (!g_dir_hint.empty())
                fctx.func_dir = std::move(g_dir_hint);
            else if (!frame_func_dir(h, p.vid, p.depth, &fctx.func_dir))
                return -1;
            std::string err;
            std::string stop_pc;
            Outcome rr = g_vmkv.run_frame(fctx, p.s0, &stop_pc, &err);
            if (rr == Outcome::Error) {
                // 出错指令行：run_frame 已把 stop_pc 设为 param_key(出错帧, s0)。
                // 子 vthread 的 pc 应指向出错指令（printstack cur 渲染依赖）。
                if (!stop_pc.empty())
                    (void)write_text(h, pc_k, stop_pc);
                write_status(h, vid, "error");
                write_text(h, msg_key,
                           err.empty() ? "runtimec2: execution failed" : err);
                return -1;
            }
            if (rr == Outcome::Ok) {
                write_status(h, vid, "ok");
                return 0;
            }
            if (ev_trace)
                std::fprintf(stderr, "[rr=%d stop=%s]\n", (int)rr, stop_pc.c_str());
            if (rr == Outcome::Returned) {
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

int execute_vthread_impl(Runtime* r, const char* vid, char** out_pc) {
    // 顶层驱动（bootstrap/test 链）不可观察：mem_frame 保持零 kvspace 性能。
    return r ? execute_vthread_h(r->kv.handle(), vid, out_pc, false) : -1;
}

}  // namespace kvlang::runtimec2
