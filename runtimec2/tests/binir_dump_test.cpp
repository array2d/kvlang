// binir_dump 独立单测：人工构造 Segment，抓 stderr，断言 §2·8 关键行。
// 只依赖 include/binir_dump.hpp + src/binir_dump.cpp（纯头文件数据视图），
// 不链接 runtime、不连 kvspace。可直接 ctest：
//   add_executable(binir_dump_test tests/binir_dump_test.cpp src/binir_dump.cpp)
//   target_include_directories(binir_dump_test PRIVATE include . )
//   add_test(NAME c2_binir_dump COMMAND binir_dump_test)

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "binir_dump.hpp"

namespace {

using kvlang::runtimec2::CInsn;
using kvlang::runtimec2::LitFast;
using kvlang::runtimec2::Operand;
using kvlang::runtimec2::RegDesc;
using kvlang::runtimec2::Segment;
using kvlang::runtimec2::dump_binir;
using kvlang::runtimec2::rwir::Kind;
using kvlang::runtimec2::rwir::Op;

std::uint8_t push_var(Segment* seg, const char* name, RegDesc::VarClass vclass,
                      int param_k = 0) {
    RegDesc d;
    d.kind = Operand::Kind::Var;
    d.name = name;
    d.vclass = vclass;
    d.param_k = param_k;
    seg->regs.push_back(d);
    return static_cast<std::uint8_t>(seg->regs.size() - 1);
}

std::uint8_t push_const_int64(Segment* seg, std::int64_t value) {
    const std::size_t slot = seg->regs.size();
    RegDesc d;
    d.kind = Operand::Kind::Literal;
    d.slot_key = "/lib/demo·main/[" + std::to_string(slot) + ",0]";
    seg->regs.push_back(d);
    if (seg->lit.size() < seg->regs.size())
        seg->lit.resize(seg->regs.size());
    seg->lit[slot].is_i64 = true;
    seg->lit[slot].v = value;
    return static_cast<std::uint8_t>(slot);
}

std::uint8_t push_const_string(Segment* seg, const char* text) {
    const std::size_t slot = seg->regs.size();
    RegDesc d;
    d.kind = Operand::Kind::Literal;
    d.name = text;
    d.slot_key = "/lib/demo·main/[" + std::to_string(slot) + ",0]";
    seg->regs.push_back(d);
    if (seg->lit.size() < seg->regs.size())
        seg->lit.resize(seg->regs.size());
    return static_cast<std::uint8_t>(slot);
}

CInsn make_insn(Op op, Kind kind, std::uint8_t width, const char* name,
                std::vector<std::uint8_t> reads, std::vector<std::uint8_t> writes,
                int s0) {
    CInsn ci;
    ci.op = op;
    ci.kind = kind;
    ci.width = width;
    ci.name = name;
    ci.impl = op == Op::Call ? kvlang::runtimec2::rwir::Impl::Control
                             : kvlang::runtimec2::rwir::Impl::Typed;
    ci.s0 = s0;
    ci.nrd = static_cast<std::uint8_t>(reads.size());
    ci.nwr = static_cast<std::uint8_t>(writes.size());
    for (std::size_t k = 0; k < reads.size(); k++)
        ci.rd[k] = reads[k];
    for (std::size_t k = 0; k < writes.size(); k++)
        ci.wr[k] = writes[k];
    if (op == Op::Add && kind == Kind::Int && width == 8) {
        ci.fast_i64 = true;
        ci.opfn = 1;
    }
    return ci;
}

// 纯 A 桶 + 一条 call：JIT 7 / INTERP 1。
Segment demo_mixed() {
    Segment seg;
    const std::uint8_t a = push_var(&seg, "*[0,-1]", RegDesc::VarClass::ParamPtr, -1);
    const std::uint8_t b = push_var(&seg, "*[0,1]", RegDesc::VarClass::ParamPtr, 1);
    const std::uint8_t acc = push_var(&seg, "acc", RegDesc::VarClass::Plain);
    const std::uint8_t i = push_var(&seg, "i", RegDesc::VarClass::Plain);
    const std::uint8_t k42 = push_const_int64(&seg, 42);
    const std::uint8_t t = push_var(&seg, "t", RegDesc::VarClass::Plain);
    const std::uint8_t l1 = push_var(&seg, "L1", RegDesc::VarClass::Plain);
    const std::uint8_t res = push_var(&seg, "res", RegDesc::VarClass::Plain);
    (void)push_const_string(&seg, "/lib/data");
    seg.insns.push_back(
        make_insn(Op::Add, Kind::Int, 8, "int64·add", {a, b}, {acc}, 1));
    seg.insns.push_back(
        make_insn(Op::Add, Kind::Int, 8, "int64·add", {acc, k42}, {i}, 2));
    seg.insns.push_back(
        make_insn(Op::Lt, Kind::Int, 8, "int64·lt", {i, k42}, {t}, 3));
    seg.insns.push_back(make_insn(Op::Br, Kind::None, 0, "br", {t, l1}, {}, 4));
    CInsn call = make_insn(Op::Call, Kind::None, 0, "call", {a, acc}, {res}, 5);
    call.callee = "/lib/demo·f";
    seg.insns.push_back(call);
    seg.insns.push_back(make_insn(Op::Copy, Kind::None, 0, "=", {k42}, {i}, 6));
    seg.insns.push_back(make_insn(Op::Goto, Kind::None, 0, "goto", {l1}, {}, 7));
    seg.insns.push_back(make_insn(Op::Return, Kind::None, 0, "return", {res}, {}, 8));
    return seg;
}

// 段内含 rwir handler 指令 → 整段 INTERP；has_rwirhandler=1。
Segment demo_handler() {
    Segment seg;
    const std::uint8_t p = push_var(&seg, "*[0,-1]", RegDesc::VarClass::ParamPtr, -1);
    const std::uint8_t tmp = push_var(&seg, "tmp", RegDesc::VarClass::Plain);
    const std::uint8_t k7 = push_const_int64(&seg, 7);
    CInsn handler_insn;
    handler_insn.op = Op::None;
    handler_insn.kind = Kind::None;
    handler_insn.handler = kvlang::runtimec2::rwir::RwirHandlerId::ArrayNew;
    handler_insn.name = "compactarray·new";
    handler_insn.s0 = 1;
    handler_insn.nrd = 1;
    handler_insn.rd[0] = p;
    handler_insn.nwr = 1;
    handler_insn.wr[0] = tmp;
    seg.insns.push_back(handler_insn);
    seg.insns.push_back(
        make_insn(Op::Add, Kind::Int, 8, "int64·add", {tmp, k7}, {tmp}, 2));
    seg.has_rwirhandler = true;
    return seg;
}

std::string capture_stderr(const Segment& seg, const char* func_dir) {
    char path[] = "/tmp/binir_dump_test_XXXXXX";
    const int fd = mkstemp(path);
    if (fd < 0) {
        std::perror("mkstemp");
        std::exit(2);
    }
    const int saved = dup(STDERR_FILENO);
    if (saved < 0 || dup2(fd, STDERR_FILENO) < 0) {
        std::perror("dup2");
        std::exit(2);
    }
    dump_binir(seg, func_dir);
    std::fflush(stderr);
    const int wrote = dup2(saved, STDERR_FILENO);
    close(saved);
    close(fd);
    if (wrote < 0) {
        std::perror("dup2 restore");
        std::exit(2);
    }
    std::FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::perror("fopen");
        std::exit(2);
    }
    std::string text;
    char buf[4096];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
        text.append(buf, n);
    std::fclose(f);
    unlink(path);
    return text;
}

