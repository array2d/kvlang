// 生成表自检：299 项；policy 172/127；Impl 四类互斥完备 166/6/54/72/1。
#include <cstdio>

#include "rwir.hpp"

int main() {
    int base = 0, complex = 0, missing = 0;
    int typed = 0, control = 0, dynamic = 0, native = 0, handoff = 0;
    for (int i = 0; i < kv::ops::kMyrwircapsN; i++) {
        const auto& e = kv::ops::myrwircaps[i];
        if (e.cls == kv::ops::Class::Base)
            base++;
        else
            complex++;
        switch (e.impl) {
        case kv::ops::Impl::Typed: typed++; break;
        case kv::ops::Impl::Control: control++; break;
        case kv::ops::Impl::Dynamic: dynamic++; break;
        case kv::ops::Impl::Native: native++; break;
        case kv::ops::Impl::Handoff: handoff++; break;
        default:
            std::fprintf(stderr, "missing impl: %s\n", e.name);
            missing++;
            break;
        }
        if ((e.impl == kv::ops::Impl::Typed || e.impl == kv::ops::Impl::Control ||
             e.impl == kv::ops::Impl::Dynamic) &&
            e.op == kv::ops::Op::None) {
            std::fprintf(stderr, "missing op: %s\n", e.name);
            missing++;
        }
    }
    if (kv::ops::kMyrwircapsN != 299 || base != 172 || complex != 127 || missing != 0 ||
        typed != 166 || control != 6 || dynamic != 54 || native != 72 ||
        handoff != 1) {
        std::fprintf(stderr,
                     "ops table: n=%d base=%d complex=%d missing=%d "
                     "impl=%d/%d/%d/%d/%d\n",
                     kv::ops::kMyrwircapsN, base, complex, missing, typed, control,
                     dynamic, native, handoff);
        return 1;
    }
    std::printf("OK ops table (299 = 172 base + 127 complex; impl 166/6/54/72/1)\n");
    return 0;
}
