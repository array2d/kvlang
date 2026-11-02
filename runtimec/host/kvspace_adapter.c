/* kvspace adapter（包括静态 V.body 地址解析）的失败封闭骨架。 */

#include "host/kvspace_adapter.h"

static runtimec_status_t runtimec_kvspace_adapter_fail(
    runtimec_error_t *error, runtimec_status_t status,
    const char *message) {
    runtimec_error_set(
        error, status, RUNTIMEC_ERROR_DOMAIN_STORAGE, 0,
        runtimec_string_view_from_c_string(message),
        runtimec_string_view_make(NULL, 0U));
    return status;
}

runtimec_status_t runtimec_kvspace_adapter_create_services(
    const runtimec_kvspace_adapter_request_t *request,
    runtimec_host_services_t *output, runtimec_error_t *error) {
    runtimec_error_reset(error);
    runtimec_host_services_initialize(output);
    if (request == NULL || output == NULL || error == NULL ||
        request->borrowed_kvspace_handle == NULL ||
        !runtimec_allocator_is_valid(&request->allocator)) {
        return runtimec_kvspace_adapter_fail(
            error, RUNTIMEC_STATUS_INVALID,
            "invalid kvspace adapter request");
    }
    return runtimec_kvspace_adapter_fail(
        error, RUNTIMEC_STATUS_NOT_IMPLEMENTED,
        "kvspace adapter is not implemented");
}
