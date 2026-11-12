#ifndef BINRWIR_BINVTHREAD_FRAME_H
#define BINRWIR_BINVTHREAD_FRAME_H

#include <stdint.h>

#include "contract/kvspace_access.h"
#include "contract/snapshot.h"
#include "contract/status.h"
#include "contract/view.h"
#include "binrwir/frame_bindings.h"
#include "binrwir/binfunc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum binrwir_binvthread_frame_flags {
    BINRWIR_BINVTHREAD_FRAME_FLAG_NONE = 0,
    BINRWIR_BINVTHREAD_FRAME_FLAG_INITIALIZED = 1U << 0,
    BINRWIR_BINVTHREAD_FRAME_FLAG_OBSERVABLE = 1U << 1,
    BINRWIR_BINVTHREAD_FRAME_FLAG_RESUMED = 1U << 2
} binrwir_binvthread_frame_flags_t;

typedef struct binrwir_binvthread_frame {
    const binrwir_binfunc_t *program;
    struct binrwir_binvthread_frame *parent;
    
    binrwir_frame_bindings_t *bindings;
    
    void *kvspace_handle;
    
    runtimec_key_view_t frame_path;
    
    int32_t stack_identifier;
    
    runtimec_string_view_t vthread_identifier;
    
    runtimec_kvspace_access_t *kvspace_access;
    
    runtimec_snapshot_t snapshot;
    uint64_t snapshot_generation;
    uint64_t program_generation;
    uint32_t logical_program_counter;
    uint32_t flags;
} binrwir_binvthread_frame_t;

#define BINRWIR_BINVTHREAD_FRAME_ALIGNMENT \
    ((uint32_t)_Alignof(binrwir_binvthread_frame_t))

runtimec_status_t binrwir_binvthread_frame_initialize(
    binrwir_binvthread_frame_t *frame, const binrwir_binfunc_t *program,
    binrwir_binvthread_frame_t *parent,
    uint64_t snapshot_generation, uint64_t program_generation,
    uint32_t logical_program_counter);
runtimec_status_t binrwir_binvthread_frame_reset(
    binrwir_binvthread_frame_t *frame);

runtimec_status_t binrwir_binvthread_frame_destroy(
    binrwir_binvthread_frame_t *frame);

#ifdef __cplusplus
}
#endif

#endif
