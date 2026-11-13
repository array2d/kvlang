#ifndef VMBIN_VALUE_ACCESS_H
#define VMBIN_VALUE_ACCESS_H

#include <stddef.h>
#include <stdint.h>

#include "binrwir/argument.h"
#include "contract/diagnostic_band.h"
#include "binrwir/binvthread_frame.h"
#include "contract/error.h"
#include "contract/pathspace_access.h"
#include "contract/status.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum vmbin_value_access_diagnostic {
    VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST = UINT32_C(0x4400) + 1U,
    VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_REFERENCE = UINT32_C(0x4400) + 2U,
    VMBIN_VALUE_ACCESS_DIAG_UNBOUND_SLOT = UINT32_C(0x4400) + 3U,
    VMBIN_VALUE_ACCESS_DIAG_MISSING_LITERAL = UINT32_C(0x4400) + 4U,
    VMBIN_VALUE_ACCESS_DIAG_LITERAL_IS_READ_ONLY = UINT32_C(0x4400) + 5U,
    VMBIN_VALUE_ACCESS_DIAG_SIZE_MISMATCH = UINT32_C(0x4400) + 6U,
    VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE = UINT32_C(0x4400) + 7U,
    VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE = UINT32_C(0x4400) + 8U,
    
    VMBIN_VALUE_ACCESS_DIAG_DEREFERENCE_TARGET_IS_NOT_POINTER =
        UINT32_C(0x4400) + 9U
} vmbin_value_access_diagnostic_t;

_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST, RUNTIMEC_DIAGNOSTIC_BAND_VALUE_ACCESS),
               "VMBIN_VALUE_ACCESS_DIAG_INVALID_REQUEST must stay inside its diagnostic band");
_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_REFERENCE, RUNTIMEC_DIAGNOSTIC_BAND_VALUE_ACCESS),
               "VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_REFERENCE must stay inside its diagnostic band");
_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(VMBIN_VALUE_ACCESS_DIAG_UNBOUND_SLOT, RUNTIMEC_DIAGNOSTIC_BAND_VALUE_ACCESS),
               "VMBIN_VALUE_ACCESS_DIAG_UNBOUND_SLOT must stay inside its diagnostic band");
_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(VMBIN_VALUE_ACCESS_DIAG_MISSING_LITERAL, RUNTIMEC_DIAGNOSTIC_BAND_VALUE_ACCESS),
               "VMBIN_VALUE_ACCESS_DIAG_MISSING_LITERAL must stay inside its diagnostic band");
_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(VMBIN_VALUE_ACCESS_DIAG_LITERAL_IS_READ_ONLY, RUNTIMEC_DIAGNOSTIC_BAND_VALUE_ACCESS),
               "VMBIN_VALUE_ACCESS_DIAG_LITERAL_IS_READ_ONLY must stay inside its diagnostic band");
_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(VMBIN_VALUE_ACCESS_DIAG_SIZE_MISMATCH, RUNTIMEC_DIAGNOSTIC_BAND_VALUE_ACCESS),
               "VMBIN_VALUE_ACCESS_DIAG_SIZE_MISMATCH must stay inside its diagnostic band");
_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE, RUNTIMEC_DIAGNOSTIC_BAND_VALUE_ACCESS),
               "VMBIN_VALUE_ACCESS_DIAG_UNKNOWN_LANGTYPE must stay inside its diagnostic band");
_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE, RUNTIMEC_DIAGNOSTIC_BAND_VALUE_ACCESS),
               "VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE must stay inside its diagnostic band");
_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(VMBIN_VALUE_ACCESS_DIAG_DEREFERENCE_TARGET_IS_NOT_POINTER, RUNTIMEC_DIAGNOSTIC_BAND_VALUE_ACCESS),
               "VMBIN_VALUE_ACCESS_DIAG_DEREFERENCE_TARGET_IS_NOT_POINTER must stay inside its diagnostic band");

typedef enum vmbin_value_fact {
    VMBIN_VALUE_FACT_UNKNOWN = 0,
    VMBIN_VALUE_FACT_NO = 1,
    VMBIN_VALUE_FACT_YES = 2
} vmbin_value_fact_t;

typedef struct vmbin_value_location {
    const uint8_t *read_bytes;
    uint8_t *write_bytes;
    size_t size;
    bool is_whole_wire;
    
    vmbin_value_fact_t body_is_empty;      
    vmbin_value_fact_t has_langtype_record;
    vmbin_value_fact_t is_directory_key;   
} vmbin_value_location_t;

vmbin_value_fact_t vmbin_value_location_is_none(
    const vmbin_value_location_t *location);

runtimec_status_t vmbin_value_access_locate(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument,
    vmbin_value_location_t *output_location, runtimec_error_t *error);

runtimec_status_t vmbin_value_access_read(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_bytes_buffer_t *output,
    runtimec_error_t *error);

runtimec_status_t vmbin_value_access_write(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_bytes_view_t value,
    runtimec_error_t *error);

runtimec_status_t vmbin_value_access_langtype(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument,
    runtimec_string_view_t *output_langtype, runtimec_error_t *error);

bool vmbin_value_access_langtype_is_structref(runtimec_string_view_t langtype);

bool vmbin_value_access_langtype_is_path_string(runtimec_string_view_t langtype);

bool vmbin_value_access_langtype_is_container(runtimec_string_view_t langtype);

runtimec_status_t vmbin_value_access_write_whole_value(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_bytes_view_t body,
    runtimec_error_t *error);

typedef enum vmbin_value_access_form {
    VMBIN_VALUE_ACCESS_FORM_VALUE = 0,
    VMBIN_VALUE_ACCESS_FORM_POINTER = 1,
    VMBIN_VALUE_ACCESS_FORM_EXTENSION = 2
} vmbin_value_access_form_t;

runtimec_status_t vmbin_value_access_form(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, uint8_t *output_form,
    runtimec_error_t *error);

runtimec_status_t vmbin_value_access_form_unfollowed(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, uint8_t *output_form,
    runtimec_error_t *error);

runtimec_status_t vmbin_value_access_locate_body(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument,
    vmbin_value_location_t *output_location, runtimec_error_t *error);

runtimec_status_t vmbin_value_access_locate_body_read(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument,
    vmbin_value_location_t *output_location, runtimec_error_t *error);

runtimec_status_t vmbin_value_access_locate_write(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument,
    vmbin_value_location_t *output_location, runtimec_error_t *error);

runtimec_status_t vmbin_value_access_read_body(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_bytes_buffer_t *output,
    runtimec_error_t *error);
runtimec_status_t vmbin_value_access_write_body(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_bytes_view_t value,
    runtimec_error_t *error);

runtimec_status_t vmbin_value_access_bind_write_target(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_key_view_t target_key,
    runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
