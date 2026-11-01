// 阶段 A 交叉校验：layout 发出的 typed 指令 vs「按 union/promotion 选出的实现」。
//
// 用法：union_cross_check <dsn> <function>...
// 对每个函数做通用编译（不特化），逐条比对：
//   - layout 已发 typed：必须与 union 按操作数类型选出的实现完全一致；
//   - layout 发泛型但操作数全已知、表里存在对应 typed 变体：记为「漏特化」。
// 操作数类型未知（char/容器/动态槽）的指令计入 undecidable。
// 退出码：0 无差异（编译失败的函数记为 skip，不算差异）；1 有差异；2 用法/环境。
#include <cstdio>
#include <string>
#include <vector>

#include <kvspace/kvspace.h>

#include "binircompiler/compiler.hpp"

namespace {

using kvlang::runtimec2::binircompiler::CompileError;
using kvlang::runtimec2::binircompiler::CompileOptions;
using kvlang::runtimec2::binircompiler::Segment;
using kvlang::runtimec2::binircompiler::UnionCheckReport;

std::string normalize_function(std::string value) {
    while (value.size() > 5 && value.back() == '/')
        value.pop_back();
    if (value.empty() || value[0] != '/')
        value = "/lib/" + value;
    return value;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <dsn> </lib/function>...\n", argv[0]);
        return 2;
    }
    void* handle = kvspaceConnect(argv[1]);
    if (!handle) {
        std::fprintf(stderr, "union_cross_check: cannot connect to %s\n", argv[1]);
        return 2;
    }
    UnionCheckReport total;
    std::size_t skipped = 0;
    int rc = 0;
    for (int i = 2; i < argc; i++) {
        const std::string func_dir = normalize_function(argv[i]);
        Segment segment;
        CompileError error;
        if (!kvlang::runtimec2::binircompiler::compile(
                handle, func_dir, CompileOptions{}, &segment, &error)) {
            std::printf("skip %s: %s\n", func_dir.c_str(), error.reason.c_str());
            skipped++;
            continue;
        }
        UnionCheckReport report;
        kvlang::runtimec2::binircompiler::CrossCheckUnion(segment, &report, &error);
        std::printf("func=%s checked=%zu matched=%zu mismatched=%zu "
                    "undecidable=%zu no_typed_variant=%zu\n",
                    func_dir.c_str(), report.checked, report.matched,
                    report.mismatched, report.undecidable,
                    report.no_typed_variant);
        for (const std::string& detail : report.details)
            std::printf("  %s\n", detail.c_str());
        total.checked += report.checked;
        total.matched += report.matched;
        total.mismatched += report.mismatched;
        total.undecidable += report.undecidable;
        total.undecidable_constant += report.undecidable_constant;
        total.undecidable_member += report.undecidable_member;
        total.undecidable_pointer += report.undecidable_pointer;
        total.undecidable_untyped += report.undecidable_untyped;
        total.no_typed_variant += report.no_typed_variant;
        if (report.mismatched != 0)
            rc = 1;
    }
    std::printf("summary: checked=%zu matched=%zu mismatched=%zu "
                "undecidable=%zu skipped=%zu no_typed_variant=%zu "
                "und_const=%zu und_member=%zu und_pointer=%zu und_untyped=%zu\n",
                total.checked, total.matched, total.mismatched,
                total.undecidable, skipped, total.no_typed_variant,
                total.undecidable_constant, total.undecidable_member,
                total.undecidable_pointer, total.undecidable_untyped);
    kvspaceClose(handle);
    return rc;
}
