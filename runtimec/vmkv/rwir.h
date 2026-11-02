#ifndef RUNTIMEC_VMKV_RWIR_H
#define RUNTIMEC_VMKV_RWIR_H

/*
 * 组件：vmkv/rwir
 * 作用：明文 RWIR instruction 的只读视图与顺序读取器。operation name、
 *       parameter type、parameter name 和 key expression 都保留到 VMKV 完成
 *       manifest 校验和地址解析；Read/Write 方向由两个参数分区显式表达。
 * 联系：vmkv/source 提供函数源；vmkv/encoder 将已验证名称映射为 opid/argidx，
 *       并只把静态 key 解引用为 argaddr。vmbin 与 JIT 不直接消费本结构。
 * 约束：所有视图都只借用内存。读取器不执行、不优化、不生成机器码、不修复
 *       输入。动态 key 只能被识别并拒绝，不能进入 canonical BinRWIR。
 */

#include <stddef.h>
#include <stdint.h>

#include "contract/view.h"
#include "contract/status.h"
#include "vmkv/source.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum vmkv_key_addressability_t {
    VMKV_KEY_ADDRESSABILITY_INVALID = 0,
    VMKV_KEY_ADDRESSABILITY_STATIC = 1,
    VMKV_KEY_ADDRESSABILITY_DYNAMIC = 2
} vmkv_key_addressability_t;

/*
 * 单个明文参数。parameter_type_name 与 parameter_name 必须和 operation
 * manifest 完全匹配；key 是静态 key 或尚待求值的动态 key expression。
 */
typedef struct vmkv_rwir_argument_view_t {
    runtimec_string_view_t parameter_type_name;
    runtimec_string_view_t parameter_name;
    runtimec_key_view_t key;
    vmkv_key_addressability_t key_addressability;
} vmkv_rwir_argument_view_t;

/* 一条 RWIR 指令及其两个方向分区。 */
typedef struct vmkv_rwir_instruction_view_t {
    runtimec_string_view_t operation_name;
    uint32_t logical_program_counter;
    const vmkv_rwir_argument_view_t *read_arguments;
    size_t read_argument_count;
    const vmkv_rwir_argument_view_t *write_arguments;
    size_t write_argument_count;
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
