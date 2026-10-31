#pragma once

#include <cstdlib>
#include <cstring>

#include "codeseg.hpp"

// §2·8 binir 序列打印（执行前，javap / objdump -d 式）。
// 唯一目的：让人类与 CI 逐条对照 rwir 明文 ↔ binir 指令，检查语义、操作数、
// 跳转、常量池，并可视化 §2·7.1 覆盖落点（JIT / INTERP）。
// 只看 Segment，不碰 kvspace、不建段、不执行。
//
// 零开销契约：本函数只在被调用时构造字符串。开关判定归调用方：
//   RC2_DUMP_BINIR=1   每次建段打印一次（缓存命中不重复）
//   RC2_DUMP_BINIR=all 每次执行前都打印
namespace kvlang::runtimec2 {

// 开关判定：进程内只读一次环境变量；未设置时零开销。
inline const char* binir_dump_mode() {
    static const char* mode = std::getenv("RC2_DUMP_BINIR");
    return mode;
}
inline bool binir_dump_on_build() {
    const char* m = binir_dump_mode();
    return m && (std::strcmp(m, "1") == 0 || std::strcmp(m, "all") == 0);
}
inline bool binir_dump_on_run() {
    const char* m = binir_dump_mode();
    return m && std::strcmp(m, "all") == 0;
}

void dump_binir(const Segment& seg, const char* func_dir);

}  // namespace kvlang::runtimec2
