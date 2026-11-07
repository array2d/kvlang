#ifndef RUNTIMEC_BINRWIR_JIT_CODE_H
#define RUNTIMEC_BINRWIR_JIT_CODE_H

/*
 * 组件：binrwir_compiler/jit/code
 * 作用：已发布机器码的引用计数句柄：retain/release 保证执行期间 code
 *       不被回收；entry 返回调用入口。
 * 联系：binrwir_jit 编译阶段产生；BinRWIR compilation 持有 code，compilation
 *       repository 的 lease 保证执行期间有效；最后一个引用释放后回收
 *       只读可执行页。
 * 约束：句柄不暴露机器地址给持久层；事务边界由 route 保证。
 */

#include <stddef.h>

#include "contract/snapshot.h"
#include "binrwir_compiler/jit/backend.h"
#include "binrwir_compiler/jit/code_memory.h"
#include "binrwir_compiler/jit/execution_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct binrwir_jit_code binrwir_jit_code_t;
/* 成功时 code 接管已发布 memory；失败时 memory 仍归调用者。 */
runtimec_status_t binrwir_jit_code_create(
    binrwir_jit_code_memory_t *memory,
    size_t machine_code_size,
    size_t entry_byte_offset,
    binrwir_jit_code_t **output_code,
    runtimec_error_t *error);
runtimec_status_t binrwir_jit_code_retain(binrwir_jit_code_t *code);
runtimec_status_t binrwir_jit_code_release(binrwir_jit_code_t *code);
runtimec_status_t binrwir_jit_code_get_machine_code_function(
    const binrwir_jit_code_t *code,
    binrwir_jit_machine_code_function_t *output_function);

#ifdef __cplusplus
}
#endif

#endif
