
#include "engine/session.h"

#include <stdio.h>
#include <string.h>

#include "binrwir_compiler/compiler.h"
#include "engine/compilation_repository.h"
#include "engine/frame_state.h"
#include "binrwir/registry.h"
#include "vmbin/handlers/container.h"
#include "vmbin/handlers/host.h"
#include "vmbin/handlers/langtype.h"
#include "vmbin/handlers/scalar.h"
#include "vmbin/handlers/scalar_generated.h"
#include "vmbin/handlers/scatter.h"
#include "vmbin/handlers/time.h"
#include "vmbin/handlers/string.h"
#ifdef RUNTIMEC_TEMP_OUTPUT
#include "vmbin/temp_output.h"
#endif
#include "vmkv/encoder.h"

#define RUNTIMEC_SESSION_COMPILER_REVISION 1U

#if defined(__x86_64__) && !defined(__APPLE__)
#define RUNTIMEC_SESSION_JIT_ARCHITECTURE BINRWIR_JIT_ARCHITECTURE_X86_64
#define RUNTIMEC_SESSION_JIT_TRIPLE "x86_64-unknown-linux-gnu"
#elif defined(__x86_64__) && defined(__APPLE__)
#define RUNTIMEC_SESSION_JIT_ARCHITECTURE BINRWIR_JIT_ARCHITECTURE_X86_64
#define RUNTIMEC_SESSION_JIT_TRIPLE "x86_64-apple-darwin"
#elif defined(__aarch64__) && defined(__APPLE__)
#define RUNTIMEC_SESSION_JIT_ARCHITECTURE BINRWIR_JIT_ARCHITECTURE_AARCH64
#define RUNTIMEC_SESSION_JIT_TRIPLE "aarch64-apple-darwin"
#else
#define RUNTIMEC_SESSION_JIT_ARCHITECTURE BINRWIR_JIT_ARCHITECTURE_AARCH64
#define RUNTIMEC_SESSION_JIT_TRIPLE "aarch64-unknown-linux-gnu"
#endif

struct runtimec_session_t {
    
    runtimec_kvspace_access_t *kvspace_access;
    runtimec_session_options_t options;
    vmkv_encoder_t *vmkv_encoder;
    binrwir_compiler_t *binrwir_compiler;
    runtimec_compilation_repository_t *compilation_repository;
    
    binrwir_registry_t builtin_registry;
    
    uint64_t checkpoint_sequence;
    
    void *kvspace_handle;
    
    runtimec_frame_state_table_t frames;
};

static runtimec_status_t runtimec_session_fail(runtimec_error_t *error,
                                               runtimec_status_t status,
                                               int64_t code,
                                               const char *message) {
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT, code,
                       runtimec_string_view_from_c_string(message),
                       RUNTIMEC_PROGRAM_COUNTER_NONE);
    return status;
}

static runtimec_status_t runtimec_session_forward(
    runtimec_error_t *error, runtimec_status_t status, const char *component) {
    static char message[160];
    const char *prefix = "dependency failed: ";
    size_t length = 0U;
    for (const char *cursor = prefix; *cursor != '\0'; cursor++) {
        message[length++] = *cursor;
    }
    for (const char *cursor = component; *cursor != '\0' &&
                                      length + 1U < sizeof(message);
         cursor++) {
        message[length++] = *cursor;
    }
    message[length] = '\0';
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT, 0,
                       runtimec_string_view_from_c_string(message),
                       RUNTIMEC_PROGRAM_COUNTER_NONE);
    return status;
}

runtimec_status_t runtimec_session_create(
    runtimec_kvspace_access_t *kvspace_access,
    const runtimec_session_options_t *options,
    runtimec_session_t **output_session, runtimec_error_t *error) {
    runtimec_error_reset(error);
    if (output_session != NULL) {
        *output_session = NULL;
    }
    if (kvspace_access == NULL || options == NULL ||
        options->structure_size != sizeof(*options) ||
        !runtimec_allocator_is_valid(&options->allocator) ||
        options->compilation_cache_capacity == 0U ||
        (options->enable_jit && options->machine_code_capacity == 0U) ||
        (!options->enable_jit && options->machine_code_capacity != 0U) ||
        output_session == NULL || error == NULL) {
        return runtimec_session_fail(error, RUNTIMEC_STATUS_INVALID, 1,
                                     "invalid session create request");
    }

    const runtimec_allocator_t allocator = options->allocator;
    runtimec_session_t *session = runtimec_allocator_allocate(
        &allocator, sizeof(*session), _Alignof(runtimec_session_t));
    if (session == NULL) {
        return runtimec_session_fail(error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 2,
                                     "session allocation failed");
    }
    memset(session, 0, sizeof(*session));
    session->kvspace_access = kvspace_access;
    session->options = *options;
    
    session->kvspace_handle = options->kvspace_handle;
    
    runtimec_frame_state_table_initialize(&session->frames, allocator);

    runtimec_status_t status = binrwir_registry_initialize(
        &session->builtin_registry);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_session_forward(error, status, "builtin registry");
        goto fail;
    }
