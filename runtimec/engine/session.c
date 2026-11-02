/* engine/session 的失败封闭骨架。 */

#include "engine/session.h"

#include "binrwir_compiler/compiler.h"
#include "engine/compilation_repository.h"
#include "vmkv/encoder.h"

struct runtimec_session_t {
    runtimec_host_services_t host_services;
    runtimec_session_options_t options;
    vmkv_encoder_t *vmkv_encoder;
    binrwir_compiler_t *binrwir_compiler;
    runtimec_compilation_repository_t *compilation_repository;
};

runtimec_status_t runtimec_session_create(
    const runtimec_host_services_t *host_services,
    const runtimec_session_options_t *options,
    runtimec_session_t **output_session,
    runtimec_error_t *error) {
    (void)host_services;
    (void)options;
    (void)error;
    if (output_session != NULL) {
        *output_session = NULL;
    }
    if (!runtimec_host_services_is_valid(host_services) || options == NULL ||
        options->structure_size != sizeof(*options) ||
        !runtimec_allocator_is_valid(&options->allocator) ||
        options->compilation_cache_capacity == 0U ||
        (options->enable_jit && options->machine_code_capacity == 0U) ||
        (!options->enable_jit && options->machine_code_capacity != 0U) ||
        output_session == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t runtimec_session_destroy(runtimec_session_t *session) {
    if (session == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t runtimec_session_get_host_services(
    runtimec_session_t *session,
    const runtimec_host_services_t **output_host_services) {
    if (output_host_services != NULL) {
        *output_host_services = NULL;
    }
    if (session == NULL || output_host_services == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t runtimec_session_clear_derived_state(
    runtimec_session_t *session) {
    if (session == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
