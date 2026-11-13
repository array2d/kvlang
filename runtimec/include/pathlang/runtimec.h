#ifndef PATHLANG_RUNTIMEC_H
#define PATHLANG_RUNTIMEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(PATHLANG_RUNTIMEC_BUILDING_LIBRARY)
#    define PATHLANG_RUNTIMEC_EXPORT __declspec(dllexport)
#  else
#    define PATHLANG_RUNTIMEC_EXPORT __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define PATHLANG_RUNTIMEC_EXPORT __attribute__((visibility("default")))
#else
#  define PATHLANG_RUNTIMEC_EXPORT
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

typedef struct pathlang_runtime pathlang_runtime_t;

typedef struct pathlang_runtime_text_buffer {
    char *data;              
    size_t capacity;         
    size_t length;           
    size_t required_length;  
} pathlang_runtime_text_buffer_t;

typedef struct pathlang_runtime_error {
    runtimec_status_t status;            
    runtimec_status_t operation_status;  
    int64_t code;
    pathlang_runtime_text_buffer_t message;
} pathlang_runtime_error_t;

typedef runtimec_status_t (*pathlang_runtime_extension_handler_t)(
    void *frame, const void *instruction, void *output_exit);

typedef struct pathlang_runtime_extension_entry {
    uint32_t structure_size;
    uint32_t operation_identifier;
    uint32_t flags; 
    pathlang_runtime_extension_handler_t handler;
} pathlang_runtime_extension_entry_t;

typedef struct pathlang_runtime_extension_module {
    const char *module_name;
    const pathlang_runtime_extension_entry_t *entries;
    size_t entry_count;
} pathlang_runtime_extension_module_t;

typedef struct pathlang_runtime_create_request {
    
    size_t structure_size;
    const char *data_source_name;
    
    uint64_t instance_identifier;
    
    const pathlang_runtime_extension_module_t *extensions;
    size_t extension_count;
} pathlang_runtime_create_request_t;

static inline size_t pathlang_runtime_create_request_size(void) {
    return sizeof(pathlang_runtime_create_request_t);
}
static inline size_t pathlang_runtime_extension_entry_size(void) {
    return sizeof(pathlang_runtime_extension_entry_t);
}

typedef enum pathlang_runtime_entry_kind {
    PATHLANG_RUNTIME_ENTRY_FUNCTION = 0,
    PATHLANG_RUNTIME_ENTRY_PROGRAM_COUNTER = 1,
    PATHLANG_RUNTIME_ENTRY_VTHREAD = 2,
    PATHLANG_RUNTIME_ENTRY_BOOTSTRAP = 3
} pathlang_runtime_entry_kind_t;

typedef struct pathlang_runtime_execute_request {
    pathlang_runtime_entry_kind_t entry_kind;
    
    const char *entry_name;
    const char *const *arguments;
    size_t argument_count;
} pathlang_runtime_execute_request_t;

typedef struct pathlang_runtime_execute_result {
    pathlang_runtime_text_buffer_t value;
    pathlang_runtime_text_buffer_t program_counter;
} pathlang_runtime_execute_result_t;

PATHLANG_RUNTIMEC_EXPORT runtimec_status_t pathlang_runtime_read_argument_text(
    const void *frame, const void *instruction, size_t index,
    pathlang_runtime_text_buffer_t *output, pathlang_runtime_error_t *error);

PATHLANG_RUNTIMEC_EXPORT runtimec_status_t pathlang_runtime_read_argument_integer(
    const void *frame, const void *instruction, size_t index, int64_t *output,
    pathlang_runtime_error_t *error);

PATHLANG_RUNTIMEC_EXPORT runtimec_status_t pathlang_runtime_read_argument_wire(
    const void *frame, const void *instruction, size_t index,
    const uint8_t **output_data, size_t *output_size,
    pathlang_runtime_error_t *error);

PATHLANG_RUNTIMEC_EXPORT runtimec_status_t pathlang_runtime_read_argument_langtype(
    const void *frame, const void *instruction, size_t index,
    const char **output_data, size_t *output_size,
    pathlang_runtime_error_t *error);

PATHLANG_RUNTIMEC_EXPORT runtimec_status_t pathlang_runtime_write_argument_wire(
    void *frame, const void *instruction, size_t index, const uint8_t *data,
    size_t size, pathlang_runtime_error_t *error);

PATHLANG_RUNTIMEC_EXPORT runtimec_status_t pathlang_runtime_write_argument_text(
    void *frame, const void *instruction, size_t index, const char *data,
    size_t size, pathlang_runtime_error_t *error);

PATHLANG_RUNTIMEC_EXPORT runtimec_status_t pathlang_runtime_write_argument_integer(
    void *frame, const void *instruction, size_t index, int64_t value,
    pathlang_runtime_error_t *error);

typedef enum pathlang_runtime_exit_kind {
    PATHLANG_RUNTIME_EXIT_NONE = 0,
    PATHLANG_RUNTIME_EXIT_ERROR = 1,
    PATHLANG_RUNTIME_EXIT_YIELD = 2
} pathlang_runtime_exit_kind_t;

PATHLANG_RUNTIMEC_EXPORT runtimec_status_t pathlang_runtime_set_exit(
    void *output_exit, pathlang_runtime_exit_kind_t kind, int64_t detail_code,
    const char *message, pathlang_runtime_error_t *error);

PATHLANG_RUNTIMEC_EXPORT runtimec_status_t pathlang_runtime_make_pointer(
    const char *target_key, size_t target_size, const char *target_langtype,
    pathlang_runtime_text_buffer_t *output, pathlang_runtime_error_t *error);

PATHLANG_RUNTIMEC_EXPORT runtimec_status_t pathlang_runtime_dereference_pointer(
    const uint8_t *wire, size_t size, pathlang_runtime_text_buffer_t *output_key,
    pathlang_runtime_text_buffer_t *output_langtype,
    pathlang_runtime_error_t *error);

PATHLANG_RUNTIMEC_EXPORT runtimec_status_t pathlang_runtime_create(
    const pathlang_runtime_create_request_t *request,
    pathlang_runtime_t **output, pathlang_runtime_error_t *error);

PATHLANG_RUNTIMEC_EXPORT runtimec_status_t pathlang_runtime_destroy(
    pathlang_runtime_t *runtime, pathlang_runtime_error_t *error);

PATHLANG_RUNTIMEC_EXPORT runtimec_status_t pathlang_runtime_execute(
    pathlang_runtime_t *runtime,
    const pathlang_runtime_execute_request_t *request,
    pathlang_runtime_execute_result_t *output,
    pathlang_runtime_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
