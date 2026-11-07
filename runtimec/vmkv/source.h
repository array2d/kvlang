#ifndef RUNTIMEC_VMKV_SOURCE_H
#define RUNTIMEC_VMKV_SOURCE_H

/*
 * 组件：vmkv/source
 * 作用：通过主机服务打开一个函数的明文 RWIR 源，并返回源快照。源保留
 *       operation name、parameter type、parameter name 与 parameter key。
 *       它是 vmkv 与 kvspace 之间唯一的读取入口，vmkv 不直接包含
 *       kvspace 结构。
 * 联系：vmkv/encoder 使用本接口读取 RWIR；读取结果只能进入顶层
 *       binrwir 的不可变 Program 构建器。
 * 约束：打开源不执行 RWIR、不生成机器码、不修改执行事实；失败
 *       不得留下函数源。
 */

#include "contract/kvspace_access.h"
#include "contract/snapshot.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 一次函数读取的句柄；生命周期内保证视图可借用。 */
typedef struct vmkv_function_source_t vmkv_function_source_t;

/* 打开函数源；失败时输出保持明确无效。 */
runtimec_status_t vmkv_source_open_function(
    runtimec_kvspace_access_t *kvspace_access,
    runtimec_string_view_t function_path,
    vmkv_function_source_t **output_function,
    runtimec_snapshot_t *output_snapshot,
    runtimec_error_t *error);

/* 关闭函数源；不提交、不修改 kvspace 事实。 */
runtimec_status_t vmkv_source_close_function(vmkv_function_source_t *function);

#ifdef __cplusplus
}
#endif

#endif
