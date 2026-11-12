#ifndef RUNTIMEC_CONTRACT_ERROR_H
#define RUNTIMEC_CONTRACT_ERROR_H

#include <stdbool.h>
#include <stdint.h>

#include "contract/program_counter.h"
#include "contract/status.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum runtimec_error_domain {
    RUNTIMEC_ERROR_DOMAIN_NONE = 0,
    RUNTIMEC_ERROR_DOMAIN_CONTRACT = 1,
    RUNTIMEC_ERROR_DOMAIN_HOST = 2,
    RUNTIMEC_ERROR_DOMAIN_STORAGE = 3,
    RUNTIMEC_ERROR_DOMAIN_CHECKPOINT = 4,
    RUNTIMEC_ERROR_DOMAIN_EXTENSION = 5
} runtimec_error_domain_t;

struct runtimec_error {
    runtimec_status_t status;        
    runtimec_error_domain_t domain;  
    int64_t code;                    
    runtimec_string_view_t message;  
    runtimec_program_counter_t logical_program_counter;
};

void runtimec_error_reset(runtimec_error_t *error);

void runtimec_error_set(runtimec_error_t *error, runtimec_status_t status,
                        runtimec_error_domain_t domain, int64_t code,
                        runtimec_string_view_t message,
                        runtimec_program_counter_t logical_program_counter);

bool runtimec_error_is_valid(const runtimec_error_t *error);

bool runtimec_error_is_set(const runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
