#pragma once
#include <cstdint>

namespace kvlang::runtimec2 {

// kvspace 句柄的 RAII 包装：只做所有权与句柄传递，不做语义。
class Kvspace {
  public:
    Kvspace() = default;
    ~Kvspace();
    Kvspace(const Kvspace&) = delete;
    Kvspace& operator=(const Kvspace&) = delete;
    Kvspace(Kvspace&&) noexcept;
    Kvspace& operator=(Kvspace&&) noexcept;

    bool open(const char* dsn) noexcept;
    void close() noexcept;
    void* handle() const noexcept { return h_; }

  private:
    void* h_ = nullptr;
};

}  // namespace kvlang::runtimec2
