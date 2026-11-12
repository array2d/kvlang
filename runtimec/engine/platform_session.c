
#include "engine/platform_session.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "contract/kvspace_interface.h"
#include "contract/kvspace_access.h"

#if defined(__APPLE__)
#define RUNTIMEC_KVSPACE_FRONTEND_SONAME "libkvspace.dylib"
#else
#define RUNTIMEC_KVSPACE_FRONTEND_SONAME "libkvspace.so"
#endif

#define RUNTIMEC_PLATFORM_ERROR_TEXT_CAPACITY 512U

struct runtimec_platform_session_t {
    runtimec_allocator_t allocator;
    void *library;
    bool library_borrowed;
    void *kvspace_handle;
    runtimec_kvspace_access_t *kvspace_access;
    bool kvspace_access_active;
    runtimec_session_t *session;
};

static void *runtimec_platform_allocate(void *context, size_t size,
                                        size_t alignment) {
    (void)context;
    (void)alignment;
    return malloc(size);
}

static void runtimec_platform_deallocate(void *context, void *allocation,
                                         size_t size, size_t alignment) {
    (void)context;
    (void)size;
    (void)alignment;
    free(allocation);
}

runtimec_allocator_t runtimec_default_allocator(void) {
    runtimec_allocator_t allocator;
    allocator.context = NULL;
    allocator.allocate = runtimec_platform_allocate;
    allocator.deallocate = runtimec_platform_deallocate;
    return allocator;
}

static runtimec_status_t runtimec_platform_fail(runtimec_error_t *error,
                                                runtimec_status_t status,
                                                int64_t code,
                                                const char *message) {
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_HOST, code,
                       runtimec_string_view_from_c_string(message),
                       RUNTIMEC_PROGRAM_COUNTER_NONE);
    return status;
}

static runtimec_status_t runtimec_platform_load_frontend(
    runtimec_platform_session_t *platform_session, runtimec_error_t *error) {
    static char error_text[RUNTIMEC_PLATFORM_ERROR_TEXT_CAPACITY];
    void *library = dlopen(RUNTIMEC_KVSPACE_FRONTEND_SONAME,
                           RTLD_NOW | RTLD_NOLOAD);
    if (library != NULL) {
        platform_session->library = library;
        platform_session->library_borrowed = true;
        return RUNTIMEC_STATUS_SUCCESS;
    }
    library = dlopen(RUNTIMEC_KVSPACE_FRONTEND_SONAME, RTLD_NOW | RTLD_LOCAL);
    if (library == NULL) {
        const char *detail = dlerror();
        int written = snprintf(error_text, sizeof(error_text),
                               "cannot load kvspace frontend '%s': %s",
                               RUNTIMEC_KVSPACE_FRONTEND_SONAME,
                               detail != NULL ? detail : "not found");
        if (written <= 0 || (size_t)written >= sizeof(error_text)) {
            return runtimec_platform_fail(
                error, RUNTIMEC_STATUS_UNSUPPORTED, 1,
                "cannot load kvspace frontend (diagnostic text too long)");
        }
        return runtimec_platform_fail(error, RUNTIMEC_STATUS_UNSUPPORTED, 2,
                                      error_text);
    }
    platform_session->library = library;
    platform_session->library_borrowed = false;
    return RUNTIMEC_STATUS_SUCCESS;
}

static void runtimec_platform_release_chain(
    runtimec_platform_session_t *platform_session) {
    runtimec_error_t ignored;
    runtimec_error_reset(&ignored);
    if (platform_session->session != NULL) {
        (void)runtimec_session_destroy(platform_session->session);
        platform_session->session = NULL;
    }
    if (platform_session->kvspace_access_active) {
        (void)runtimec_kvspace_access_destroy(platform_session->kvspace_access,
                                              &ignored);
        platform_session->kvspace_access = NULL;
        platform_session->kvspace_access_active = false;
    }
    if (platform_session->kvspace_handle != NULL) {
        
        kvspaceClose(platform_session->kvspace_handle);
    }
    if (platform_session->library != NULL) {
        if (!platform_session->library_borrowed) {
            (void)dlclose(platform_session->library);
        }
        platform_session->library = NULL;
    }
}

