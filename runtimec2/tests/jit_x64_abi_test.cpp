// x86_64 发射器 ABI/编码单测：直接 emit_arch + 执行机器码。
// 编译：本文件 + src/jit_x64.cpp（不链接 jit.cpp / vmkv）。
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <sys/mman.h>

#include "codeseg.hpp"
#include "jit.hpp"

namespace {

using namespace kvlang::runtimec2;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        g_failures++;
    }
}

int push_lit(Segment* seg, std::int64_t v) {
    seg->regs.push_back(RegDesc{});
    seg->regs.back().kind = Operand::Kind::Literal;
    seg->lit.push_back(LitFast{true, v});
    return static_cast<int>(seg->regs.size()) - 1;
}

int push_var(Segment* seg, const char* name, int param_k) {
    seg->regs.push_back(RegDesc{});
    seg->regs.back().kind = Operand::Kind::Var;
    seg->regs.back().name = name;
    seg->regs.back().param_k = param_k;
    seg->lit.push_back(LitFast{});
    return static_cast<int>(seg->regs.size()) - 1;
}

void push_insn(Segment* seg, rwir::Op op, int s0,
               std::initializer_list<int> rd, std::initializer_list<int> wr,
               bool fast_i64 = false, int opfn = 0) {
    CInsn ci;
    ci.op = op;
    ci.impl = (op == rwir::Op::Goto || op == rwir::Op::Br) ? rwir::Impl::Control
                                                           : rwir::Impl::Typed;
    (void)s0;  // pc = 下标 + 1（s0 已删除）
    ci.fast_i64 = fast_i64;
    ci.opfn = static_cast<std::uint8_t>(opfn);
    ci.nrd = static_cast<std::uint8_t>(rd.size());
    ci.nwr = static_cast<std::uint8_t>(wr.size());
    int k = 0;
    for (int x : rd) ci.rd[k++] = static_cast<std::uint8_t>(x);
    k = 0;
    for (int x : wr) ci.wr[k++] = static_cast<std::uint8_t>(x);
    seg->insns.push_back(ci);
}

struct Executable {
    void* mem = nullptr;
    std::size_t len = 0;
    ~Executable() {
        if (mem)
            munmap(mem, len);
    }
};

bool make_executable(const std::vector<std::uint8_t>& bytes, Executable* out) {
    if (bytes.empty())
        return false;
    void* mem = mmap(nullptr, bytes.size(), PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED)
        return false;
    std::memcpy(mem, bytes.data(), bytes.size());
    if (mprotect(mem, bytes.size(), PROT_READ | PROT_EXEC) != 0) {
        munmap(mem, bytes.size());
        return false;
    }
    out->mem = mem;
    out->len = bytes.size();
    return true;
}

std::int64_t run(const Executable& exe, std::int64_t arg, std::int64_t* err) {
    auto fn = reinterpret_cast<JitFn>(exe.mem);
    return fn(arg, err);
}

bool contains_push_saved(const std::vector<std::uint8_t>& b) {
    for (std::size_t i = 0; i + 1 < b.size(); i++)
        if (b[i] == 0x41 && b[i + 1] >= 0x54 && b[i + 1] <= 0x57)
            return true;
    return false;
}

// 7 个局部变量：相当于 R4 复现器，必须触发 R12-R14 的按需保存。
Segment seven() {
    Segment s;
    const int src = push_var(&s, "*[0,-1]", -1);
    const int dst = push_var(&s, "*[0,1]", 1);
    const int l0 = push_lit(&s, 0);
    const int l1 = push_lit(&s, 1);
    push_insn(&s, rwir::Op::Add, 1, {src, l1}, {dst}, true, 1);
    int locs[7];
    int s0 = 2;
    for (int k = 0; k < 7; k++) {
        locs[k] = push_var(&s, "loc", 0);
        const int lit = push_lit(&s, k + 2);
        push_insn(&s, rwir::Op::Add, s0++, {src, lit}, {locs[k]}, true, 1);
    }
    for (int k = 0; k < 7; k++)
        push_insn(&s, rwir::Op::Add, s0++, {dst, locs[k]}, {dst}, true, 1);
    push_insn(&s, rwir::Op::Return, s0, {}, {});
    (void)l0;
    return s;
}

