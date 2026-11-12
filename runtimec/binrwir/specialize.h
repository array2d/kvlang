#ifndef BINRWIR_SPECIALIZE_H
#define BINRWIR_SPECIALIZE_H

#include "binrwir/binfunc.h"
#include "contract/allocator.h"
#include "contract/error.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

runtimec_status_t binrwir_binfunc_specialize(
    const binrwir_binfunc_t *program, const runtimec_allocator_t *allocator,
    binrwir_binfunc_t **output_program, runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
