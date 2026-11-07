#ifndef RUNTIMEC_VMKV_INTERNAL_H
#define RUNTIMEC_VMKV_INTERNAL_H

/*
 * 组件：vmkv 内部共享形状
 * 作用：source 与 rwir 两个实现文件之间共享的函数源内部布局。它不是公共
 *       接口：除 vmkv/ 目录外不得包含。
 * 联系：source.c 打开并填充；rwir.c 只读消费。
 * 约束：所有指针都指向 vmkv_function_source_t 自己的分配区，随 close 一起释放。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "contract/allocator.h"
#include "contract/kvspace_access.h"
#include "contract/view.h"
#include "vmkv/source.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 计数头：layout counts_body（5 字节，全小端）。 */
#define VMKV_COUNTS_HEADER_SIZE 5U

/* 诊断码：稳定整数，进 error.code。 */
enum {
    VMKV_DIAG_INVALID_REQUEST = 1,
    VMKV_DIAG_HOST_CAPABILITY_MISSING = 2,
    VMKV_DIAG_HOST_FAILURE = 3,
    VMKV_DIAG_MALFORMED_KEY = 4,
    VMKV_DIAG_MALFORMED_VALUE = 5,
    VMKV_DIAG_MISSING_SIGNATURE_ANCHOR = 6,
    VMKV_DIAG_MISSING_PARAMETER_DEFINITION = 7,
    VMKV_DIAG_MISSING_OPCODE = 8,
    VMKV_DIAG_ARGUMENT_COUNT_MISMATCH = 9,
    VMKV_DIAG_EMPTY_OPCODE = 10,
    VMKV_DIAG_OUT_OF_MEMORY = 11
};

/* 一次登记过的分配（close 时统一释放）。 */
typedef struct vmkv_allocation {
    void *pointer;
    size_t size;
    size_t alignment;
} vmkv_allocation_t;

/* 参数定义键 `.[0,±j]`：body = 参数名 NUL def-langtype 串。 */
typedef struct vmkv_param_definition {
    const char *name;
    size_t name_size;
    const char *type_name;
    size_t type_name_size;
    bool present;
} vmkv_param_definition_t;

/* 一个坐标槽的已读值：wire 是整块值（head + body + attr）。 */
typedef struct vmkv_source_slot {
    uint32_t line;
    int32_t column;
    const uint8_t *wire;
    size_t wire_size;
    const uint8_t *body;
    size_t body_size;
    runtimec_string_view_t langtype;
    bool present;
} vmkv_source_slot_t;

struct vmkv_function_source_t {
    runtimec_kvspace_access_t *kvspace_access;
    runtimec_snapshot_t snapshot;
    const char *function_path;
    size_t function_path_size;
    /* 签名行 `[0,0]` 的计数头。 */
    uint32_t read_parameter_count;
    uint32_t write_parameter_count;
    bool dynamic_parameters;
    /* 参数定义键（下标 0 对应 j=1）。 */
    vmkv_param_definition_t *read_parameters;
    vmkv_param_definition_t *write_parameters;
    /* 坐标槽 `[line,column]`（不含 `[0,0]`）。 */
    vmkv_source_slot_t *slots;
    size_t slot_count;
    uint32_t instruction_line_count;
    vmkv_allocation_t *allocations;
    size_t allocation_count;
    size_t allocation_capacity;
};

/* 在函数源的分配器上分配并登记；失败返回 NULL。 */
void *vmkv_source_allocate(vmkv_function_source_t *function, size_t size,
                           size_t alignment);

/* 值切分：head 固定 8 字节，body 紧随其后，attr 区在 body 之后。 */
bool vmkv_source_split_wire(const uint8_t *wire, size_t wire_size,
                            const uint8_t **output_body,
                            size_t *output_body_size,
                            runtimec_string_view_t *output_langtype);

/* 在 source->slots 中查坐标；未找到返回 NULL。 */
const vmkv_source_slot_t *vmkv_source_find_slot(
    const vmkv_function_source_t *function, uint32_t line, int32_t column);

#ifdef __cplusplus
}
#endif

#endif
