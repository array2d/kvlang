#ifndef RUNTIMEC_HOST_KVSPACE_ADAPTER_H
#define RUNTIMEC_HOST_KVSPACE_ADAPTER_H

/*
 * kvspace adapter 只把一个借用的 kvspace 句柄转换为 host 能力表，包括
 * 静态 key 的稳定 XValue 基址解析。当前尚未连接 kvspace C 函数，因此
 * 唯一入口明确返回 NOT_IMPLEMENTED，不发布空解析器伪装成功。
 */

#include "contract/allocator.h"
#include "contract/error.h"
#include "contract/host_services.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct runtimec_kvspace_adapter_request {
    runtimec_allocator_t allocator;
    void *borrowed_kvspace_handle;
} runtimec_kvspace_adapter_request_t;

runtimec_status_t runtimec_kvspace_adapter_create_services(
    const runtimec_kvspace_adapter_request_t *request,
    runtimec_host_services_t *output, runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
