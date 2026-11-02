/* engine/compilation_repository 的失败封闭骨架。 */

#include "engine/compilation_repository.h"

runtimec_status_t runtimec_compilation_repository_create(
    runtimec_allocator_t allocator,
    size_t capacity,
    runtimec_compilation_repository_t **output_repository,
    runtimec_error_t *error) {
    (void)error;
    if (output_repository != NULL) {
        *output_repository = NULL;
    }
    if (!runtimec_allocator_is_valid(&allocator) || capacity == 0U ||
        output_repository == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t runtimec_compilation_repository_destroy(
    runtimec_compilation_repository_t *repository) {
    if (repository == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t runtimec_compilation_repository_find(
    runtimec_compilation_repository_t *repository,
    const runtimec_compilation_repository_key_t *key,
    runtimec_compilation_repository_lookup_result_t *output_lookup_result,
    runtimec_compilation_repository_lease_t *output_lease,
    runtimec_error_t *error) {
    (void)error;
    if (output_lookup_result != NULL) {
        *output_lookup_result = RUNTIMEC_COMPILATION_REPOSITORY_LOOKUP_INVALID;
    }
    if (output_lease != NULL) {
        *output_lease = (runtimec_compilation_repository_lease_t){0};
    }
    if (repository == NULL || key == NULL || output_lookup_result == NULL ||
        output_lease == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t runtimec_compilation_repository_insert(
    runtimec_compilation_repository_t *repository,
    const runtimec_compilation_repository_key_t *key,
    binrwir_compilation_t *compilation,
    runtimec_compilation_repository_lease_t *output_lease,
    runtimec_error_t *error) {
    (void)error;
    if (output_lease != NULL) {
        *output_lease = (runtimec_compilation_repository_lease_t){0};
    }
    if (repository == NULL || key == NULL || compilation == NULL ||
        output_lease == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t runtimec_compilation_repository_release(
    runtimec_compilation_repository_t *repository,
    runtimec_compilation_repository_lease_t *lease) {
    if (repository == NULL || lease == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t runtimec_compilation_repository_invalidate(
    runtimec_compilation_repository_t *repository,
    const runtimec_snapshot_t *snapshot) {
    if (repository == NULL || snapshot == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t runtimec_compilation_repository_clear(
    runtimec_compilation_repository_t *repository) {
    if (repository == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
