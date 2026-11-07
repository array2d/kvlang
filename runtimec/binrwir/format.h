#ifndef BINRWIR_FORMAT_H
#define BINRWIR_FORMAT_H

/*
 * 组件：binrwir/format
 * 作用：把 canonical Program 打印成可人工核对的文本，用于检查编译结果。
 * 联系：vmkv 编出的 Program 经本组件变成文本；`tmpbin` 的调试命令调用它。
 * 约束：纯内存视图 → 文本，不访问 kvspace、不执行、不依赖 vmkv。
 *
 * 打印分两个区，不能混：
 *   - 定义区：函数自己的签名 `rwir name(argument<j>: T) -> (result<j>: U)`，
 *     没有 body。被调用算子的签名**不逐条重复**——它在 capability 表里，重复
 *     就是第二份签名表。
 *   - 调用区：每个 source pc 上的一次调用，`[pc n] op(实参) -> (结果)`。
 *
 * 确定性：不打印地址、指针与任何 generation。`STATIC` 的 payload 是进程内 body
 * 地址，**不打印**——打印它会让同一份编译产物在两次运行里出现不同文本。
 * 无损：argidx、引用种类、槽号/池下标、池内容全部出现；省略任何一项，打印就不再
 * 是「编译结果的完整视图」。
 */

#include <stdbool.h>

#include "binrwir/binfunc.h"
#include "contract/status.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * opid → 算子名。未登记返回 false，调用方打印数值。
 *
 * 走回调而不是直接查表，是因为 `binrwir` 不得反向依赖 `vmkv`（编码层与指令层
 * 成环）。回调可以是 `vmkv_rwir_manifest_find_operation_by_identifier`。
 */
typedef bool (*binrwir_operation_name_function)(
    binrwir_operation_identifier_t operation_identifier,
    runtimec_string_view_t *output_name, void *context);

/*
 * 输出回调：每次交付一段文本，返回值是 sink 实际接受的长度。
 * 用回调而不是固定缓冲，是因为 Program 大小无上界，格式化不应要求调用方先猜容量。
 * 返回长度小于 chunk.size 表示 sink 接不下了，格式化就此以
 * INSUFFICIENT_CAPACITY 结束。
 */
typedef runtimec_string_view_t (*binrwir_format_sink_function)(
    void *context, runtimec_string_view_t chunk);

/*
 * 格式化整个 Program。
 *
 * function_name 可为空（Program 不存函数名，存名等于给同一事实造第二处真相）；
 * resolve_operation_name 可为 NULL，此时算子一律打印 `opid=0x…`，不猜名字。
 */
runtimec_status_t binrwir_binfunc_format(
    const binrwir_binfunc_t *program, runtimec_string_view_t function_name,
    binrwir_operation_name_function resolve_operation_name,
    void *resolve_context, binrwir_format_sink_function sink,
    void *sink_context);

/*
 * 一个把整份输出追加进调用方 `runtimec_bytes_buffer_t` 的现成 sink：
 * 缓冲不够时按需扩容（分配器来自 context），够则原地追加。
 * `context` 是 `binrwir_format_buffer_sink_context_t`。
 */
typedef struct binrwir_format_buffer_sink_context {
    runtimec_allocator_t allocator;
    runtimec_bytes_buffer_t *buffer;
} binrwir_format_buffer_sink_context_t;

runtimec_string_view_t binrwir_format_buffer_sink(void *context,
                                                  runtimec_string_view_t chunk);

#ifdef __cplusplus
}
#endif

#endif