int failures = 0;

void expect(const std::string& haystack, const char* needle) {
    if (haystack.find(needle) != std::string::npos) {
        std::printf("  ok   %s\n", needle);
        return;
    }
    std::printf("  FAIL %s\n", needle);
    failures++;
}

void run(const char* title, const Segment& seg, const char* func_dir) {
    const std::string text = capture_stderr(seg, func_dir);
    std::printf("=== %s ===\n%s\n", title, text.c_str());
    if (std::strcmp(title, "mixed") == 0) {
        expect(text, "[binir] func=/lib/demo·main  ninsn=8 nslot=9 nreg=7 "
                     "nconst=2 has_rwirhandler=0");
        expect(text, "note: 常量当前物化为 RegDesc 的 Literal 槽");
        expect(text, "  pc   insn                          ; 来源 rwir");
        expect(text, "0000  add    r2, r0, r1");
        expect(text, "; int64·add");
        expect(text, "[JIT]");
        expect(text, "0001  add    r3, r2, #0");
        expect(text, "const[0]=42");
        expect(text, "0003  br_if  r4, r5");
        expect(text, "target=r5");
        expect(text, "call   /lib/demo·f, r2 → r6");
        expect(text, "[INTERP]");
        expect(text, "const[0] int64   42");
        expect(text, "const[1] string  \"/lib/data\"");
        expect(text, "reg[0]   kind=int   width=8");
        expect(text, "reg[5]   kind=plain width=0");
        expect(text, "exit: JIT 7 / INTERP 1  (87.5%)");
    } else {
        expect(text, "has_rwirhandler=1");
        expect(text, "0000  new    r1, r0");
        expect(text, "; compactarray·new");
        expect(text, "exit: JIT 0 / INTERP 2  (0.0%)");
        if (text.find("[JIT]") != std::string::npos) {
            std::printf("  FAIL handler 段不应出现 [JIT]\n");
            failures++;
        } else {
            std::printf("  ok   handler 段无 [JIT]\n");
        }
    }
}

}  // namespace

int main() {
    run("mixed", demo_mixed(), "/lib/demo·main");
    run("handler", demo_handler(), "/lib/demo·handler");
    if (failures) {
        std::printf("binir_dump_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("binir_dump_test: PASS\n");
    return 0;
}
