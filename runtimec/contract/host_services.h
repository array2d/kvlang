#ifndef RUNTIMEC_CONTRACT_HOST_SERVICES_H
#define RUNTIMEC_CONTRACT_HOST_SERVICES_H

/*
 * host 向 engine、vmkv 与 vmbin 提供的唯一外部能力表。能力表只暴露
 * kvspace 操作，不暴露 kvspace 结构。静态 key 可被解析为生命周期受 snapshot
 * 约束的 XValue 基址，V.body 地址只能由固定头长度安全推导；动态 key 不在当前
 * binary interface 中。缺失能力必须返回 NOT_IMPLEMENTED；二进制接口版本必须
 * 精确一致，不提供旧布局兼容路径。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kvspace/xvalue_head.h"

#include "contract/allocator.h"
#include "contract/checkpoint.h"
#include "contract/error.h"
#include "contract/snapshot.h"
#include "contract/status.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RUNTIMEC_HOST_SERVICES_BINARY_INTERFACE_VERSION 5U
#define RUNTIMEC_XVALUE_ADDRESS_NONE UINT64_C(0)
#define RUNTIMEC_XVALUE_BODY_OFFSET_BYTES KVSPACE_XVALUE_HEAD_LEN
#define RUNTIMEC_VALUE_BODY_ADDRESS_NONE UINT64_C(0)

typedef uint64_t runtimec_xvalue_address_t;
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
 * 解析静态 key。SUCCESS 必须返回非零的 XValue 进程内基址，且 host 必须
 * 保证该地址在 snapshot.source_revision 失效前保持有效和不变。动态 key、
 * 尚未分配的 key，或无法提供上述生命周期保证的 key 必须返回 UNSUPPORTED，
 * 并把 output_xvalue_address 保持为 RUNTIMEC_XVALUE_ADDRESS_NONE。
 * 这条保证的是 compilation 租约：地址在整份 compilation 有效期内不变。
 */
typedef runtimec_status_t
(*runtimec_host_resolve_static_xvalue_address_function_t)(
    void *context, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key,
    runtimec_xvalue_address_t *output_xvalue_address,
    runtimec_error_t *error);
/*
 * 解析帧 key（`/vthread/{vid}/[d]/…`）在**调用时**绑定的真地址。与静态那条
 * 并列而不合并：静态保证 compilation 租约，这条保证 generation——地址只在
 * output_generation 与当前 source generation 相等时有效，不等即失效，必须重绑。
 * SUCCESS 返回非零基址与非零 generation；动态拼出的 key、无法证明生命周期、
 * 尚未分配的槽一律 UNSUPPORTED，并把两个输出清零。
 */
typedef runtimec_status_t
(*runtimec_host_resolve_frame_xvalue_address_function_t)(
    void *context, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key,
    runtimec_xvalue_address_t *output_xvalue_address,
    uint64_t *output_generation, runtimec_error_t *error);
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
    runtimec_host_resolve_static_xvalue_address_function_t
        resolve_static_xvalue_address;
    runtimec_host_resolve_frame_xvalue_address_function_t
        resolve_frame_xvalue_address;
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

/*
 * V.body 地址推导：全系统**唯一**的「XValue 基址 + 固定头长度」实现，vmkv 与
 * vmbin 都调用它，各自不得再写偏移算术。纯函数：无状态、无分配、不查存储。
 * 零基址与加法溢出都返回 RUNTIMEC_VALUE_BODY_ADDRESS_NONE（不截断、不回绕）。
 */
runtimec_value_body_address_t
runtimec_xvalue_body_address(runtimec_xvalue_address_t xvalue_address);

/*
 * 上面那条的逆：值体地址退掉固定头长度就是 XValue 基址。纯算术，与 `+8` 同样
 * 只在本文件实现——调用方不得自己写 `body - 头长度`。零地址与下溢（值体地址
 * 小于固定头长度）都返回 RUNTIMEC_XVALUE_ADDRESS_NONE。不做任何存储校验：
 * 地址前是不是真值头由调用方凭地址来源自证。
 */
runtimec_xvalue_address_t
runtimec_value_body_xvalue_address(
    runtimec_value_body_address_t body_address);

/*
 * 反向：由值体地址读回当前值体长度。与上面的 `+8` 是同一条几何的两个方向，
 * 同样只在本文件实现——执行器不得再写 `body - 头长度`。
 *
 * 返回 0 表示「无法给出长度」：值体地址为零、小于固定头长度（下溢）、值头
 * storetype 不是已知档位，或长度超出 uint32。注意 **0 也可以是一个合法的空
 * 值体长度**（None 就是 len=0），两者无法从这里区分；需要严格区分的调用方
 * 必须先用别的证据确认地址有效（例如它来自编译期静态 payload 或刚被成功
 * 解析的绑定），再调用本函数。
 */
uint32_t runtimec_value_body_length(runtimec_value_body_address_t body_address);

#ifdef __cplusplus
static_assert(KVSPACE_XVALUE_HEAD_LEN == 8U,
              "KVspace XValue heads must remain 8 bytes");
static_assert(RUNTIMEC_XVALUE_BODY_OFFSET_BYTES == 8U,
              "V.body must remain 8 bytes after the XValue base");
static_assert(sizeof(runtimec_xvalue_address_t) == 8U,
              "XValue addresses must remain 64 bits");
static_assert(sizeof(runtimec_value_body_address_t) == 8U,
              "V.body addresses must remain 64 bits");
#else
_Static_assert(KVSPACE_XVALUE_HEAD_LEN == 8U,
               "KVspace XValue heads must remain 8 bytes");
_Static_assert(RUNTIMEC_XVALUE_BODY_OFFSET_BYTES == 8U,
               "V.body must remain 8 bytes after the XValue base");
_Static_assert(sizeof(runtimec_xvalue_address_t) == 8U,
               "XValue addresses must remain 64 bits");
_Static_assert(sizeof(runtimec_value_body_address_t) == 8U,
               "V.body addresses must remain 64 bits");
#endif

#ifdef __cplusplus
}
#endif

#endif
