#include "kvspace.hpp"

#include <new>

#include <kvspace/kvspace.h>

namespace kv::rt {

Kvspace::~Kvspace() { close(); }

Kvspace::Kvspace(Kvspace&& o) noexcept : h_(o.h_) { o.h_ = nullptr; }

Kvspace& Kvspace::operator=(Kvspace&& o) noexcept {
    if (this != &o) {
        close();
        h_ = o.h_;
        o.h_ = nullptr;
    }
    return *this;
}

bool Kvspace::open(const char* dsn) noexcept {
    close();
    h_ = kvspaceConnect(dsn);
    return h_ != nullptr;
}

void Kvspace::close() noexcept {
    if (h_) {
        kvspaceClose(h_);
        h_ = nullptr;
    }
}

}  // namespace kv::rt
