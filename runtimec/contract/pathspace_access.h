#ifndef RUNTIMEC_CONTRACT_PATHSPACE_ACCESS_H
#define RUNTIMEC_CONTRACT_PATHSPACE_ACCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <pathspace/pathspace_xvalue.h>

#include "contract/allocator.h"
#include "contract/checkpoint.h"
#include "contract/error.h"
#include "contract/status.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t runtimec_xvalue_address_t;
typedef uint64_t runtimec_value_body_address_t;

#define RUNTIMEC_XVALUE_ADDRESS_NONE UINT64_C(0)
#define RUNTIMEC_VALUE_BODY_ADDRESS_NONE UINT64_C(0)

#define RUNTIMEC_XVALUE_BODY_OFFSET_BYTES PATHSPACE_XVALUE_HEAD_LEN

#define RUNTIMEC_PATHSPACE_KEY_CAPACITY 1024U

#ifdef __cplusplus
static_assert(RUNTIMEC_XVALUE_BODY_OFFSET_BYTES == 16U,
              "V.body must start 16 bytes after the XValue base");
#else
_Static_assert(RUNTIMEC_XVALUE_BODY_OFFSET_BYTES == 16U,
               "V.body must start 16 bytes after the XValue base");
#endif

typedef struct runtimec_pathspace_value_header {
    uint64_t value_size;
    uint64_t value_revision;
    uint32_t flags;
    uint32_t reserved;
} runtimec_pathspace_value_header_t;

typedef struct runtimec_handoff_request {
    runtimec_string_view_t operation;
    runtimec_checkpoint_t checkpoint;
} runtimec_handoff_request_t;

typedef runtimec_status_t (*runtimec_pathspace_list_visitor_function_t)(
    void *context, runtimec_key_view_t key, runtimec_error_t *error);

typedef struct runtimec_pathspace_resolved_address {
    uint64_t address;
    uint64_t observed_size;
    uint64_t key_hash; 
    uint16_t key_size;
    char key[RUNTIMEC_PATHSPACE_KEY_CAPACITY];
} runtimec_pathspace_resolved_address_t;

typedef struct runtimec_pathspace_access {
    runtimec_allocator_t allocator;
    void *handle;
    
    uint64_t instance_identifier;
    
    struct runtimec_pathspace_resolved_address *resolved_addresses;
    size_t resolved_address_count;    
    size_t resolved_address_capacity; 
    uint64_t generation;
    bool generation_started;
    
    struct runtimec_key_directory *directory;
} runtimec_pathspace_access_t;

typedef struct runtimec_pathspace_access_request {
    runtimec_allocator_t allocator;
    void *borrowed_pathspace_handle;
    uint64_t instance_identifier;
} runtimec_pathspace_access_request_t;

runtimec_status_t runtimec_pathspace_access_create(
    const runtimec_pathspace_access_request_t *request,
    runtimec_pathspace_access_t **output, runtimec_error_t *error);
runtimec_status_t runtimec_pathspace_access_destroy(
    runtimec_pathspace_access_t *access, runtimec_error_t *error);

runtimec_status_t runtimec_pathspace_access_read(
    runtimec_pathspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_bytes_buffer_t *output,
    runtimec_error_t *error);
runtimec_status_t runtimec_pathspace_access_read_header(
    runtimec_pathspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_pathspace_value_header_t *output,
    runtimec_error_t *error);
runtimec_status_t runtimec_pathspace_access_write(
    runtimec_pathspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_bytes_view_t value,
    runtimec_error_t *error);
runtimec_status_t runtimec_pathspace_access_write_partial(
    runtimec_pathspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, uint64_t offset, runtimec_bytes_view_t value,
    runtimec_error_t *error);
runtimec_status_t runtimec_pathspace_access_remove(
    runtimec_pathspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_error_t *error);
runtimec_status_t runtimec_pathspace_access_list(
    runtimec_pathspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t prefix,
    runtimec_pathspace_list_visitor_function_t visitor, void *visitor_context,
    runtimec_error_t *error);
runtimec_status_t runtimec_pathspace_access_signal(
    runtimec_pathspace_access_t *access, runtimec_key_view_t key,
    runtimec_error_t *error);
runtimec_status_t runtimec_pathspace_access_source_revision(
    runtimec_pathspace_access_t *access, runtimec_key_view_t function_key,
    runtimec_source_revision_t *revision, runtimec_error_t *error);
runtimec_status_t runtimec_pathspace_access_resolve_static_xvalue_address(
    runtimec_pathspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key,
    runtimec_xvalue_address_t *output_xvalue_address,
    runtimec_error_t *error);
runtimec_status_t runtimec_pathspace_access_resolve_frame_xvalue_address(
    runtimec_pathspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key,
    runtimec_xvalue_address_t *output_xvalue_address,
    uint64_t *output_generation, runtimec_error_t *error);
runtimec_status_t runtimec_pathspace_access_ensure_frame(
    runtimec_pathspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t frame_path, runtimec_string_view_t function_key,
    runtimec_error_t *error);
runtimec_status_t runtimec_pathspace_access_ensure_zero_value(
    runtimec_pathspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t key, runtimec_string_view_t langtype,
    runtimec_error_t *error);

bool runtimec_pathspace_access_langtype_has_fixed_zero_width(
    runtimec_string_view_t langtype);

bool runtimec_pathspace_access_langtype_zero_width(
    runtimec_string_view_t langtype, uint64_t *output_width);

runtimec_status_t runtimec_pathspace_access_frame_generation(
    runtimec_pathspace_access_t *access, const runtimec_snapshot_t *snapshot,
    runtimec_key_view_t frame_path, uint64_t *output_generation,
    runtimec_error_t *error);
runtimec_status_t runtimec_pathspace_access_checkpoint(
    runtimec_pathspace_access_t *access, const runtimec_checkpoint_t *checkpoint,
    runtimec_error_t *error);

runtimec_status_t runtimec_pathspace_access_write_program_counter(
    runtimec_pathspace_access_t *access, runtimec_string_view_t vthread,
    runtimec_program_counter_t counter, runtimec_error_t *error);

runtimec_status_t runtimec_pathspace_access_read_program_counter(
    runtimec_pathspace_access_t *access, runtimec_string_view_t vthread,
    runtimec_program_counter_t *output_counter, bool *output_present,
    runtimec_error_t *error);

runtimec_status_t runtimec_pathspace_access_read_frame_library(
    runtimec_pathspace_access_t *access, runtimec_key_view_t frame_path,
    char *output_text, size_t output_capacity, runtimec_error_t *error);
runtimec_status_t runtimec_pathspace_access_handoff(
    runtimec_pathspace_access_t *access,
    const runtimec_handoff_request_t *request, runtimec_error_t *error);

bool runtimec_pathspace_access_key_is_static(runtimec_key_view_t key);

runtimec_value_body_address_t runtimec_xvalue_body_address(
    runtimec_xvalue_address_t xvalue_address);
runtimec_xvalue_address_t runtimec_value_body_xvalue_address(
    runtimec_value_body_address_t body_address);

uint32_t runtimec_value_body_length(runtimec_value_body_address_t body_address);

runtimec_string_view_t runtimec_xvalue_body_from_wire(
    runtimec_bytes_view_t wire);

void runtimec_pathspace_value_header_reset(runtimec_pathspace_value_header_t *header);
void runtimec_handoff_request_reset(runtimec_handoff_request_t *request);
bool runtimec_handoff_request_is_valid(const runtimec_handoff_request_t *request);

#ifdef __cplusplus
}
#endif

#endif
