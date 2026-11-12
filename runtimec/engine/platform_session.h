#ifndef RUNTIMEC_ENGINE_PLATFORM_SESSION_H
#define RUNTIMEC_ENGINE_PLATFORM_SESSION_H

#include <stdbool.h>
#include <stddef.h>

#include "contract/allocator.h"
#include "contract/error.h"
#include "contract/status.h"
#include "engine/session.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct runtimec_platform_session_t runtimec_platform_session_t;

runtimec_allocator_t runtimec_default_allocator(void);

runtimec_status_t runtimec_platform_session_create(
    const char *data_source_name, uint64_t instance_identifier,
    const runtimec_session_options_t *options,
    runtimec_platform_session_t **output, runtimec_error_t *error);

runtimec_status_t runtimec_platform_session_destroy(
    runtimec_platform_session_t *platform_session, runtimec_error_t *error);

runtimec_status_t runtimec_platform_session_get_session(
    const runtimec_platform_session_t *platform_session,
    runtimec_session_t **output_session, runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
