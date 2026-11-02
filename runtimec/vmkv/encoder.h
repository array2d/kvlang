#ifndef RUNTIMEC_VMKV_ENCODER_H
#define RUNTIMEC_VMKV_ENCODER_H

/*
 * 组件：vmkv/encoder
 * 作用：读取并验证明文 RWIR，将其等价编码为 canonical、不可变的
 *       binrwir_program_t。
 * 联系：engine route 在 compilation repository 未命中时调用；源只经
 *       vmkv/source 与主机服务读取；结果交给 BinRWIR Compiler。
 * 约束：vmkv 只有 RWIR 到 canonical BinRWIR 这一条正常方向。编码不得
 *       优化、类型特化、融合、生成执行计划或生成机器码，也不得执行程序、
 *       提交持久事实或依赖 BinRWIR JIT。
 */

#include "binrwir/program.h"
#include "contract/allocator.h"
#include "contract/snapshot.h"
#include "contract/status.h"
#include "vmkv/source.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vmkv_encoder_t vmkv_encoder_t;

/* 一次等价编码请求。RWIR 验证是强制步骤，不提供绕过开关。 */
typedef struct vmkv_encode_request_t {
    const runtimec_host_services_t *host_services;
    runtimec_string_view_t function_path;
    runtimec_snapshot_t expected_snapshot;
} vmkv_encode_request_t;

runtimec_status_t vmkv_encoder_create(
    runtimec_allocator_t allocator,
    vmkv_encoder_t **output_encoder,
    runtimec_error_t *error);

runtimec_status_t vmkv_encoder_destroy(vmkv_encoder_t *encoder);

runtimec_status_t vmkv_encoder_get_canonical_encoding_revision(
    const vmkv_encoder_t *encoder,
    uint32_t *output_canonical_encoding_revision);

/* 成功时只返回与已验证 RWIR 等价的 canonical BinRWIR Program。 */
runtimec_status_t vmkv_encode(
    vmkv_encoder_t *encoder,
    const vmkv_encode_request_t *request,
    binrwir_program_t **output_program,
    runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
