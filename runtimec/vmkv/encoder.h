#ifndef RUNTIMEC_VMKV_ENCODER_H
#define RUNTIMEC_VMKV_ENCODER_H

/*
 * 组件：vmkv/encoder
 * 作用：读取并验证明文 RWIR，将 operation name 和 typed named parameters
 *       映射为 opid/argidx，再把静态参数 key 解引用为非零 64 位 argaddr，
 *       最终等价编码为 canonical、不可变的 binrwir_program_t。
 * 联系：engine route 在 compilation repository 未命中时调用；源只经
 *       vmkv/source 与主机服务读取；结果交给 BinRWIR Compiler。
 * 约束：vmkv 只有 RWIR 到 canonical BinRWIR 这一条正常方向。每个静态 key
 *       必须通过 host service resolve_static_value_body_address 解析；动态 key、
 *       零地址及无法保证 snapshot 生命周期的地址返回 UNSUPPORTED 或错误，且不
 *       产生 Program。编码不得优化、融合、生成执行计划或机器码，也不得执行。
 */

#include "binrwir/program.h"
#include "contract/allocator.h"
#include "contract/snapshot.h"
#include "contract/status.h"
#include "vmkv/rwir.h"
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

/*
 * VMKV 地址化的唯一窄入口。它先验证明文 parameter type/name/key，再拒绝
 * dynamic key；静态 key 只能调用 host service 得到非零地址后生成 argument。
 * 该函数不读取 V.body，也不缓存 key、xvalue 或 body。
 */
runtimec_status_t vmkv_encode_static_argument(
    const vmkv_encode_request_t *request,
    const vmkv_rwir_argument_view_t *source_argument,
    uint32_t argument_index,
    binrwir_argument_t *output_argument,
    runtimec_error_t *error);

/*
 * 成功时只返回与已验证 RWIR 等价的 canonical BinRWIR Program。任一 instruction
 * 的 operation name、parameter type/name 或 argidx 映射不匹配时失败；遇到动态
 * key 必须返回 UNSUPPORTED，并保持 output_program 为 NULL。
 */
runtimec_status_t vmkv_encode(
    vmkv_encoder_t *encoder,
    const vmkv_encode_request_t *request,
    binrwir_program_t **output_program,
    runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
