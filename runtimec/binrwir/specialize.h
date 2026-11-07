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

#include "binrwir/binfunc.h"
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
 *   3. 在查找表里查 `(base opid, 元组串)`。**命中 → 用特化 opid；不命中 → 整份
 *      Program 拒绝**（`RUNTIMEC_STATUS_INVALID`，`error` 里带成因）。
 *
 * **不命中为什么拒绝而不是保留基础 opid**（2026-11-05 改）：
 *
 * 基础 opid 的通用 handler 已经按 p10「先删后增」删掉了（`scalar.c` 的 8 条
 * 算术 handler）。保留基础 opid 只会得到一条**必然 `UNSUPPORTED`** 的指令——
 * 那时错误出现在执行期、且被归因成「注册表里没有」，而真正的原因是「特化表
 * 缺一行」。回落把后者永久掩盖：表缺得越多，看起来越像「都正常执行了」。
 *
 * 拒绝时 `error` 的 `code` 区分两种成因，**因为它们指向完全不同的层**：
 *
 *   code 1（MISSING_OPERAND_TYPE）  操作数取不到 langtype，或元组串超缓冲。
 *                                   去修**生产者**（layout / vmkv），不是表。
 *   code 3（NO_SPECIALIZATION_ROW） 元组完整但表里没有这一行。
 *                                   去**补表**：`specification/rwir_catalog.def`
 *                                   的能力行 → `generate_rwir_catalog.py` 生成的
 *                                   `binrwir/specialization_table.c`。
 *
 * **已知限制**：`binrwir_specialize_tuple` 只返回元组长度、不返回失败原因，所以
 * 「操作数没有 langtype」与「元组串超容量」**目前被合并成 code 1**，上游区分不
 * 出来。要区分得改它的返回值语义（返回 `Option`/`Result` 而非 size）。这不是
 * 待办，是当前实现的边界——在它被改之前，code 1 的文本已如实写明两种可能。
 *
 * **延迟报错的好处**（改之前的设计意图，仍然成立）：一条函数里某个局部变量没
 * 拿到静态类型，不该让同 Program 里无关的其他函数一起跑不了。但现在这条已经
 * 不是权衡——基础 handler 删了，保留基础 opid 只会把「表缺行」伪装成「能力缺失」。
 *
 * 符号池、字面量池、签名参数、别名规则**按原序搬运**，不重排：重排会要求改写
 * 所有 `PARAMETER_*` 引用的 slot，引入一整类新 bug，而没有任何收益。
 */
runtimec_status_t binrwir_binfunc_specialize(
    const binrwir_binfunc_t *program, const runtimec_allocator_t *allocator,
    binrwir_binfunc_t **output_program, runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
