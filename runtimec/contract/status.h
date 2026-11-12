#ifndef RUNTIMEC_CONTRACT_STATUS_H
#define RUNTIMEC_CONTRACT_STATUS_H

#include <stdbool.h>

#include <kvlang/runtimec.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct runtimec_error runtimec_error_t;

bool runtimec_status_is_valid(runtimec_status_t status);

bool runtimec_status_is_failure(runtimec_status_t status);

const char *runtimec_status_name(runtimec_status_t status);

#ifdef __cplusplus
}
#endif

#endif
