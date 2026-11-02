#ifndef VMBIN_PROFILE_H
#define VMBIN_PROFILE_H

/*
 * 组件：vmbin/profile
 * 作用：保存 session 拥有的可丢弃执行统计。
 * 联系：interpreter 更新；engine session 持有。
 * 约束：统计不是正确性依赖，也不得进入不可变 Program；只记录计数，绝不
 *       保存 argaddr 指向的 V.body 内容。
 */

#include <stddef.h>
#include <stdint.h>

#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vmbin_profile_counter {
    uint64_t instruction_count;
    uint64_t dispatch_ticks;
} vmbin_profile_counter_t;

typedef struct vmbin_profile {
    vmbin_profile_counter_t *counters;
    size_t counter_count;
    uint64_t frame_count;
    uint64_t instruction_count;
    uint32_t flags;
    uint32_t reserved_32_bits;
} vmbin_profile_t;

runtimec_status_t
vmbin_profile_initialize(vmbin_profile_t *profile,
                         vmbin_profile_counter_t *counter_storage,
                         size_t counter_count);
runtimec_status_t vmbin_profile_reset(vmbin_profile_t *profile);
runtimec_status_t vmbin_profile_destroy(vmbin_profile_t *profile);

#ifdef __cplusplus
}
#endif

#endif
