#ifndef VMBIN_FRAME_TARGET_KEY_H
#define VMBIN_FRAME_TARGET_KEY_H

#include <stddef.h>

#include "binrwir/binvthread_frame.h"
#include "contract/error.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

runtimec_status_t vmbin_frame_target_key(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, char *buffer, size_t capacity);

runtimec_status_t vmbin_value_access_argument_key(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, char *buffer, size_t capacity,
    runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