#ifdef RUNTIMEC_TEMP_OUTPUT
    {
        size_t temp_output_entry_count = 0U;
        const binrwir_registry_entry_t *temp_output_entries =
            runtimec_temp_output_entries(&temp_output_entry_count);
        status = binrwir_registry_append(
            &session->builtin_registry, "temp_output", temp_output_entries,
            temp_output_entry_count, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            status = runtimec_session_forward(error, status, "builtin registry");
            goto fail;
        }
    }
#endif
    
    {
        size_t host_entry_count = 0U;
        const binrwir_registry_entry_t *host_entries =
            runtimec_host_handler_entries(&host_entry_count);

        status = binrwir_registry_append(&session->builtin_registry, "host",
                                         host_entries, host_entry_count, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            status = runtimec_session_forward(error, status, "builtin registry");
            goto fail;
        }
    }
    
    {
        size_t container_entry_count = 0U;
        const binrwir_registry_entry_t *container_entries =
            runtimec_container_handler_entries(&container_entry_count);

        status = binrwir_registry_append(
            &session->builtin_registry, "container", container_entries,
            container_entry_count, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            status = runtimec_session_forward(error, status, "builtin registry");
            goto fail;
        }
    }
    
    {
        size_t scalar_entry_count = 0U;
        const binrwir_registry_entry_t *scalar_entries =
            runtimec_scalar_handler_entries(&scalar_entry_count);

        status = binrwir_registry_append(&session->builtin_registry, "scalar",
                                         scalar_entries, scalar_entry_count,
                                         error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            status = runtimec_session_forward(error, status, "builtin registry");
            goto fail;
        }
    }
    
    {
        status = binrwir_registry_append(
            &session->builtin_registry, "scalar_generated",
            scalar_generated_entries, scalar_generated_entry_count, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            status = runtimec_session_forward(error, status, "builtin registry");
            goto fail;
        }
    }
    
    {
        size_t string_entry_count = 0U;
        const binrwir_registry_entry_t *string_entries =
            runtimec_string_handler_entries(&string_entry_count);

        status = binrwir_registry_append(&session->builtin_registry, "string",
                                         string_entries, string_entry_count,
                                         error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            status = runtimec_session_forward(error, status, "builtin registry");
            goto fail;
        }
    }
    
    {
        size_t langtype_entry_count = 0U;
        const binrwir_registry_entry_t *langtype_entries =
            runtimec_langtype_handler_entries(&langtype_entry_count);

        status = binrwir_registry_append(&session->builtin_registry, "langtype",
                                         langtype_entries, langtype_entry_count,
                                         error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            status = runtimec_session_forward(error, status, "builtin registry");
            goto fail;
        }
    }
    
    {
        size_t scatter_entry_count = 0U;
        const binrwir_registry_entry_t *scatter_entries =
            runtimec_scatter_handler_entries(&scatter_entry_count);

        status = binrwir_registry_append(&session->builtin_registry, "scatter",
                                         scatter_entries, scatter_entry_count,
                                         error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            status = runtimec_session_forward(error, status, "builtin registry");
            goto fail;
        }
    }

    {
        size_t time_entry_count = 0U;
        const binrwir_registry_entry_t *time_entries =
            runtimec_time_handler_entries(&time_entry_count);

        status = binrwir_registry_append(&session->builtin_registry, "time",
                                         time_entries, time_entry_count, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            status = runtimec_session_forward(error, status, "builtin registry");
            goto fail;
        }
    }

    if (options->extension_modules != NULL && options->extension_count != 0U) {
        
        const runtimec_extension_module_view_t *modules =
            (const runtimec_extension_module_view_t *)options->extension_modules;
        for (size_t module = 0U; module < options->extension_count; ++module) {
            const runtimec_extension_module_view_t *entry_module = &modules[module];
            for (size_t item = 0U; item < entry_module->entry_count; ++item) {
                const runtimec_extension_entry_view_t *source =
                    &entry_module->entries[item];
                const binrwir_registry_entry_t entry = {
                    .operation_identifier = source->operation_identifier,
                    .flags = source->flags,
                    .handler = (binrwir_registry_handler_t)source->handler,
                };
                status = binrwir_registry_append(
                    &session->builtin_registry, entry_module->module_name,
                    &entry, 1U, error);
                if (status != RUNTIMEC_STATUS_SUCCESS) {
                    
                    static char extension_error[256];
                    (void)snprintf(
                        extension_error, sizeof(extension_error),
                        "extension module \"%s\" registers opid 0x%04X which is "
                        "already taken",
                        entry_module->module_name,
                        (unsigned)entry.operation_identifier);
                    runtimec_error_reset(error);
                    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                                       1,
                                       runtimec_string_view_from_c_string(
                                           extension_error),
                                       RUNTIMEC_PROGRAM_COUNTER_NONE);
                    goto fail;
                }
            }
        }
    }

    status = vmkv_encoder_create(allocator, &session->vmkv_encoder, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_session_forward(error, status, "vmkv encoder");
        goto fail;
    }

    binrwir_compiler_configuration_t configuration;
    memset(&configuration, 0, sizeof(configuration));
    configuration.structure_size = sizeof(configuration);
    configuration.compiler_revision = RUNTIMEC_SESSION_COMPILER_REVISION;
    configuration.enable_jit = options->enable_jit;
    if (options->enable_jit) {
        configuration.jit_machine_code_capacity =
            options->machine_code_capacity;
        configuration.jit_target.architecture =
            RUNTIMEC_SESSION_JIT_ARCHITECTURE;
        configuration.jit_target.triple = runtimec_string_view_from_c_string(
            RUNTIMEC_SESSION_JIT_TRIPLE);
        configuration.jit_target.feature_mask = 0U;
    }
    status = binrwir_compiler_create(&allocator, &configuration,
                                     &session->binrwir_compiler, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_session_forward(error, status, "binrwir compiler");
        goto fail;
    }

    status = runtimec_compilation_repository_create(
        allocator, options->compilation_cache_capacity,
        &session->compilation_repository, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_session_forward(error, status,
                                          "compilation repository");
        goto fail;
    }

    *output_session = session;
    return RUNTIMEC_STATUS_SUCCESS;

