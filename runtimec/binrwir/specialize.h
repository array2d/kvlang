#ifndef BINRWIR_SPECIALIZE_H
#define BINRWIR_SPECIALIZE_H

/*
 * 组件：binrwir/specialize
 * 作用：把 Program 里的通用算子指令按操作数的**静态类型组合**改写成特化 opid。
 * 联系：`vmkv` 编码产出 Program 之后、第一次执行之前调用一次；查找表与常量在
 *       `binrwir/operation_identifiers.h`（同一个生成器产出并校验）。
 * 约束：
 *   - **纯函数**：产出新 Program，旧的不动，失败不改输入；
 *   - **per Program 一次并缓存**：调用点在装载层，绝不放进 `vmbin_execute`——
 *     那里每条指令执行前都会走到，复制一份 Program 的代价与其大小成正比，
 *     等于把编译期的活干成运行期的活；
 *   - **元组不命中就保留原 opid**，不拒绝整条 Program（见下）。
 */

#include "binrwir/program.h"
#include "contract/allocator.h"
#include "contract/error.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 产出一份特化后的新 Program。
 *
 * 每一条指令：
 *   1. 取实参的静态 langtype（LITERAL → 字面量池，LOCAL/STATIC → 符号池，
 *      PARAMETER_* → 签名类型表），逐项归约成**基名**（`*int64` → `int64`，
 *      `[3]int64` → `int64`）；
 *   2. 拼成元组串（读参按声明序在前、写参在后，同型也写全）；
 *   3. 在查找表里查 `(base opid, 元组串)`。命中 → 用特化 opid；不命中 → 原样保留。
 *
 * **不命中为什么保留而不是拒绝整条 Program**：一条函数里某个局部变量没拿到静态
 * 类型，不该让同 Program 里无关的其他函数一起跑不了。执行到那条指令时注册表按
 * base opid 查不到处理函数，返回 `UNSUPPORTED`——错误仍然响亮、且定位到那一条
 * 指令（opid + 元组），只是延迟到使用点。副作用是它会把「layout 不传播被调方
 * 返回类型」这类缺口暴露成执行期错误，而不是被哪个通用 handler 悄悄吃掉。
 *
 * 符号池、字面量池、签名参数、别名规则**按原序搬运**，不重排：重排会要求改写
 * 所有 `PARAMETER_*` 引用的 slot，引入一整类新 bug，而没有任何收益。
 */
runtimec_status_t binrwir_program_specialize(
    const binrwir_program_t *program, const runtimec_allocator_t *allocator,
    binrwir_program_t **output_program, runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
