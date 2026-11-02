#ifndef RUNTIMEC_VMKV_COMPILER_H
#define RUNTIMEC_VMKV_COMPILER_H

/*
 * 组件：vmkv/compiler
 * 作用：读取并校验明文 RWIR，调用 vmbin Program 构建器产出不可变
 *       BinRWIR Program。
 * 联系：engine route 在 Program repository 未命中时调用；源只经
 *       vmkv/source 与主机服务读取；Program 由 vmbin 持有。
 * 约束：vmkv 只有 RWIR 到 BinRWIR Program 这一条正常方向。它不执行、
 *       不生成机器码、不提交持久事实，也不依赖 vmjit。未实现路径
 *       必须返回 RUNTIMEC_STATUS_NOT_IMPLEMENTED。
 */

#include "contract/allocator.h"
#include "contract/snapshot.h"
#include "contract/status.h"
#include "vmbin/program.h"
#include "vmkv/source.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vmkv_compiler_t vmkv_compiler_t;

/* 一次编译请求。校验是强制步骤，不提供绕过开关。 */
typedef struct vmkv_compile_request_t {
    const runtimec_host_services_t *host_services;
    runtimec_string_view_t function_path;
    runtimec_snapshot_t expected_snapshot;
} vmkv_compile_request_t;

runtimec_status_t vmkv_compiler_create(
    runtimec_allocator_t allocator,
    vmkv_compiler_t **output_compiler,
    runtimec_error_t *error);

runtimec_status_t vmkv_compiler_destroy(vmkv_compiler_t *compiler);

/* 编译一个函数；成功时只返回不可变 BinRWIR Program。 */
runtimec_status_t vmkv_compile(
    vmkv_compiler_t *compiler,
    const vmkv_compile_request_t *request,
    vmbin_program_t **output_program,
    runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
