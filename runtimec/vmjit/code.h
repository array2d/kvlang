#ifndef RUNTIMEC_VMJIT_CODE_H
#define RUNTIMEC_VMJIT_CODE_H

/*
 * 组件：vmjit/code
 * 作用：已发布机器码的引用计数句柄：retain/release 保证执行期间 code
 *       不被回收；entry 返回调用入口。
 * 联系：code cache 产生并持有；engine/route 在执行前取 code lease；
 *       retired 后等最后一个 lease 释放再回收只读可执行页。
 * 约束：句柄不暴露机器地址给持久层；事务边界由 route 保证。
 */

#include "contract/snapshot.h"
#include "vmjit/backend.h"
#include "vmjit/execution_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vmjit_code vmjit_code_t;
runtimec_status_t vmjit_code_retain(vmjit_code_t *code);
runtimec_status_t vmjit_code_release(vmjit_code_t *code);
runtimec_status_t vmjit_code_get_machine_code_function(
    const vmjit_code_t *code,
    vmjit_machine_code_function_t *output_function);

#ifdef __cplusplus
}
#endif

#endif
