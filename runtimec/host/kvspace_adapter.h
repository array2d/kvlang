#ifndef RUNTIMEC_HOST_KVSPACE_ADAPTER_H
#define RUNTIMEC_HOST_KVSPACE_ADAPTER_H

/*
 * 组件：host/kvspace_adapter
 * 作用：把一个借用的 kvspace 句柄转换成 host 能力表。它是 runtimec 里唯一
 *       调用 kvspace C 二进制接口的组件，也是静态 key 到 XValue 基址的唯一
 *       解析者；body 偏移不在这里决定。
 * 联系：host/kvspace_binary_interface 提供符号表形状；本模块把它绑定到一个
 *       真实或替身库，填满 contract/host_services.h 的每一项。
 * 约束：只借用句柄，不连接、不关闭、不拥有；动态拼出的 key 不解析成地址；
 *       任何失败都先清零输出再返回明确状态。
 */

#include <stdbool.h>
#include <stdint.h>

#include "contract/allocator.h"
#include "contract/error.h"
#include "contract/host_services.h"
#include "contract/status.h"
#include "host/kvspace_binary_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct runtimec_kvspace_adapter_request {
    runtimec_allocator_t allocator;
    void *borrowed_kvspace_handle;
} runtimec_kvspace_adapter_request_t;

/*
 * 创建一个能力表，context 指向本模块自己分配的所有者结构。句柄由调用方
 * 借用并在 services 生命周期内保持有效；本模块不会关闭它。
 */
runtimec_status_t runtimec_kvspace_adapter_create_services(
    const runtimec_kvspace_adapter_request_t *request,
    const runtimec_kvspace_binary_interface_t *binary_interface,
    runtimec_host_services_t *output, runtimec_error_t *error);

/* 释放 create 分配的所有者结构；不关闭句柄。表会被重新置为空表。 */
runtimec_status_t runtimec_kvspace_adapter_destroy_services(
    runtimec_host_services_t *services, runtimec_error_t *error);

/* 该 key 是否属于可以解析成稳定基址的静态 key。供调用方与测试直接查询。 */
bool runtimec_kvspace_adapter_key_is_static(runtimec_key_view_t key);

#ifdef __cplusplus
}
#endif

#endif
