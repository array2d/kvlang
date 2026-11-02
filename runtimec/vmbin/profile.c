/*
 * 组件：vmbin/profile 的实现
 * 作用：初始化、清零和读取调用方拥有的统计存储。
 */

#include "profile.h"

#include <string.h>

runtimec_status_t
vmbin_profile_initialize(vmbin_profile_t *profile,
                    vmbin_profile_counter_t *counter_storage,
                    size_t counter_count) {
    if (profile == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    memset(profile, 0, sizeof(*profile));
    if (counter_count != 0U && counter_storage == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (counter_storage != NULL) {
        memset(counter_storage, 0, counter_count * sizeof(*counter_storage));
    }
    profile->counters = counter_storage;
    profile->counter_count = counter_count;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_profile_reset(vmbin_profile_t *profile) {
    if (profile == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (profile->counters != NULL) {
        memset(profile->counters, 0,
               profile->counter_count * sizeof(*profile->counters));
    }
    profile->frame_count = 0U;
    profile->instruction_count = 0U;
    profile->flags = 0U;
    profile->reserved_32_bits = 0U;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t vmbin_profile_destroy(vmbin_profile_t *profile) {
    if (profile == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    memset(profile, 0, sizeof(*profile));
    return RUNTIMEC_STATUS_SUCCESS;
}
