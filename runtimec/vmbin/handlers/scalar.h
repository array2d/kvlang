#ifndef VMBIN_HANDLERS_SCALAR_H
#define VMBIN_HANDLERS_SCALAR_H

#include <stddef.h>
#include <stdint.h>

#include "binrwir/execution_exit.h"
#include "contract/diagnostic_band.h"
#include "binrwir/registry.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum runtimec_scalar_diagnostic {
    RUNTIMEC_SCALAR_DIAG_NONE = 0,
    
    RUNTIMEC_SCALAR_DIAG_DIVIDE_BY_ZERO = 0x4C01,
    
    RUNTIMEC_SCALAR_DIAG_DIVIDE_OVERFLOW = 0x4C02,
    
    RUNTIMEC_SCALAR_DIAG_NOT_A_NUMBER = 0x4C03,
    
    RUNTIMEC_SCALAR_DIAG_OPERAND_NOT_NUMERIC = 0x4C04,
    
    RUNTIMEC_SCALAR_DIAG_LOCATE_FAILED = 0x4C05,
    
    RUNTIMEC_SCALAR_DIAG_READ_ONLY_TARGET = 0x4C06,
    
    RUNTIMEC_SCALAR_DIAG_LENGTH_MISMATCH = 0x4C07,
    
    RUNTIMEC_SCALAR_DIAG_MALFORMED_CHARACTER = 0x4C08,
    
    RUNTIMEC_SCALAR_DIAG_NOT_A_SCALAR_VALUE = 0x4C09
} runtimec_scalar_diagnostic_t;

_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(RUNTIMEC_SCALAR_DIAG_NOT_A_SCALAR_VALUE, RUNTIMEC_DIAGNOSTIC_BAND_SCALAR),
               "RUNTIMEC_SCALAR_DIAG_NOT_A_SCALAR_VALUE must stay inside its diagnostic band");

static inline void runtimec_scalar_fail(binrwir_execution_exit_t *output_exit,
                                        uint32_t detail_code) {
    output_exit->kind = BINRWIR_EXECUTION_EXIT_ERROR;
    output_exit->status = RUNTIMEC_STATUS_INVALID;
    output_exit->detail_code = detail_code;
}

const binrwir_registry_entry_t *runtimec_scalar_handler_entries(
    size_t *output_count);

#ifdef __cplusplus
}
#endif

#endif
