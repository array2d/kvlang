/* layout 的 C ABI：供第三方（Rust/Python/C 扩展）把声明写进 kvspace，
 * 无需 fork 子进程、无需 .kv 文本、无需抄 defrwir 的 wire 字节布局。
 *
 * 符号在 cdylib（libkvlanglayout.so）中导出。
 *
 * 为什么这个能力在 layout 而不在 runtimec：defrwir 的 wire 格式唯一真相是
 * layout 的 `kvkind.rs`（`new_defrwir` / `new_def_langtype`）。抄一份进 runtimec
 * 就是第二份真相，两份一旦漂移就是静默读错。所以扩展方直接调 layout 声明，
 * runtimec 不新增对 layout 的依赖（裁决 2026-11-10 (c)，见 runtimec.h）。 */

#ifndef KVLANG_LAYOUT_H
#define KVLANG_LAYOUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 声明一个 rwir 扩展的签名，写进 `dsn` 指向的 kvspace。
 *
 * 写入内容（全部由 layout 的 kvkind 构造，调用方不拼字节）：
 *   /lib/<opcode>            kind = "def rwir"，body = 计数头（nr/nw/variadic）
 *   /lib/<opcode>/[0,-i]     kind = "def langtype"，body = read_langtypes[i-1]
 *   /lib/<opcode>/[0,+i]     kind = "def langtype"，body = write_langtypes[i-1]
 *                                   （write_inplace[i-1] 时追加 ` inplace`）
 *
 * 参数：
 *   dsn              kvspace DSN（如 `shm:///tmp/x.shm`）
 *   opcode           算子全名，**直接当全名用**：不拼 pkg 前缀，含 `·` 的
 *                    `pkg·op` 由调用方自己给出（如 `drivetest·unclaimed`）
 *   read_langtypes   读参类型串数组，长度 read_count；read_count>0 时不得为 NULL
 *   write_langtypes  写参类型串数组，长度 write_count；write_count>0 时不得为 NULL
 *   write_inplace    写参 inplace 位图，长度 write_count；NULL 表示全 false。
 *                    用显式数组而不是在类型串里加 ` inplace` 后缀：后者是字符串里的
 *                    魔法编码，调用方与读代码都要靠约定才看得懂。
 *                    inplace 表示该参数**在同一指令内既读又写**（`add(*acc, x) -> *acc`）。
 *   variadic         末读参是否为变参（源码里的 `T...`）。只在 read_count>0 时有意义。
 *   err / err_cap    失败时写入可读原因；成功不写。
 *
 * 返回 0 成功；-1 失败（err 非空）。
 *
 * 幂等与冲突：
 *   - 同一 opcode 重复声明 → **覆盖成功**（后一次生效）；
 *   - 若该 opcode 已由 rwfunc 体定义 → **报错**，一个 opcode 只能有一个定义。
 *     这与「同一声明的两次声明」不同：那是两个不同东西抢一个名字。
 */
int kvlangLayoutDefineRwir(const char *dsn, const char *opcode,
                           const char *const *read_langtypes, uint32_t read_count,
                           const char *const *write_langtypes, uint32_t write_count,
                           const uint8_t *write_inplace,
                           int variadic, char *err, uint32_t err_cap);

#ifdef __cplusplus
}
#endif

#endif
