#ifndef VMBIN_PROFILE_H
#define VMBIN_PROFILE_H

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
