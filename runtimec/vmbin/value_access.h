#ifndef VMBIN_VALUE_ACCESS_H
#define VMBIN_VALUE_ACCESS_H

/*
 * 组件：vmbin/value_access
 * 作用：阶段 1 的唯一值访问面——把参数项解析成「值体地址 + 当前长度」或
 *       「Program 池内字面量」，并在同一条路径上读写。
 * 联系：vmbin/interpreter 的 core 规则与 builtin handler 都只经这里取写值。
 * 约束：以 shm 后端为准——值体就在进程内，按地址直接读写，不做 key 往返。
 *       头/体几何全部经合同：基址 → 值体用 runtimec_xvalue_body_address，值体 →
 *       当前长度用 runtimec_value_body_length，本组件不写偏移算术。长度 0 是合法
 *       的空值体（None），地址的有效性由来源证明而不是由长度判断。写只允许与
 *       当前值体等长：截断、静默扩容、回退按 key 都被禁止，扩容只有存储层能做。
 */

#include <stddef.h>
#include <stdint.h>

#include "binrwir/argument.h"
#include "binrwir/execution_frame.h"
#include "contract/error.h"
#include "contract/host_services.h"
#include "contract/status.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 一个参数项解析出的位置：可读写值体，或只读的池内字面量。 */
typedef struct vmbin_value_location {
    const uint8_t *read_bytes;
    uint8_t *write_bytes;
    size_t size;
} vmbin_value_location_t;

/*
 * 解析一个参数项。字面量给出池内只读视图；其余给出可直接读写的值体与它当前的
 * 长度。写位置不可用（字面量）时 write_bytes 为 NULL。
 */
runtimec_status_t vmbin_value_access_locate(
    const binrwir_execution_frame_t *frame,
    const binrwir_argument_t *argument,
    vmbin_value_location_t *output_location, runtimec_error_t *error);

/*
 * 读一个参数项的值字节到 output->data。容量不足时返回 INSUFFICIENT_CAPACITY，
 * 并把 output->size 置成所需字节数，调用方扩容后重试。
 */
runtimec_status_t vmbin_value_access_read(
    const binrwir_execution_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_bytes_buffer_t *output,
    runtimec_error_t *error);

/*
 * 写一个参数项的值字节。长度必须与当前值体一致：放不下、需要改长度或字面量目标
 * 一律 INVALID。只有全部字节写完之后才返回 SUCCESS。
 */
runtimec_status_t vmbin_value_access_write(
    const binrwir_execution_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_bytes_view_t value,
    runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
