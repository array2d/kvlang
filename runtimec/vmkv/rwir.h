#ifndef RUNTIMEC_VMKV_RWIR_H
#define RUNTIMEC_VMKV_RWIR_H

/*
 * 组件：vmkv/rwir
 * 作用：明文 RWIR 指令的只读视图与顺序读取器。操作数按
 *       [ReadReference...][WriteReference...] 两个分区表达方向：字面量
 *       只能出现在读分区，读参数只能读，写参数只能定位写入。
 * 联系：vmkv/source 提供函数源；vmkv/compiler 校验后构建不可变
 *       BinRWIR Program。vmbin 与 vmjit 不直接消费本结构。
 * 约束：所有视图都只借用内存。读取器不执行、不修复、不兼容输入。
 */

#include <stddef.h>
#include <stdint.h>

#include "contract/view.h"
#include "contract/status.h"
#include "vmkv/source.h"

#ifdef __cplusplus
extern "C" {
#endif

/* specification manifest 中稳定的 RWIR 操作标识符。 */
typedef uint16_t vmkv_rwir_operation_identifier_t;

/* 操作数类别；方向由所在分区决定。 */
typedef enum vmkv_operand_kind_t {
    VMKV_OPERAND_NONE = 0,
    VMKV_OPERAND_LITERAL = 1,
    VMKV_OPERAND_VARIABLE = 2,
    VMKV_OPERAND_PATH = 3
} vmkv_operand_kind_t;

/* 单个操作数的只读视图。 */
typedef struct vmkv_operand_view_t {
    vmkv_operand_kind_t kind;
    runtimec_string_view_t source_text;
    runtimec_bytes_view_t literal_bytes;
    runtimec_string_view_t langtype;
    int32_t parameter_axis;
} vmkv_operand_view_t;

/* 一条 RWIR 指令及其两个方向分区。 */
typedef struct vmkv_rwir_instruction_view_t {
    vmkv_rwir_operation_identifier_t operation_identifier;
    uint32_t logical_program_counter;
    const vmkv_operand_view_t *read_operands;
    size_t read_operand_count;
    const vmkv_operand_view_t *write_operands;
    size_t write_operand_count;
} vmkv_rwir_instruction_view_t;

/* 组件状态只表示调用成败；读取位置由本枚举表达。 */
typedef enum vmkv_rwir_read_result_t {
    VMKV_RWIR_READ_RESULT_INVALID = 0,
    VMKV_RWIR_READ_RESULT_INSTRUCTION = 1,
    VMKV_RWIR_READ_RESULT_END = 2
} vmkv_rwir_read_result_t;

typedef struct vmkv_rwir_reader_t vmkv_rwir_reader_t;

/* 在函数源上创建顺序读取器；不拥有 function。 */
runtimec_status_t vmkv_rwir_reader_create(
    vmkv_function_source_t *function,
    vmkv_rwir_reader_t **output_reader,
    runtimec_error_t *error);

/* 成功读取或到达结尾都返回 RUNTIMEC_STATUS_SUCCESS，读取结果单独表达。 */
runtimec_status_t vmkv_rwir_reader_next(
    vmkv_rwir_reader_t *reader,
    vmkv_rwir_read_result_t *output_result,
    vmkv_rwir_instruction_view_t *output_instruction,
    runtimec_error_t *error);

runtimec_status_t vmkv_rwir_reader_destroy(vmkv_rwir_reader_t *reader);

#ifdef __cplusplus
}
#endif

#endif