// 2 个局部变量：不应产生 push/pop。
Segment small() {
    Segment s;
    const int src = push_var(&s, "*[0,-1]", -1);
    const int dst = push_var(&s, "*[0,1]", 1);
    const int l1 = push_lit(&s, 1);
    const int acc = push_var(&s, "acc", 0);
    push_insn(&s, rwir::Op::Add, 1, {src, l1}, {dst}, true, 1);
    push_insn(&s, rwir::Op::Add, 2, {dst, l1}, {acc}, true, 1);
    push_insn(&s, rwir::Op::Add, 3, {dst, acc}, {dst}, true, 1);
    push_insn(&s, rwir::Op::Return, 4, {}, {});
    return s;
}

// 比较写回同一局部：结果必须是严格 0/1（setcc 高位清零）。
Segment bool_shape() {
    Segment s;
    const int src = push_var(&s, "*[0,-1]", -1);
    const int dst = push_var(&s, "*[0,1]", 1);
    const int big = push_lit(&s, 1000000);
    const int small = push_lit(&s, 5);
    push_insn(&s, rwir::Op::Add, 1, {src, big}, {dst}, true, 1);
    push_insn(&s, rwir::Op::Le, 2, {src, small}, {dst}, true, 16);
    push_insn(&s, rwir::Op::Return, 3, {}, {});
    return s;
}

// 比较 + 后向 br：禁止融合，否则 guard 的 sub 会覆盖比较 flags。
Segment backward_loop() {
    Segment s;
    const int dst = push_var(&s, "*[0,1]", 1);
    const int src = push_var(&s, "*[0,-1]", -1);
    const int i = push_var(&s, "i", 0);
    const int cond = push_var(&s, "cond", 0);
    const int l0 = push_lit(&s, 0);
    const int l1 = push_lit(&s, 1);
    const int t_loop = push_lit(&s, 3);
    const int t_exit = push_lit(&s, 6);
    push_insn(&s, rwir::Op::Copy, 1, {l0}, {dst});
    push_insn(&s, rwir::Op::Copy, 2, {l0}, {i});
    push_insn(&s, rwir::Op::Add, 3, {i, l1}, {i}, true, 1);
    push_insn(&s, rwir::Op::Le, 4, {i, src}, {cond}, true, 16);
    push_insn(&s, rwir::Op::Br, 5, {cond, t_loop, t_exit}, {});
    push_insn(&s, rwir::Op::Add, 6, {dst, i}, {dst}, true, 1);
    push_insn(&s, rwir::Op::Return, 7, {}, {});
    return s;
}

// 自环 goto：必须扣 guard（结构断言，避免回归时挂死 CI）。
Segment self_loop() {
    Segment s;
    const int t = push_lit(&s, 1);
    push_insn(&s, rwir::Op::Goto, 1, {t}, {});
    return s;
}

}  // namespace

