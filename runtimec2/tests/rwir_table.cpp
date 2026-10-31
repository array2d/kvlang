// 生成表自检：305 项；policy 172/133；Impl 五类互斥完备 166/6/54/78/1。
#include <cstdio>
#include <cstring>

#include "rwir.hpp"

int main() {
    int base = 0, complex = 0, missing = 0;
    int typed = 0, control = 0, dynamic = 0, native = 0, handoff = 0;
    bool watch_handoff = false, get_native = false, set_native = false;
    for (int i = 0; i < kvlang::runtimec2::rwir::kMyrwircapsN; i++) {
        const auto& e = kvlang::runtimec2::rwir::myrwircaps[i];
        if (e.cls == kvlang::runtimec2::rwir::Class::Base)
            base++;
        else
            complex++;
        switch (e.impl) {
        case kvlang::runtimec2::rwir::Impl::Typed: typed++; break;
        case kvlang::runtimec2::rwir::Impl::Control: control++; break;
        case kvlang::runtimec2::rwir::Impl::Dynamic: dynamic++; break;
        case kvlang::runtimec2::rwir::Impl::Native: native++; break;
        case kvlang::runtimec2::rwir::Impl::Handoff: handoff++; break;
        default:
            std::fprintf(stderr, "missing impl: %s\n", e.name);
            missing++;
            break;
        }
        if (std::strcmp(e.name, "kvspace·watch") == 0)
            watch_handoff = e.impl == kvlang::runtimec2::rwir::Impl::Handoff;
        else if (std::strcmp(e.name, "kvspace·get") == 0)
            get_native = e.impl == kvlang::runtimec2::rwir::Impl::Native;
        else if (std::strcmp(e.name, "kvspace·set") == 0)
            set_native = e.impl == kvlang::runtimec2::rwir::Impl::Native;
        if ((e.impl == kvlang::runtimec2::rwir::Impl::Typed || e.impl == kvlang::runtimec2::rwir::Impl::Control ||
             e.impl == kvlang::runtimec2::rwir::Impl::Dynamic) &&
            e.op == kvlang::runtimec2::rwir::Op::None) {
            std::fprintf(stderr, "missing op: %s\n", e.name);
            missing++;
        }
    }
    if (!watch_handoff)
        std::fprintf(stderr, "expected Handoff impl: kvspace·watch\n");
    if (!get_native)
        std::fprintf(stderr, "expected Native impl: kvspace·get\n");
    if (!set_native)
        std::fprintf(stderr, "expected Native impl: kvspace·set\n");
    if (kvlang::runtimec2::rwir::kMyrwircapsN != 305 || base != 172 || complex != 133 || missing != 0 ||
        typed != 166 || control != 6 || dynamic != 54 || native != 78 || handoff != 1 ||
        !watch_handoff || !get_native || !set_native) {
        std::fprintf(stderr,
                     "ops table: n=%d base=%d complex=%d missing=%d "
                     "impl=%d/%d/%d/%d/%d\n",
                     kvlang::runtimec2::rwir::kMyrwircapsN, base, complex, missing, typed, control,
                     dynamic, native, handoff);
        return 1;
    }
    std::printf("OK ops table (305 = 172 base + 133 complex; impl 166/6/54/78/1)\n");
    return 0;
}
