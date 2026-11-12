#ifndef VMBIN_VTHREAD_STATUS_H
#define VMBIN_VTHREAD_STATUS_H

#include <stddef.h>
#include <stdint.h>

#include "binrwir/binvthread_frame.h"
#include "contract/error.h"
#include "contract/status.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RUNTIMEC_VTHREAD_STATUS_HEAD_LENGTH KVSPACE_XVALUE_HEAD_LEN
#define RUNTIMEC_VTHREAD_STATUS_CONTENT_LIMIT 16U
#define RUNTIMEC_VTHREAD_STATUS_CAPACITY 80U

#define RUNTIMEC_VTHREAD_STATUS_LANGTYPE "[]char/utf8"

runtimec_status_t runtimec_vthread_status_handle(
    const binrwir_binvthread_frame_t *frame, void **output_handle);

runtimec_status_t runtimec_vthread_status_key(
    runtimec_key_view_t frame_path, runtimec_string_view_t suffix, char *buffer,
    size_t capacity);

runtimec_status_t runtimec_vthread_status_write(
    void *kvspace_handle, const char *status_key, runtimec_bytes_view_t status);

runtimec_status_t runtimec_vthread_status_read(
    void *kvspace_handle, const char *status_key,
    runtimec_string_view_t *output_text, runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
