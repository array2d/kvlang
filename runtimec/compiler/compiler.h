#ifndef RUNTIMEC_COMPILER_COMPILER_H
#define RUNTIMEC_COMPILER_COMPILER_H

/*
 * 组件：BinRWIR Compiler
 * 作用：从 canonical BinRWIR Program 派生 execution plan，并在启用时于同一
 *       编译阶段请求 vmjit 编译机器码。输出是统一的 opaque compilation。
 * 联系：唯一内容输入是 binrwir_program_t；结果可明确选择 vmbin 或 vmjit。
 * 约束：不读取 RWIR 或 kvspace，不执行 Program；全部优化只在本模块完成，
 *       真实错误不得转换成 VMBIN compilation。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "binrwir/execution_plan.h"
#include "binrwir/program.h"
#include "contract/allocator.h"
#include "contract/status.h"
#include "vmjit/code.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vmjit_compiler_t vmjit_compiler_t;
typedef struct binrwir_compiler_t binrwir_compiler_t;
typedef struct binrwir_compilation_t binrwir_compilation_t;

typedef enum binrwir_execution_target_t {
    BINRWIR_EXECUTION_TARGET_INVALID = 0,
    BINRWIR_EXECUTION_TARGET_VMBIN = 1,
    BINRWIR_EXECUTION_TARGET_VMJIT = 2
} binrwir_execution_target_t;

/*
 * enable_vmjit 为 false 时 vmjit_compiler 必须为空；为 true 时必须非空。
 * create 成功后，vmjit_compiler 的所有权转入 BinRWIR Compiler；create
 * 失败时所有权仍属于调用者。structure_size 必须精确等于本结构大小。
 */
typedef struct binrwir_compiler_configuration_t {
    size_t structure_size;
    uint32_t compiler_revision;
    bool enable_vmjit;
    vmjit_compiler_t *vmjit_compiler;
} binrwir_compiler_configuration_t;

/*
 * 本视图借用 compilation 持有的对象。
 * compilation 对象只表示已经成功完成的编译。VMJIT 目标必须携带非空
 * vmjit_code；VMBIN 目标必须保持 vmjit_code 为空。编译错误不产生对象。
 */
typedef struct binrwir_compilation_view_t {
    binrwir_execution_target_t execution_target;
    const binrwir_program_t *canonical_program;
    const binrwir_execution_plan_t *execution_plan;
    const vmjit_code_t *vmjit_code;
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
