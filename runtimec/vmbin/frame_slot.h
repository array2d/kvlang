#ifndef VMBIN_FRAME_SLOT_H
#define VMBIN_FRAME_SLOT_H

#include <stddef.h>
#include <stdint.h>

#include "binrwir/argument.h"
#include "binrwir/frame_bindings.h"
#include "binrwir/binfunc.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

runtimec_status_t vmbin_frame_slot_capacity(const binrwir_binfunc_t *program,
                                            size_t *output_capacity);

runtimec_status_t vmbin_frame_slot_of_argument(
    const binrwir_binfunc_t *program, const binrwir_argument_t *argument,
    uint16_t *output_slot);

runtimec_status_t vmbin_frame_slot_of_parameter(
    const binrwir_binfunc_t *program, binrwir_parameter_direction_t direction,
    uint32_t parameter_index, uint16_t *output_slot);

runtimec_status_t vmbin_frame_slot_of_local_symbol(
    const binrwir_binfunc_t *program, uint16_t symbol_index,
    uint16_t *output_slot);

#ifdef __cplusplus
}
#endif

#endif
