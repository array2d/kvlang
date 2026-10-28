#pragma once
#include "kvspace.hpp"

namespace kv::rt {

struct Runtime {
    Kvspace kv;
};

// 值对象（kvlangRuntime_t*）与内部 Runtime 的互转。
Runtime* runtime_of(void* rt) noexcept;

char* bootstrap_impl(Runtime* r, const char* funcname);
int execute_vthread_impl(Runtime* r, const char* vid, char** out_pc);

}  // namespace kv::rt
