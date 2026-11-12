#ifndef KVLANG_RUNTIMEC_H
#define KVLANG_RUNTIMEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(KVLANG_RUNTIMEC_BUILDING_LIBRARY)
#    define KVLANG_RUNTIMEC_EXPORT __declspec(dllexport)
#  else
#    define KVLANG_RUNTIMEC_EXPORT __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define KVLANG_RUNTIMEC_EXPORT __attribute__((visibility("default")))
#else
#  define KVLANG_RUNTIMEC_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum runtimec_status {
    RUNTIMEC_STATUS_SUCCESS = 0,
    RUNTIMEC_STATUS_YIELD = 1,
    RUNTIMEC_STATUS_WAIT = 2,
    RUNTIMEC_STATUS_UNSUPPORTED = 3,
    RUNTIMEC_STATUS_NOT_IMPLEMENTED = 4,
    RUNTIMEC_STATUS_INVALID = 5,
    RUNTIMEC_STATUS_OUT_OF_MEMORY = 6,
    RUNTIMEC_STATUS_ERROR = 7,
    RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY = 8
} runtimec_status_t;

typedef struct kvlang_runtime kvlang_runtime_t;

typedef struct kvlang_runtime_text_buffer {
    char *data;              
    size_t capacity;         
    size_t length;           
    size_t required_length;  
} kvlang_runtime_text_buffer_t;

typedef struct kvlang_runtime_error {
    runtimec_status_t status;            
    runtimec_status_t operation_status;  
    int64_t code;
    kvlang_runtime_text_buffer_t message;
} kvlang_runtime_error_t;

typedef runtimec_status_t (*kvlang_runtime_extension_handler_t)(
    void *frame, const void *instruction, void *output_exit);

typedef struct kvlang_runtime_extension_entry {
    uint32_t structure_size;
    uint32_t operation_identifier;
    uint32_t flags; 
    kvlang_runtime_extension_handler_t handler;
} kvlang_runtime_extension_entry_t;

typedef struct kvlang_runtime_extension_module {
    const char *module_name;
    const kvlang_runtime_extension_entry_t *entries;
    size_t entry_count;
} kvlang_runtime_extension_module_t;

typedef struct kvlang_runtime_create_request {
    
    size_t structure_size;
    const char *data_source_name;
    
    uint64_t instance_identifier;
    
    const kvlang_runtime_extension_module_t *extensions;
    size_t extension_count;
} kvlang_runtime_create_request_t;

static inline size_t kvlang_runtime_create_request_size(void) {
    return sizeof(kvlang_runtime_create_request_t);
}
static inline size_t kvlang_runtime_extension_entry_size(void) {
    return sizeof(kvlang_runtime_extension_entry_t);
}

typedef enum kvlang_runtime_entry_kind {
    KVLANG_RUNTIME_ENTRY_FUNCTION = 0,
    KVLANG_RUNTIME_ENTRY_PROGRAM_COUNTER = 1,
    KVLANG_RUNTIME_ENTRY_VTHREAD = 2,
    KVLANG_RUNTIME_ENTRY_BOOTSTRAP = 3
} kvlang_runtime_entry_kind_t;

typedef struct kvlang_runtime_execute_request {
    kvlang_runtime_entry_kind_t entry_kind;
    
    const char *entry_name;
    const char *const *arguments;
    size_t argument_count;
} kvlang_runtime_execute_request_t;

typedef struct kvlang_runtime_execute_result {
    kvlang_runtime_text_buffer_t value;
    kvlang_runtime_text_buffer_t program_counter;
} kvlang_runtime_execute_result_t;

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_read_argument_text(
    const void *frame, const void *instruction, size_t index,
    kvlang_runtime_text_buffer_t *output, kvlang_runtime_error_t *error);

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_read_argument_integer(
    const void *frame, const void *instruction, size_t index, int64_t *output,
    kvlang_runtime_error_t *error);

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_read_argument_wire(
    const void *frame, const void *instruction, size_t index,
    const uint8_t **output_data, size_t *output_size,
    kvlang_runtime_error_t *error);

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_read_argument_langtype(
    const void *frame, const void *instruction, size_t index,
    const char **output_data, size_t *output_size,
    kvlang_runtime_error_t *error);

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_write_argument_wire(
    void *frame, const void *instruction, size_t index, const uint8_t *data,
    size_t size, kvlang_runtime_error_t *error);

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_write_argument_text(
    void *frame, const void *instruction, size_t index, const char *data,
    size_t size, kvlang_runtime_error_t *error);

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_write_argument_integer(
    void *frame, const void *instruction, size_t index, int64_t value,
    kvlang_runtime_error_t *error);

typedef enum kvlang_runtime_exit_kind {
    KVLANG_RUNTIME_EXIT_NONE = 0,
    KVLANG_RUNTIME_EXIT_ERROR = 1,
    KVLANG_RUNTIME_EXIT_YIELD = 2
} kvlang_runtime_exit_kind_t;

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_set_exit(
    void *output_exit, kvlang_runtime_exit_kind_t kind, int64_t detail_code,
    const char *message, kvlang_runtime_error_t *error);

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_make_pointer(
    const char *target_key, size_t target_size, const char *target_langtype,
    kvlang_runtime_text_buffer_t *output, kvlang_runtime_error_t *error);

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_dereference_pointer(
    const uint8_t *wire, size_t size, kvlang_runtime_text_buffer_t *output_key,
    kvlang_runtime_text_buffer_t *output_langtype,
    kvlang_runtime_error_t *error);

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_create(
    const kvlang_runtime_create_request_t *request,
    kvlang_runtime_t **output, kvlang_runtime_error_t *error);

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_destroy(
    kvlang_runtime_t *runtime, kvlang_runtime_error_t *error);

KVLANG_RUNTIMEC_EXPORT runtimec_status_t kvlang_runtime_execute(
    kvlang_runtime_t *runtime,
    const kvlang_runtime_execute_request_t *request,
    kvlang_runtime_execute_result_t *output,
    kvlang_runtime_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
