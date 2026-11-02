#ifndef RUNTIMEC_HOST_HOST_H
#define RUNTIMEC_HOST_HOST_H

/*
 * host 是 runtimec 对外部能力的唯一所有者。它只持有一份经过精确版本
 * 校验的能力表，不拥有 BinRWIR，不参与 BinRWIR Compiler 的 JIT/VMBIN
 * 执行目标选择，也不提交 checkpoint。它是静态 key 到稳定 XValue 基址解析
 * 合同的唯一入口；基址只借用到 snapshot source revision 失效为止。
 */

#include "contract/error.h"
#include "contract/host_services.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct runtimec_host runtimec_host_t;

runtimec_status_t runtimec_host_create(
    const runtimec_host_services_t *services, runtimec_host_t **output,
    runtimec_error_t *error);
runtimec_status_t runtimec_host_destroy(
    runtimec_host_t *host, runtimec_error_t *error);
runtimec_status_t runtimec_host_get_services(
    const runtimec_host_t *host,
    const runtimec_host_services_t **output, runtimec_error_t *error);
runtimec_status_t runtimec_host_resolve_static_xvalue_address(
    const runtimec_host_t *host, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key,
    runtimec_xvalue_address_t *output_xvalue_address,
    runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
