// 生成表自检：300 项；policy 174/126；Impl 四类互斥完备 167/7/54/72。
#include <cstdio>

#include "ops.hpp"

int main() {
    int base = 0, complex = 0, missing = 0;
    int typed = 0, control = 0, dynamic = 0, handoff = 0;
    for (int i = 0; i < kv::ops::kBaseOpsN; i++) {
        const auto& e = kv::ops::kBaseOps[i];
        if (e.cls == kv::ops::Class::Base)
            base++;
        else
            complex++;
        switch (e.impl) {
        case kv::ops::Impl::Typed: typed++; break;
        case kv::ops::Impl::Control: control++; break;
        case kv::ops::Impl::Dynamic: dynamic++; break;
        case kv::ops::Impl::Handoff: handoff++; break;
        default:
            std::fprintf(stderr, "missing impl: %s\n", e.name);
            missing++;
            break;
        }
        if (e.impl != kv::ops::Impl::Handoff && e.op == kv::ops::Op::None) {
            std::fprintf(stderr, "missing op: %s\n", e.name);
            missing++;
        }
    }
    if (kv::ops::kBaseOpsN != 300 || base != 174 || complex != 126 || missing != 0 ||
        typed != 167 || control != 7 || dynamic != 54 || handoff != 72) {
        std::fprintf(stderr,
                     "ops table: n=%d base=%d complex=%d missing=%d "
                     "impl=%d/%d/%d/%d\n",
                     kv::ops::kBaseOpsN, base, complex, missing, typed, control,
                     dynamic, handoff);
        return 1;
    }
    std::printf("OK ops table (300 = 174 base + 126 complex; impl 167/7/54/72)\n");
    return 0;
}
