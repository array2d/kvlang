#ifndef KVLANG_XVALUE_STORE_H
#define KVLANG_XVALUE_STORE_H

/*
 * 组件：kvlang/runtimec —— 按 dsn + key 读写**带 langtype 的 XValue**。
 *
 * 这一层只做一件事：把「一条 XValue」与「(langtype, 文本)」对上。存储寻址、langtype
 * 解析、值编解码各自已经有唯一实现（kvspace / kvlangxvalue），这里只把它们串起来，
 * 不复制其中任何一段。
 *
 * ## 失败即终止，不返回错误码
 *
 * 与 `runtimec.h` 同一约定：**没有第二个降级点**。写不进去、类型不支持、wire 畸形，
 * 一律终止进程并打印原因。返回错误码会诱导调用方写 `if (rc)` 分支——那正是
 * 「代码以奇怪的方式继续运行」的起点。
 *
 * **唯一的例外是 `read` 返回 false**，且它只表示一件事：**key 不存在**。除此之外
 * 的任何问题（wire 畸形、langtype 无法解释、缓冲不足）都终止，不会用一个 false
 * 把不同性质的事混成同一件。
 *
 * ## 为什么读用调用方缓冲
 *
 * 调用方可能是 Python / Go / Rust，各有各的内存管理。返回 malloc 指针就得让每个
 * 绑定都实现「谁来 free」；缓冲由调用方给，零所有权问题——与 `kvspaceList` 的既有
 * 形态一致。
 */

#include <stdbool.h>
#include <stddef.h>

/* 与 runtimec.h 同一导出宏：两个头可能被同一个 TU include。 */
#if defined(_WIN32)
#  if defined(KVLANG_RUNTIMEC_BUILDING_LIBRARY)
#    define KVLANG_XVALUE_STORE_EXPORT __declspec(dllexport)
#  else
#    define KVLANG_XVALUE_STORE_EXPORT __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define KVLANG_XVALUE_STORE_EXPORT __attribute__((visibility("default")))
#else
#  define KVLANG_XVALUE_STORE_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* 存储句柄：`kvspaceConnect` 的原样返回值。不透明。 */
typedef void *kvlang_xvalue_store_t;

/*
 * 打开一个存储。`dsn` 只接受 `shm://`（与 `kvspaceConnect` 同契约）；其余 scheme
 * 返回 NULL——**不终止**，因为「连不上」是调用方要能报告的启动期错误，不是容器内的
 * 逻辑错误。
 */
KVLANG_XVALUE_STORE_EXPORT kvlang_xvalue_store_t kvlang_xvalue_store_open(
    const char *dsn);

KVLANG_XVALUE_STORE_EXPORT void kvlang_xvalue_store_close(
    kvlang_xvalue_store_t store);

/*
 * 按 `langtype` 构造一条完整 XValue（head + body + attr）并写入 `key`。
 *
 * `value_text` 是**文本**，按 `langtype` 解释成字节（`int64` 的 `42`、`[]char/utf8`
 * 的 `abc`）。不支持文本解释的类型（Ptr / map / 定长数组）→ 终止并报
 * 「该类型不支持文本解释」。
 *
 * `key` 已存在则覆盖（旧值的 langtype 不参与本次解释——以 `langtype` 实参为准）。
 */
KVLANG_XVALUE_STORE_EXPORT void kvlang_xvalue_store_write_new(
    kvlang_xvalue_store_t store, const char *key, const char *langtype,
    const char *value_text);

/*
 * 按 `key` **现有** XValue 的 langtype 写入。
 *
 * 类型从目标值的 attr 记录读（tag 1），**不猜**：
 *   - `key` 不存在 → 终止（没有类型可依）；
 *   - `key` 存在但没有 langtype 记录 → 终止（同上）。
 *
 * 这样 `writeT` 永远不会「因为写错类型而静默改变一个键的类型」。
 */
KVLANG_XVALUE_STORE_EXPORT void kvlang_xvalue_store_write_typed(
    kvlang_xvalue_store_t store, const char *key, const char *value_text);

/*
 * 读 `key`，把 langtype 文本与解释后的值文本写进调用方缓冲。
 *
 * 返回 false **只表示 key 不存在**（此时两个输出缓冲都不写）。
 * wire 畸形、langtype 无法解释、缓冲不足（capacity 不够放含 NUL 的结果）→ 终止。
 *
 * 两个输出缓冲都必须是调用方给的、容量非 0 的缓冲；传 NULL 终止。
 */
KVLANG_XVALUE_STORE_EXPORT bool kvlang_xvalue_store_read(
    kvlang_xvalue_store_t store, const char *key, char *langtype_out,
    size_t langtype_capacity, char *value_out, size_t value_capacity);

#ifdef __cplusplus
}
#endif

#endif
