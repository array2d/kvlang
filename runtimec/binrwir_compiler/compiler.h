#ifndef RUNTIMEC_BINRWIR_COMPILER_COMPILER_H
#define RUNTIMEC_BINRWIR_COMPILER_COMPILER_H

/*
 * 组件：BinRWIR Compiler
 * 作用：从 canonical BinRWIR Program 派生 execution plan，并在启用时于同一
 *       编译阶段尝试生成 JIT 机器码。输出是统一的 opaque compilation。
 * 联系：唯一内容输入是 binrwir_program_t；编译阶段一次性选择 VMBIN 或 JIT。
 * 约束：不读取 RWIR 或 kvspace，不执行 Program；全部优化只在本模块完成。
 *       JIT 不可编译只形成 VMBIN plan；真实编译错误不得转换成 VMBIN plan。
 *       执行阶段不得再次探测 JIT，也不得在 JIT 与 VMBIN 之间回退。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "binrwir/execution_plan.h"
#include "binrwir/program.h"
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
 * JIT 不可编译产生 VMBIN compilation；编译错误不产生 compilation。
 */
typedef struct binrwir_compilation_view_t {
    binrwir_execution_target_t execution_target;
    const binrwir_program_t *canonical_program;
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
 * 唯一内容输入是 canonical_program。成功产生的 compilation 持有该 Program
 * 的不可变快照、execution plan 与可选机器码；调用者仍拥有输入 Program。
 */
runtimec_status_t binrwir_compiler_compile(
    binrwir_compiler_t *compiler,
    const binrwir_program_t *canonical_program,
    binrwir_compilation_t **output_compilation,
    runtimec_error_t *error);

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
