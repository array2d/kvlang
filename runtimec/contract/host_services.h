#ifndef RUNTIMEC_CONTRACT_HOST_SERVICES_H
#define RUNTIMEC_CONTRACT_HOST_SERVICES_H

/*
 * host 向 engine、vmkv 与 vmbin 提供的唯一外部能力表。能力表只暴露
 * kvspace 操作，不暴露 kvspace 结构。静态 key 可被解析为生命周期受 snapshot
 * 约束的 V.body 地址；动态 key 不在当前 binary interface 中。缺失能力必须返回
 * NOT_IMPLEMENTED；二进制接口版本必须精确一致，不提供旧布局兼容路径。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "contract/allocator.h"
#include "contract/checkpoint.h"
#include "contract/error.h"
#include "contract/snapshot.h"
#include "contract/status.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RUNTIMEC_HOST_SERVICES_BINARY_INTERFACE_VERSION 2U
#define RUNTIMEC_VALUE_BODY_ADDRESS_NONE UINT64_C(0)

typedef uint64_t runtimec_value_body_address_t;

typedef struct runtimec_host_value_header {
    uint64_t value_size;
    uint64_t value_revision;
    uint32_t flags;
    uint32_t reserved;
} runtimec_host_value_header_t;

typedef runtimec_status_t (*runtimec_host_list_visitor_function_t)(
    void *context, runtimec_key_view_t key, runtimec_error_t *error);

typedef struct runtimec_handoff_request {
    runtimec_string_view_t operation;
    runtimec_checkpoint_t checkpoint;
} runtimec_handoff_request_t;

typedef runtimec_status_t (*runtimec_host_read_function_t)(
    void *context, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_bytes_buffer_t *output,
    runtimec_error_t *error);
typedef runtimec_status_t (*runtimec_host_read_header_function_t)(
    void *context, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_host_value_header_t *output,
    runtimec_error_t *error);
typedef runtimec_status_t (*runtimec_host_write_function_t)(
    void *context, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_bytes_view_t value,
    runtimec_error_t *error);
typedef runtimec_status_t (*runtimec_host_write_partial_function_t)(
    void *context, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, uint64_t offset,
    runtimec_bytes_view_t value, runtimec_error_t *error);
typedef runtimec_status_t (*runtimec_host_remove_function_t)(
    void *context, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_error_t *error);
typedef runtimec_status_t (*runtimec_host_list_function_t)(
    void *context, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t prefix,
    runtimec_host_list_visitor_function_t visitor,
    void *visitor_context, runtimec_error_t *error);
typedef runtimec_status_t (*runtimec_host_watch_function_t)(
    void *context, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_bytes_view_t expected,
    uint64_t timeout_nanoseconds, runtimec_bytes_buffer_t *observed,
    runtimec_error_t *error);
typedef runtimec_status_t (*runtimec_host_signal_function_t)(
    void *context, runtimec_key_view_t key, runtimec_error_t *error);
typedef runtimec_status_t (*runtimec_host_source_revision_function_t)(
    void *context, runtimec_key_view_t function_key,
    runtimec_source_revision_t *revision, runtimec_error_t *error);
/*
 * 只解析静态 key。SUCCESS 必须返回非零的 V.body 进程内地址，且 host 必须
 * 保证该地址在 snapshot.source_revision 失效前保持有效和不变。动态 key、
 * 尚未分配的 key，或无法提供上述生命周期保证的 key 必须返回 UNSUPPORTED，
 * 并把 output_body_address 保持为 RUNTIMEC_VALUE_BODY_ADDRESS_NONE。
 */
typedef runtimec_status_t
(*runtimec_host_resolve_static_value_body_address_function_t)(
    void *context, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key,
    runtimec_value_body_address_t *output_body_address,
    runtimec_error_t *error);
typedef runtimec_status_t (*runtimec_host_checkpoint_function_t)(
    void *context, const runtimec_checkpoint_t *checkpoint,
    runtimec_error_t *error);
typedef runtimec_status_t (*runtimec_host_handoff_function_t)(
    void *context, const runtimec_handoff_request_t *request,
    runtimec_error_t *error);

typedef struct runtimec_host_services {
    uint32_t binary_interface_version;
    void *context;
    runtimec_allocator_t allocator;
    runtimec_host_read_function_t read;
    runtimec_host_read_header_function_t read_header;
    runtimec_host_write_function_t write;
    runtimec_host_write_partial_function_t write_partial;
    runtimec_host_remove_function_t remove;
    runtimec_host_list_function_t list;
    runtimec_host_watch_function_t watch;
    runtimec_host_signal_function_t signal;
    runtimec_host_source_revision_function_t source_revision;
    runtimec_host_resolve_static_value_body_address_function_t
        resolve_static_value_body_address;
    runtimec_host_checkpoint_function_t checkpoint;
    runtimec_host_handoff_function_t handoff;
} runtimec_host_services_t;

void runtimec_host_value_header_reset(runtimec_host_value_header_t *header);
void runtimec_handoff_request_reset(runtimec_handoff_request_t *request);
bool runtimec_handoff_request_is_valid(
    const runtimec_handoff_request_t *request);
void runtimec_host_services_initialize(runtimec_host_services_t *services);
bool runtimec_host_services_is_valid(
    const runtimec_host_services_t *services);

#ifdef __cplusplus
static_assert(sizeof(runtimec_value_body_address_t) == 8U,
              "V.body addresses must remain 64 bits");
#else
_Static_assert(sizeof(runtimec_value_body_address_t) == 8U,
               "V.body addresses must remain 64 bits");
#endif

#ifdef __cplusplus
}
#endif

#endif