runtimec_status_t runtimec_platform_session_create(
    const char *data_source_name, uint64_t instance_identifier,
    const runtimec_session_options_t *options,
    runtimec_platform_session_t **output, runtimec_error_t *error) {
    runtimec_error_reset(error);
    if (output != NULL) {
        *output = NULL;
    }
    if (data_source_name == NULL || data_source_name[0] == '\0' ||
        options == NULL || output == NULL || error == NULL ||
        options->structure_size != sizeof(*options) ||
        !runtimec_allocator_is_valid(&options->allocator)) {
        return runtimec_platform_fail(error, RUNTIMEC_STATUS_INVALID, 3,
                                      "invalid platform session request");
    }

    runtimec_allocator_t allocator = options->allocator;
    runtimec_platform_session_t *platform_session =
        runtimec_allocator_allocate(&allocator, sizeof(*platform_session),
                                    _Alignof(runtimec_platform_session_t));
    if (platform_session == NULL) {
        return runtimec_platform_fail(error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 4,
                                      "platform session allocation failed");
    }
    memset(platform_session, 0, sizeof(*platform_session));
    platform_session->allocator = allocator;

    runtimec_status_t status =
        runtimec_platform_load_frontend(platform_session, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        (void)runtimec_platform_release_chain(platform_session);
        (void)runtimec_allocator_deallocate(
            &allocator, platform_session, sizeof(*platform_session),
            _Alignof(runtimec_platform_session_t));
        return status;
    }

    platform_session->kvspace_handle = kvspaceConnect(data_source_name);
    if (platform_session->kvspace_handle == NULL) {
        static char connect_text[RUNTIMEC_PLATFORM_ERROR_TEXT_CAPACITY];
        int written = snprintf(connect_text, sizeof(connect_text),
                               "cannot connect kvspace data source '%s'",
                               data_source_name);
        status = (written > 0 && (size_t)written < sizeof(connect_text))
                     ? runtimec_platform_fail(error, RUNTIMEC_STATUS_ERROR, 5,
                                              connect_text)
                     : runtimec_platform_fail(
                           error, RUNTIMEC_STATUS_ERROR, 5,
                           "cannot connect kvspace data source");
        (void)runtimec_platform_release_chain(platform_session);
        (void)runtimec_allocator_deallocate(
            &allocator, platform_session, sizeof(*platform_session),
            _Alignof(runtimec_platform_session_t));
        return status;
    }

    const runtimec_kvspace_access_request_t access_request = {
        .allocator = allocator,
        .borrowed_kvspace_handle = platform_session->kvspace_handle,
        .instance_identifier = instance_identifier,
    };
    status = runtimec_kvspace_access_create(
        &access_request, &platform_session->kvspace_access, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        (void)runtimec_platform_release_chain(platform_session);
        (void)runtimec_allocator_deallocate(
            &allocator, platform_session, sizeof(*platform_session),
            _Alignof(runtimec_platform_session_t));
        return status;
    }
    platform_session->kvspace_access_active = true;

    runtimec_session_t *session = NULL;
    status = runtimec_session_create(platform_session->kvspace_access, options,
                                     &session, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        (void)runtimec_platform_release_chain(platform_session);
        (void)runtimec_allocator_deallocate(
            &allocator, platform_session, sizeof(*platform_session),
            _Alignof(runtimec_platform_session_t));
        return status;
    }
    platform_session->session = session;
    *output = platform_session;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_platform_session_destroy(
    runtimec_platform_session_t *platform_session, runtimec_error_t *error) {
    runtimec_error_reset(error);
    if (platform_session == NULL || error == NULL) {
        return runtimec_platform_fail(error, RUNTIMEC_STATUS_INVALID, 6,
                                      "invalid platform session destroy request");
    }
    const runtimec_allocator_t allocator = platform_session->allocator;
    runtimec_platform_release_chain(platform_session);
    memset(platform_session, 0, sizeof(*platform_session));
    runtimec_status_t status = runtimec_allocator_deallocate(
        &allocator, platform_session, sizeof(*platform_session),
        _Alignof(runtimec_platform_session_t));
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_platform_fail(
            error, RUNTIMEC_STATUS_ERROR, 7,
            "platform session deallocation violated the allocator contract");
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_platform_session_get_session(
    const runtimec_platform_session_t *platform_session,
    runtimec_session_t **output_session, runtimec_error_t *error) {
    runtimec_error_reset(error);
    if (output_session != NULL) {
        *output_session = NULL;
    }
    if (platform_session == NULL || output_session == NULL || error == NULL) {
        return runtimec_platform_fail(error, RUNTIMEC_STATUS_INVALID, 8,
                                      "invalid platform session request");
    }
    if (platform_session->session == NULL) {
        return runtimec_platform_fail(error, RUNTIMEC_STATUS_NOT_IMPLEMENTED,
                                      9, "platform session has no engine session");
    }
    *output_session = platform_session->session;
    return RUNTIMEC_STATUS_SUCCESS;
}
