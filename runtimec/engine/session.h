#ifndef RUNTIMEC_ENGINE_SESSION_H
#define RUNTIMEC_ENGINE_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "contract/allocator.h"
#include "binrwir/registry.h"
#include "contract/kvspace_access.h"
#include "contract/status.h"
#include "engine/frame_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct runtimec_session_t runtimec_session_t;

typedef struct runtimec_extension_entry_view {
    uint32_t structure_size;
    uint32_t operation_identifier;
    uint32_t flags;
    
    binrwir_registry_handler_t handler;
} runtimec_extension_entry_view_t;

typedef struct runtimec_extension_module_view {
    const char *module_name;
    const runtimec_extension_entry_view_t *entries;
    size_t entry_count;
} runtimec_extension_module_view_t;

typedef struct runtimec_session_options_t {
    uint32_t structure_size;
    runtimec_allocator_t allocator;
    size_t compilation_cache_capacity;
    size_t machine_code_capacity;
    bool enable_jit;
    
    void *kvspace_handle;
    
    const void *extension_modules;
    size_t extension_count;
} runtimec_session_options_t;
runtimec_status_t runtimec_session_create(
    runtimec_kvspace_access_t *kvspace_access,
    const runtimec_session_options_t *options,
    runtimec_session_t **output_session,
    runtimec_error_t *error);
runtimec_status_t runtimec_session_destroy(runtimec_session_t *session);
runtimec_status_t runtimec_session_get_kvspace_access(
    runtimec_session_t *session,
    runtimec_kvspace_access_t **output_kvspace_access);

runtimec_status_t runtimec_session_get_kvspace_handle(
    runtimec_session_t *session, void **output_kvspace_handle);
runtimec_status_t runtimec_session_clear_derived_state(
    runtimec_session_t *session);

typedef struct vmkv_encoder_t vmkv_encoder_t;
typedef struct binrwir_compiler_t binrwir_compiler_t;
typedef struct runtimec_compilation_repository_t
    runtimec_compilation_repository_t;
typedef struct binrwir_registry binrwir_registry_t;

typedef struct runtimec_session_runtime_components_t {
    vmkv_encoder_t *vmkv_encoder;
    binrwir_compiler_t *binrwir_compiler;
    runtimec_compilation_repository_t *compilation_repository;
    const binrwir_registry_t *builtin_registry;
} runtimec_session_runtime_components_t;

runtimec_status_t runtimec_session_get_runtime_components(
    runtimec_session_t *session,
    runtimec_session_runtime_components_t *output_components);

runtimec_status_t runtimec_session_get_frame_state(
    runtimec_session_t *session,
    runtimec_frame_state_table_t **output_frame_state);

runtimec_status_t runtimec_session_take_checkpoint_sequence(
    runtimec_session_t *session, uint64_t *output_sequence);

#ifdef __cplusplus
}
#endif

#endif
