/* host 的严格生命周期与能力表借用。 */

#include "host/host_internal.h"

#include <stdalign.h>
#include <string.h>

static runtimec_status_t runtimec_host_fail(
    runtimec_error_t *error, runtimec_status_t status,
    const char *message) {
    runtimec_error_set(
        error, status, RUNTIMEC_ERROR_DOMAIN_HOST, 0,
        runtimec_string_view_from_c_string(message),
        RUNTIMEC_PROGRAM_COUNTER_NONE);
    return status;
}

runtimec_status_t runtimec_host_create(
    const runtimec_host_services_t *services, runtimec_host_t **output,
    runtimec_error_t *error) {
    runtimec_error_reset(error);
    if (output != NULL) {
        *output = NULL;
    }
    if (services == NULL || output == NULL || error == NULL ||
        !runtimec_host_services_is_valid(services)) {
        return runtimec_host_fail(
            error, RUNTIMEC_STATUS_INVALID,
            "invalid host create request");
    }
    runtimec_host_t *host = runtimec_allocator_allocate(
        &services->allocator, sizeof(*host), alignof(runtimec_host_t));
    if (host == NULL) {
        return runtimec_host_fail(
            error, RUNTIMEC_STATUS_OUT_OF_MEMORY,
            "host allocation failed");
    }
    host->services = *services;
    *output = host;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_host_destroy(
    runtimec_host_t *host, runtimec_error_t *error) {
    runtimec_error_reset(error);
    if (host == NULL || error == NULL ||
        !runtimec_host_services_is_valid(&host->services)) {
        return runtimec_host_fail(
            error, RUNTIMEC_STATUS_INVALID,
            "invalid host destroy request");
    }
    runtimec_allocator_t allocator = host->services.allocator;
    memset(host, 0, sizeof(*host));
    runtimec_status_t deallocation_status = runtimec_allocator_deallocate(
        &allocator, host, sizeof(*host), alignof(runtimec_host_t));
    if (deallocation_status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(
            error, RUNTIMEC_STATUS_ERROR,
            "host deallocation violated the allocator contract");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_host_get_services(
    const runtimec_host_t *host,
    const runtimec_host_services_t **output, runtimec_error_t *error) {
    runtimec_error_reset(error);
    if (output != NULL) {
        *output = NULL;
    }
    if (host == NULL || output == NULL || error == NULL ||
        !runtimec_host_services_is_valid(&host->services)) {
        return runtimec_host_fail(
            error, RUNTIMEC_STATUS_INVALID,
            "invalid host services request");
    }
    *output = &host->services;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_host_resolve_static_xvalue_address(
    const runtimec_host_t *host, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key,
    runtimec_xvalue_address_t *output_xvalue_address,
    runtimec_error_t *error) {
    runtimec_error_reset(error);
    if (output_xvalue_address != NULL) {
        *output_xvalue_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
    }
    if (host == NULL || snapshot == NULL || output_xvalue_address == NULL ||
        error == NULL || !runtimec_host_services_is_valid(&host->services) ||
        !runtimec_snapshot_is_valid(snapshot) ||
        !runtimec_key_view_is_valid(key) || key.size == 0U) {
        return runtimec_host_fail(
            error, RUNTIMEC_STATUS_INVALID,
            "invalid static XValue address request");
    }
    if (host->services.resolve_static_xvalue_address == NULL) {
        return runtimec_host_fail(
            error, RUNTIMEC_STATUS_NOT_IMPLEMENTED,
            "static XValue address resolution is not implemented");
    }

    runtimec_xvalue_address_t resolved_address =
        RUNTIMEC_XVALUE_ADDRESS_NONE;
    runtimec_status_t status =
        host->services.resolve_static_xvalue_address(
            host->services.context, snapshot, key, &resolved_address, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        *output_xvalue_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
        return status;
    }
    if (resolved_address == RUNTIMEC_XVALUE_ADDRESS_NONE) {
        return runtimec_host_fail(
            error, RUNTIMEC_STATUS_ERROR,
            "host returned an invalid static XValue address");
    }

    *output_xvalue_address = resolved_address;
    return RUNTIMEC_STATUS_SUCCESS;
}
