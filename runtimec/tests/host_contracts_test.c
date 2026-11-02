/* Verify that host exposes stable XValue bases without deriving V.body. */

#include <stdint.h>
#include <stdlib.h>

#include "host/host.h"

#define RUNTIMEC_EXPECT(condition)                                                \
    do {                                                                          \
        if (!(condition)) {                                                        \
            ++failures;                                                            \
        }                                                                          \
    } while (0)

typedef struct host_resolution_context {
    runtimec_xvalue_address_t xvalue_address;
    runtimec_status_t status;
} host_resolution_context_t;

static int failures;

static void *test_allocate(void *context, size_t size, size_t alignment) {
    (void)context;
    (void)alignment;
    return malloc(size);
}

static void test_deallocate(
    void *context, void *allocation, size_t size, size_t alignment) {
    (void)context;
    (void)size;
    (void)alignment;
    free(allocation);
}

static runtimec_status_t resolve_static_xvalue_address(
    void *context, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key,
    runtimec_xvalue_address_t *output_xvalue_address,
    runtimec_error_t *error) {
    host_resolution_context_t *resolution_context = context;

    (void)snapshot;
    (void)key;
    (void)error;
    if (resolution_context->status != RUNTIMEC_STATUS_SUCCESS) {
        *output_xvalue_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
        return resolution_context->status;
    }
    *output_xvalue_address = resolution_context->xvalue_address;
    return RUNTIMEC_STATUS_SUCCESS;
}

int main(void) {
    host_resolution_context_t resolution_context = {
        .xvalue_address = UINT64_C(0x1000),
        .status = RUNTIMEC_STATUS_SUCCESS,
    };
    runtimec_host_services_t services;
    runtimec_host_t *host = NULL;
    runtimec_snapshot_t snapshot;
    runtimec_error_t error;
    runtimec_xvalue_address_t output_address = UINT64_MAX;
    const runtimec_key_view_t value_key =
        RUNTIMEC_KEY_VIEW_LITERAL("/value");
    const runtimec_key_view_t dynamic_key =
        RUNTIMEC_KEY_VIEW_LITERAL("/dynamic");

    runtimec_host_services_initialize(&services);
    services.context = &resolution_context;
    services.allocator = (runtimec_allocator_t){
        .context = NULL,
        .allocate = test_allocate,
        .deallocate = test_deallocate,
    };
    services.resolve_static_xvalue_address = resolve_static_xvalue_address;
    runtimec_snapshot_reset(&snapshot);
    runtimec_error_reset(&error);

    RUNTIMEC_EXPECT(runtimec_host_create(&services, &host, &error) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(host != NULL);

    RUNTIMEC_EXPECT(runtimec_host_resolve_static_xvalue_address(
        host, &snapshot, value_key, &output_address, &error) ==
        RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(output_address == UINT64_C(0x1000));

    resolution_context.xvalue_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
    output_address = UINT64_MAX;
    RUNTIMEC_EXPECT(runtimec_host_resolve_static_xvalue_address(
        host, &snapshot, value_key, &output_address, &error) ==
        RUNTIMEC_STATUS_ERROR);
    RUNTIMEC_EXPECT(output_address == RUNTIMEC_XVALUE_ADDRESS_NONE);

    resolution_context.status = RUNTIMEC_STATUS_UNSUPPORTED;
    output_address = UINT64_MAX;
    RUNTIMEC_EXPECT(runtimec_host_resolve_static_xvalue_address(
        host, &snapshot, dynamic_key, &output_address, &error) ==
        RUNTIMEC_STATUS_UNSUPPORTED);
    RUNTIMEC_EXPECT(output_address == RUNTIMEC_XVALUE_ADDRESS_NONE);

    RUNTIMEC_EXPECT(runtimec_host_destroy(host, &error) ==
                    RUNTIMEC_STATUS_SUCCESS);
    return failures == 0 ? 0 : 1;
}
