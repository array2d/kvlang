/* host 能力表的纯生命周期与形状校验。 */

#include "contract/host_services.h"

void runtimec_host_value_header_reset(runtimec_host_value_header_t *header) {
    if (header != NULL) {
        header->value_size = 0U;
        header->value_revision = 0U;
        header->flags = 0U;
        header->reserved = 0U;
    }
}

void runtimec_handoff_request_reset(runtimec_handoff_request_t *request) {
    if (request != NULL) {
        request->operation = runtimec_string_view_make(NULL, 0U);
        runtimec_checkpoint_reset(&request->checkpoint);
    }
}

bool runtimec_handoff_request_is_valid(
    const runtimec_handoff_request_t *request) {
    return request != NULL && request->operation.size != 0U &&
           runtimec_string_view_is_valid(request->operation) &&
           runtimec_checkpoint_is_valid(&request->checkpoint);
}

void runtimec_host_services_initialize(runtimec_host_services_t *services) {
    if (services != NULL) {
        services->binary_interface_version =
            RUNTIMEC_HOST_SERVICES_BINARY_INTERFACE_VERSION;
        services->context = NULL;
        runtimec_allocator_reset(&services->allocator);
        services->read = NULL;
        services->read_header = NULL;
        services->write = NULL;
        services->write_partial = NULL;
        services->remove = NULL;
        services->list = NULL;
        services->watch = NULL;
        services->signal = NULL;
        services->source_revision = NULL;
        services->checkpoint = NULL;
        services->handoff = NULL;
    }
}

bool runtimec_host_services_is_valid(
    const runtimec_host_services_t *services) {
    return services != NULL &&
           services->binary_interface_version ==
               RUNTIMEC_HOST_SERVICES_BINARY_INTERFACE_VERSION &&
           runtimec_allocator_is_valid(&services->allocator);
}