int main() {
    // 1) 按需保存：7 局部有 push，2 局部没有
    {
        Segment s = seven();
        JitShape sh{0, 1};
        Executable exe;
        check(make_executable(emit_arch(&s, sh), &exe), "seven emit");
        std::int64_t err = 0;
        const std::int64_t r = run(exe, 10, &err);
        check(err == 0 && r == 116, "seven result == 116");
    }
    {
        Segment s = seven();
        JitShape sh{0, 1};
        const auto bytes = emit_arch(&s, sh);
        check(contains_push_saved(bytes), "seven uses callee-saved push");
    }
    {
        Segment s = small();
        JitShape sh{0, 1};
        const auto bytes = emit_arch(&s, sh);
        check(!contains_push_saved(bytes), "small has no callee-saved push");
        Executable exe;
        check(make_executable(bytes, &exe), "small emit");
        std::int64_t err = 0;
        (void)run(exe, 7, &err);
        check(err == 0, "small executes");
    }
    // 2) ABI canary：JIT 调用前后 R12-R15 必须不变
    {
        Segment s = seven();
        JitShape sh{0, 1};
        Executable exe;
        check(make_executable(emit_arch(&s, sh), &exe), "canary emit");
        JitFn fn = reinterpret_cast<JitFn>(exe.mem);
        std::int64_t err = 0;
        std::int64_t arg = 10;
        std::int64_t* errp = &err;
        std::uint64_t a = 0, b = 0, c = 0, d = 0;
        asm volatile(
            "mov $0x1122334455667788, %%r12\n"
            "mov $0x2233445566778899, %%r13\n"
            "mov $0x33445566778899aa, %%r14\n"
            "mov $0x445566778899aabb, %%r15\n"
            "mov %[arg], %%rdi\n"
            "mov %[errp], %%rsi\n"
            "call *%[fn]\n"
            "mov %%r12, %[a]\n"
            "mov %%r13, %[b]\n"
            "mov %%r14, %[c]\n"
            "mov %%r15, %[d]\n"
            : [a] "=m"(a), [b] "=m"(b), [c] "=m"(c), [d] "=m"(d)
            : [fn] "m"(fn), [arg] "m"(arg), [errp] "m"(errp)
            : "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11",
              "r12", "r13", "r14", "r15", "memory", "cc");
        check(err == 0, "canary err");
        check(a == 0x1122334455667788ULL && b == 0x2233445566778899ULL &&
                  c == 0x33445566778899aaULL && d == 0x445566778899aabbULL,
              "callee-saved R12-R15 preserved");
    }
    // 3) 比较结果严格 0/1（setcc 高位清零）
    {
        Segment s = bool_shape();
        JitShape sh{0, 1};
        Executable exe;
        check(make_executable(emit_arch(&s, sh), &exe), "bool emit");
        std::int64_t err = 0;
        check(run(exe, 3, &err) == 1 && err == 0, "bool true == 1");
        check(run(exe, 9, &err) == 0 && err == 0, "bool false == 0");
    }
    // 4) 比较 + 后向 br：循环结果正确
    {
        Segment s = backward_loop();
        JitShape shape{1, 0};  // src=1, dst=0
        Executable exe;
        check(make_executable(emit_arch(&s, shape), &exe), "loop emit");
        std::int64_t err = 0;
        check(run(exe, 5, &err) == 6 && err == 0, "backward loop result");
    }
    // 5) 自环 goto：发射必须包含 guard 递减（结构断言）
    {
        Segment s = self_loop();
        JitShape sh{-1, -1};
        const auto bytes = emit_arch(&s, sh);
        bool has_sub_rcx = false;
        for (std::size_t i = 0; i + 3 < bytes.size(); i++)
            if (bytes[i] == 0x48 && bytes[i + 1] == 0x83 &&
                bytes[i + 2] == 0xE9 && bytes[i + 3] == 0x01)
                has_sub_rcx = true;
        check(has_sub_rcx, "self-loop decrements guard");
    }
    // 6) 池溢出：emit 直接 fail-closed（不调用 verify）
    {
        Segment s;
        const int src = push_var(&s, "*[0,-1]", -1);
        const int dst = push_var(&s, "*[0,1]", 1);
        const int l0 = push_lit(&s, 0);
        push_insn(&s, rwir::Op::Copy, 1, {l0}, {dst});
        int locs[12];
        int s0 = 2;
        for (int k = 0; k < 12; k++) {
            locs[k] = push_var(&s, "loc", 0);
            push_insn(&s, rwir::Op::Add, s0++, {src, dst}, {locs[k]}, true, 1);
        }
        for (int k = 0; k < 12; k++)
            push_insn(&s, rwir::Op::Add, s0++, {dst, locs[k]}, {dst}, true, 1);
        push_insn(&s, rwir::Op::Return, s0, {}, {});
        JitShape sh{0, 1};
        check(emit_arch(&s, sh).empty(), "pool overflow fails closed");
    }

    if (g_failures == 0) {
        std::printf("OK jit_x64_abi_test\n");
        return 0;
    }
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
}
