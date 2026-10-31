// 生成表自检：305 项；policy 172/133；Impl 166/6/54/1 + handler 78。
#include <cstdio>
#include <cstring>

#include "rwir.hpp"

using kvlang::runtimec2::rwir::Class;
using kvlang::runtimec2::rwir::Impl;
using kvlang::runtimec2::rwir::Op;
using kvlang::runtimec2::rwir::RwirHandlerId;

int main() {
    int base = 0, complex = 0, missing = 0;
    int typed = 0, control = 0, dynamic = 0, handler = 0, handoff = 0;
    bool watch_handoff = false, get_handler = false, set_handler = false;
    for (int i = 0; i < kvlang::runtimec2::rwir::kMyrwircapsN; i++) {
        const auto& e = kvlang::runtimec2::rwir::myrwircaps[i];
        if (e.cls == Class::Base)
            base++;
        else
            complex++;
        switch (e.impl) {
        case Impl::Typed: typed++; break;
        case Impl::Control: control++; break;
        case Impl::Dynamic: dynamic++; break;
        case Impl::Handoff: handoff++; break;
        default:
            if (e.handler != RwirHandlerId::None) {
                handler++;
            } else {
                std::fprintf(stderr, "missing impl: %s\n", e.name);
                missing++;
            }
            break;
        }
        if (std::strcmp(e.name, "kvspace·watch") == 0)
            watch_handoff = e.impl == Impl::Handoff;
        else if (std::strcmp(e.name, "kvspace·get") == 0)
            get_handler = e.handler == RwirHandlerId::KvspaceGet;
        else if (std::strcmp(e.name, "kvspace·set") == 0)
            set_handler = e.handler == RwirHandlerId::KvspaceSet;
        if ((e.impl == Impl::Typed || e.impl == Impl::Control ||
             e.impl == Impl::Dynamic) &&
            e.op == Op::None) {
            std::fprintf(stderr, "missing op: %s\n", e.name);
            missing++;
        }
    }
    if (!watch_handoff)
        std::fprintf(stderr, "expected Handoff impl: kvspace·watch\n");
    if (!get_handler)
        std::fprintf(stderr, "expected KvspaceGet handler: kvspace·get\n");
    if (!set_handler)
        std::fprintf(stderr, "expected KvspaceSet handler: kvspace·set\n");
    if (kvlang::runtimec2::rwir::kMyrwircapsN != 305 || base != 172 ||
        complex != 133 || missing != 0 || typed != 166 || control != 6 ||
        dynamic != 54 || handler != 78 || handoff != 1 || !watch_handoff ||
        !get_handler || !set_handler) {
        std::fprintf(stderr,
                     "ops table: n=%d base=%d complex=%d missing=%d "
                     "impl=%d/%d/%d/%d handler=%d\n",
                     kvlang::runtimec2::rwir::kMyrwircapsN, base, complex,
                     missing, typed, control, dynamic, handoff, handler);
        return 1;
    }
    std::printf("OK ops table (305 = 172 base + 133 complex; "
                "impl 166/6/54/1 + handler 78)\n");
    return 0;
}
