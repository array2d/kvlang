#ifndef KVLANG_RUNTIMEC_H
#define KVLANG_RUNTIMEC_H

/*
 * runtimec 的唯一公共 C 接口。
 *
 * 公共层只负责创建 runtimec、提交一次执行请求和销毁 runtimec。RWIR、
 * BinRWIR、vmkv、vmbin、vmjit 与 kvspace 的内部结构都不在此暴露。
 * 所有可失败调用返回 runtimec_status_t，输出对象在失败前先进入无效状态，
 * 错误写入调用方提供的缓冲区。当前骨架只会明确返回未实现，不伪造成功。
 */

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
    RUNTIMEC_STATUS_FALLBACK = 3,
    RUNTIMEC_STATUS_UNSUPPORTED = 4,
    RUNTIMEC_STATUS_NOT_IMPLEMENTED = 5,
    RUNTIMEC_STATUS_INVALID = 6,
    RUNTIMEC_STATUS_OUT_OF_MEMORY = 7,
    RUNTIMEC_STATUS_ERROR = 8,
    RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY = 9
} runtimec_status_t;

typedef struct kvlang_runtime kvlang_runtime_t;

typedef struct kvlang_runtime_text_buffer {
    char *data;              /* 调用方拥有；capacity 为 0 时可以为 NULL。 */
    size_t capacity;         /* 包含字符串终止空字符的可写字节数。 */
    size_t length;           /* 实际写入字节数，不含字符串终止空字符。 */
    size_t required_length;  /* 完整内容字节数，不含字符串终止空字符。 */
} kvlang_runtime_text_buffer_t;

/*
 * message 容量不足时不写入半条诊断：函数返回
 * RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY，status 与返回值一致，
 * operation_status 保存原始失败，required_length 给出完整消息长度。
 */
typedef struct kvlang_runtime_error {
    runtimec_status_t status;            /* 与函数返回状态完全一致。 */
    runtimec_status_t operation_status;  /* 容量失败时保存原始操作状态。 */
    int64_t code;
    kvlang_runtime_text_buffer_t message;
} kvlang_runtime_error_t;

typedef struct kvlang_runtime_create_request {
    const char *data_source_name;
} kvlang_runtime_create_request_t;

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
