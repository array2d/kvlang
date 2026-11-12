#ifndef BINRWIR_FRAME_BINDINGS_H
#define BINRWIR_FRAME_BINDINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "contract/allocator.h"
#include "contract/kvspace_access.h"
#include "contract/status.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BINRWIR_FRAME_BINDING_SLOT_NONE UINT16_MAX
#define BINRWIR_FRAME_BINDING_GENERATION_NONE UINT64_C(0)

typedef enum binrwir_binding_kind {
    BINRWIR_BINDING_INVALID = 0,
    BINRWIR_BINDING_ADDRESS = 1, 
    BINRWIR_BINDING_INLINE = 2   
} binrwir_binding_kind_t;

typedef struct binrwir_frame_binding {
    runtimec_key_view_t slot_key;
    runtimec_key_view_t caller_key;
    runtimec_xvalue_address_t xvalue_address;
    
    const uint8_t *bytes;
    size_t length;
    uint64_t generation;
    uint8_t kind; 
    bool valid;
} binrwir_frame_binding_t;

typedef struct binrwir_frame_bindings binrwir_frame_bindings_t;

runtimec_status_t binrwir_frame_bindings_create(
    const runtimec_allocator_t *allocator, size_t capacity,
    binrwir_frame_bindings_t **output_bindings);

runtimec_status_t binrwir_frame_bindings_destroy(
    binrwir_frame_bindings_t *bindings);

runtimec_status_t binrwir_frame_bindings_reset(
    binrwir_frame_bindings_t *bindings);
size_t binrwir_frame_bindings_get_capacity(
    const binrwir_frame_bindings_t *bindings);

runtimec_status_t binrwir_frame_bindings_bind(
    binrwir_frame_bindings_t *bindings, uint16_t slot,
    runtimec_key_view_t slot_key, runtimec_key_view_t caller_key,
    runtimec_xvalue_address_t xvalue_address,
    uint64_t generation);

runtimec_status_t binrwir_frame_bindings_bind_inline(
    binrwir_frame_bindings_t *bindings, uint16_t slot,
    runtimec_key_view_t slot_key, runtimec_bytes_view_t bytes,
    uint64_t generation);

runtimec_status_t binrwir_frame_bindings_get(
    const binrwir_frame_bindings_t *bindings, uint16_t slot,
    uint64_t expected_generation,
    const binrwir_frame_binding_t **output_binding);

runtimec_status_t binrwir_frame_bindings_invalidate(
    binrwir_frame_bindings_t *bindings, uint16_t slot);

size_t binrwir_frame_local_key(runtimec_string_view_t frame_path,
                               runtimec_string_view_t name, char *output,
                               size_t capacity);

size_t binrwir_frame_parameter_key(runtimec_string_view_t frame_path,
                                   uint16_t parameter_ordinal,
                                   bool write_parameter, char *output,
                                   size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
