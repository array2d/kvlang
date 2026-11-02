/*
 * 组件：engine 的 scaffold 实现
 * 作用：session 与 Program repository 的 fail-closed 入口；输出指针
 *       一律先置 NULL，不伪造空 session/空 lease。
 * 联系：公共接口的 Connect/Bootstrap 调用；真实实现建立显式 owner graph
 *       与 lease/generation 语义。
 */

#include "engine/program_repository.h"
#include "engine/session.h"

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
    if (host_services == NULL || options == NULL || output_session == NULL) {
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
runtimec_status_t runtimec_program_repository_create(
    runtimec_allocator_t allocator,
    size_t capacity,
    runtimec_program_repository_t **output_repository,
    runtimec_error_t *error) {
    (void)allocator;
    (void)capacity;
    (void)error;
    if (output_repository != NULL) {
        *output_repository = NULL;
    }
    if (output_repository == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
runtimec_status_t runtimec_program_repository_destroy(
    runtimec_program_repository_t *repository) {
    if (repository == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
runtimec_status_t runtimec_program_repository_find(
    runtimec_program_repository_t *repository,
    const runtimec_program_repository_key_t *key,
    runtimec_program_lookup_result_t *output_lookup_result,
    runtimec_program_lease_t *output_lease,
    runtimec_error_t *error) {
    (void)repository;
    (void)key;
    (void)error;
    if (output_lookup_result != NULL) {
        *output_lookup_result = RUNTIMEC_PROGRAM_LOOKUP_INVALID;
    }
    if (output_lease != NULL) {
        *output_lease = (runtimec_program_lease_t){0};
    }
    if (repository == NULL || key == NULL || output_lookup_result == NULL ||
        output_lease == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
runtimec_status_t runtimec_program_repository_insert(
    runtimec_program_repository_t *repository,
    const runtimec_program_repository_key_t *key,
    vmbin_program_t *program,
    runtimec_program_lease_t *output_lease,
    runtimec_error_t *error) {
    (void)error;
    if (output_lease != NULL) {
        *output_lease = (runtimec_program_lease_t){0};
    }
    if (repository == NULL || key == NULL || program == NULL ||
        output_lease == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
runtimec_status_t runtimec_program_repository_release(
    runtimec_program_repository_t *repository,
    runtimec_program_lease_t *lease) {
    if (repository == NULL || lease == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
runtimec_status_t runtimec_program_repository_invalidate(
    runtimec_program_repository_t *repository,
    const runtimec_snapshot_t *snapshot) {
    if (repository == NULL || snapshot == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
runtimec_status_t runtimec_program_repository_clear(
    runtimec_program_repository_t *repository) {
    if (repository == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
