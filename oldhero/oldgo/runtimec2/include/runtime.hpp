#pragma once
#include "kvspace.hpp"

namespace kvlang::runtimec2 {

struct Runtime {
    Kvspace kv;
};

// 值对象（kvlangRuntime_t*）与内部 Runtime 的互转。
Runtime* runtime_of(void* rt) noexcept;

char* bootstrap_impl(Runtime* r, const char* funcname);
int execute_vthread_impl(Runtime* r, const char* vid, char** out_pc);
// h 版内核（native vthread·* handler 与 abi 共用；Runtime 实例在 native 内不可达）。
char* bootstrap_h(void* h, const char* funcname);
int execute_vthread_h(void* h, const char* vid, char** out_pc, bool observable);

}  // namespace kvlang::runtimec2
