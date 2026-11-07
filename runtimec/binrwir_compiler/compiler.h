#ifndef RUNTIMEC_BINRWIR_COMPILER_COMPILER_H
#define RUNTIMEC_BINRWIR_COMPILER_COMPILER_H

/*
 * 组件：BinRWIR Compiler
 * 作用：从 VMKV 已经地址化的 canonical BinRWIR Program 派生 execution plan，
 *       并在启用时于同一编译阶段尝试生成 JIT 机器码。输出是统一的 opaque
 *       compilation。
 * 联系：唯一内容输入是带非零 argaddr 的 binrwir_binfunc_t；编译阶段一次性
 *       选择 VMBIN 或 JIT。
 * 约束：不读取 RWIR 或 kvspace，不解析 key，不缓存 V.body，不执行 Program；
 *       通用 plan 不重复编码 instruction、地址或寄存器状态。
 *       JIT 不可编译只形成 VMBIN plan；真实编译错误不得转换成 VMBIN plan。
 *       执行阶段不得再次探测 JIT，也不得在 JIT 与 VMBIN 之间回退。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "binrwir/execution_plan.h"
#include "binrwir/binfunc.h"
#include "binrwir_compiler/jit/backend.h"
#include "binrwir_compiler/jit/code.h"
#include "contract/allocator.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct binrwir_compiler_t binrwir_compiler_t;
typedef struct binrwir_compilation_t binrwir_compilation_t;

typedef enum binrwir_execution_target_t {
    BINRWIR_EXECUTION_TARGET_INVALID = 0,
    BINRWIR_EXECUTION_TARGET_VMBIN = 1,
    BINRWIR_EXECUTION_TARGET_JIT = 2
} binrwir_execution_target_t;

/*
 * BinRWIR Compiler 根据这些值在内部创建并独占 JIT compiler，调用者不得
 * 构造或注入 JIT compiler 对象。enable_jit 为 true 时 capacity 必须非零，
 * target architecture 必须明确；为 false 时所有 JIT 字段必须保持零值。
 * create 必须复制 jit_target.triple 字节。configuration 只在调用期间有效。
 */
typedef struct binrwir_compiler_configuration_t {
    size_t structure_size;
    uint32_t compiler_revision;
    bool enable_jit;
    size_t jit_machine_code_capacity;
    binrwir_jit_target_t jit_target;
} binrwir_compiler_configuration_t;

/*
 * 本视图借用 compilation 持有的对象。compilation 只表示已经成功完成的编译。
 * JIT 目标必须携带非空 binrwir_jit_code；VMBIN 目标必须保持该字段为空。
 * VMBIN compilation 不携带 body cache；它只借用 Program 中已解析的 argaddr。
 * JIT 不可编译产生 VMBIN compilation；编译错误不产生 compilation。
 */
typedef struct binrwir_compilation_view_t {
    binrwir_execution_target_t execution_target;
    const binrwir_binfunc_t *canonical_program;
    const binrwir_execution_plan_t *execution_plan;
    const binrwir_jit_code_t *binrwir_jit_code;
} binrwir_compilation_view_t;

runtimec_status_t binrwir_compiler_create(
    const runtimec_allocator_t *allocator,
    const binrwir_compiler_configuration_t *configuration,
    binrwir_compiler_t **output_compiler,
    runtimec_error_t *error);

runtimec_status_t binrwir_compiler_destroy(
    binrwir_compiler_t *compiler,
    runtimec_error_t *error);

runtimec_status_t binrwir_compiler_get_revision(
    const binrwir_compiler_t *compiler,
    uint32_t *output_compiler_revision);

/*
 * 唯一内容输入是 program，其全部 argument 必须已由 VMKV 解析为非零 V.body
 * 地址。compilation 持有 Program、execution plan 与可选机器码，但不拥有地址
 * 指向的 body。地址只在生成它的 snapshot/source revision 有效期间可用；该
 * revision 失效时必须使整份 compilation 失效。
 *
 * 所有权：program 在这里被**接管**——成功时归 compilation，失败时由编译器
 * 就地销毁。无论成败调用方都不再拥有它，不得再销毁。不做「成功才消费、失败
 * 还给你」那种不对称契约，那正是 double free 的来源。
 */
runtimec_status_t binrwir_compiler_compile(
    binrwir_compiler_t *compiler, binrwir_binfunc_t *program,
    binrwir_compilation_t **output_compilation, runtimec_error_t *error);

runtimec_status_t binrwir_compilation_get_view(
    const binrwir_compilation_t *compilation,
    binrwir_compilation_view_t *output_view,
    runtimec_error_t *error);

runtimec_status_t binrwir_compilation_destroy(
    binrwir_compilation_t *compilation,
    runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
