#ifndef RUNTIMEC_CONTRACT_MEMBER_KEY_TEXT_H
#define RUNTIMEC_CONTRACT_MEMBER_KEY_TEXT_H

#include <stddef.h>
#include <stdint.h>

#include "contract/error.h"
#include "contract/status.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum runtimec_member_key_text_diagnostic {
    
    RUNTIMEC_MEMBER_KEY_TEXT_DIAG_INVALID_REQUEST = 1,
    
    RUNTIMEC_MEMBER_KEY_TEXT_DIAG_INSUFFICIENT_CAPACITY = 2,
    
    RUNTIMEC_MEMBER_KEY_TEXT_DIAG_UNSUPPORTED_LANGTYPE = 3,
    
    RUNTIMEC_MEMBER_KEY_TEXT_DIAG_MALFORMED_BODY = 4,
    
    RUNTIMEC_MEMBER_KEY_TEXT_DIAG_NON_FINITE = 5
} runtimec_member_key_text_diagnostic_t;

runtimec_status_t runtimec_member_key_text(runtimec_string_view_t langtype,
                                           runtimec_bytes_view_t body,
                                           char *output, size_t capacity,
                                           size_t *output_length,
                                           runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