fail:
    
    if (session->compilation_repository != NULL) {
        (void)runtimec_compilation_repository_destroy(
            session->compilation_repository);
    }
    if (session->binrwir_compiler != NULL) {
        runtimec_error_t ignored;
        runtimec_error_reset(&ignored);
        (void)binrwir_compiler_destroy(session->binrwir_compiler, &ignored);
    }
    if (session->vmkv_encoder != NULL) {
        (void)vmkv_encoder_destroy(session->vmkv_encoder);
    }
    (void)runtimec_allocator_deallocate(&allocator, session, sizeof(*session),
                                        _Alignof(runtimec_session_t));
    *output_session = NULL;
    return status;
}

runtimec_status_t runtimec_session_destroy(runtimec_session_t *session) {
    if (session == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    const runtimec_allocator_t allocator = session->options.allocator;
    runtimec_status_t status = RUNTIMEC_STATUS_SUCCESS;
    
    runtimec_frame_state_table_release(&session->frames);
    if (session->compilation_repository != NULL) {
        status = runtimec_compilation_repository_destroy(
            session->compilation_repository);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
    }
    if (session->binrwir_compiler != NULL) {
        runtimec_error_t ignored;
        runtimec_error_reset(&ignored);
        status = binrwir_compiler_destroy(session->binrwir_compiler, &ignored);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
    }
    if (session->vmkv_encoder != NULL) {
        status = vmkv_encoder_destroy(session->vmkv_encoder);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
    }
    memset(session, 0, sizeof(*session));
    return runtimec_allocator_deallocate(&allocator, session,
                                         sizeof(*session),
                                         _Alignof(runtimec_session_t));
}

runtimec_status_t runtimec_session_get_kvspace_access(
    runtimec_session_t *session,
    runtimec_kvspace_access_t **output_kvspace_access) {
    if (output_kvspace_access != NULL) {
        *output_kvspace_access = NULL;
    }
    if (session == NULL || output_kvspace_access == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    *output_kvspace_access = session->kvspace_access;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_session_get_kvspace_handle(
    runtimec_session_t *session, void **output_kvspace_handle) {
    if (output_kvspace_handle != NULL) {
        *output_kvspace_handle = NULL;
    }
    if (session == NULL || output_kvspace_handle == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    
    *output_kvspace_handle = session->kvspace_handle;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_session_clear_derived_state(
    runtimec_session_t *session) {
    if (session == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (session->compilation_repository == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    
    return runtimec_compilation_repository_clear(
        session->compilation_repository);
}

runtimec_status_t runtimec_session_get_runtime_components(
    runtimec_session_t *session,
    runtimec_session_runtime_components_t *output_components) {
    if (output_components != NULL) {
        *output_components = (runtimec_session_runtime_components_t){0};
    }
    if (session == NULL || output_components == NULL ||
        session->vmkv_encoder == NULL || session->binrwir_compiler == NULL ||
        session->compilation_repository == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    output_components->vmkv_encoder = session->vmkv_encoder;
    output_components->binrwir_compiler = session->binrwir_compiler;
    output_components->compilation_repository =
        session->compilation_repository;
    output_components->builtin_registry = &session->builtin_registry;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_session_get_frame_state(
    runtimec_session_t *session,
    runtimec_frame_state_table_t **output_frame_state) {
    if (output_frame_state != NULL) {
        *output_frame_state = NULL;
    }
    if (session == NULL || output_frame_state == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    *output_frame_state = &session->frames;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_session_take_checkpoint_sequence(
    runtimec_session_t *session, uint64_t *output_sequence) {
    if (output_sequence != NULL) {
        *output_sequence = 0U;
    }
    if (session == NULL || output_sequence == NULL ||
        session->checkpoint_sequence == UINT64_MAX) {
        return RUNTIMEC_STATUS_INVALID;
    }
    session->checkpoint_sequence += 1U;
    *output_sequence = session->checkpoint_sequence;
    return RUNTIMEC_STATUS_SUCCESS;
}
