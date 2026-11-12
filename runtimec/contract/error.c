
#include "contract/error.h"

void runtimec_error_reset(runtimec_error_t *error) {
    if (error != NULL) {
        error->status = RUNTIMEC_STATUS_SUCCESS;
        error->domain = RUNTIMEC_ERROR_DOMAIN_NONE;
        error->code = 0;
        error->message = runtimec_string_view_make(NULL, 0U);
        error->logical_program_counter = (runtimec_program_counter_t){
            0, RUNTIMEC_PROGRAM_COUNTER_ORDINAL_NONE};
    }
}

void runtimec_error_set(runtimec_error_t *error, runtimec_status_t status,
                        runtimec_error_domain_t domain, int64_t code,
                        runtimec_string_view_t message,
                        runtimec_program_counter_t logical_program_counter) {
    if (error != NULL) {
        error->status = status;
        error->domain = domain;
        error->code = code;
        error->message = message;
        error->logical_program_counter = logical_program_counter;
    }
}

bool runtimec_error_is_valid(const runtimec_error_t *error) {
    return error != NULL && runtimec_status_is_valid(error->status) &&
           error->domain >= RUNTIMEC_ERROR_DOMAIN_NONE &&
           error->domain <= RUNTIMEC_ERROR_DOMAIN_EXTENSION &&
           runtimec_string_view_is_valid(error->message) &&
           error->logical_program_counter.source_instruction_ordinal >=
               RUNTIMEC_PROGRAM_COUNTER_ORDINAL_NONE;
}

bool runtimec_error_is_set(const runtimec_error_t *error) {
    return error != NULL && error->status != RUNTIMEC_STATUS_SUCCESS;
}
