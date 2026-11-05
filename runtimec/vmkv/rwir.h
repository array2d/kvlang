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
 * 槽内已编码的内联字面量。只在参数是内联字面量时有效：bytes 是该槽的
 * 完整值字节（XValue head + body + attr，与 kvspace 读到的完全一致），
 * langtype 是同一个值的类型串。变量引用（含相对指针）时两个字段置空。
 *
 * 直接给出字节而不让 encoder 重读的理由：读取器与本视图必须描述同一个
 * 快照，二次读取会引入两次读之间的事实漂移面。
 */
typedef struct vmkv_rwir_literal_view_t {
    runtimec_bytes_view_t bytes;
    runtimec_string_view_t langtype;
} vmkv_rwir_literal_view_t;

/*
 * 单个明文参数。parameter_type_name 与 parameter_name 必须和 operation
 * manifest 完全匹配；key 是静态 key 或尚待求值的动态 key expression。
 * literal 只有参数是内联字面量时才有效（其余情形 bytes/langtype 均为空）。
 */
typedef struct vmkv_rwir_argument_view_t {
    runtimec_string_view_t parameter_type_name;
    runtimec_string_view_t parameter_name;
    runtimec_key_view_t key;
    vmkv_key_addressability_t key_addressability;
    vmkv_rwir_literal_view_t literal;
} vmkv_rwir_argument_view_t;

/* 一条 RWIR 指令及其两个方向分区。 */
typedef struct vmkv_rwir_instruction_view_t {
    runtimec_string_view_t operation_name;
    /*
     * opcode 槽自己的 langtype：`rwir` = 一条 rwir 指令；`rwfunc` = 一次 rwfunc
     * 调用（body 里是目标函数路径）。layout 在生成时就按目标选定了它，所以这是
     * 「这条是不是调用」的**唯一权威判据**——不得用「名字以 `/lib/` 开头」之类
     * 的字符串特判去猜，那会把拼错的名字也当成调用。
     */
    runtimec_string_view_t operation_langtype;
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

/*
 * 从一条指令槽的 **body 字节**取出算子名（counts 头之后的那段文本）。
 *
 * **为什么这个函数在 vmkv**：counts 头的布局（`VMKV_COUNTS_HEADER_SIZE`，
 * 5 字节 = 读参数 / 写参数 / dynamic）是 vmkv 的格式知识。上层要读算子名时，
 * 若自己跳过 5 字节，就等于把这份格式复制了一份到上层——**而那份复制不会
 * 随格式变化更新**，`counts` 一旦多一个字段，上层只会从中间截断名字，
 * 看起来像一个不存在的算子名。
 *
 * 判据：**谁拥有格式，谁解析。**
 *
 * `body` 是完整 body（不含 XValue head）。成功时 `output_operation` 指向
 * `body` 内部，借用，不复制。
 */
runtimec_status_t vmkv_rwir_operation_name_from_wire(
    const uint8_t *wire, size_t wire_size, runtimec_string_view_t *output_operation,
    runtimec_error_t *error);

/* 在函数源上创建顺序读取器；不拥有 function。 */
/*
 * 该 key 文本是不是**容器成员键**（含 `·`，U+00B7，UTF-8 两字节 `0xC2 0xB7`）。
 *
 * spec `05-runtime语义/03-成员访问`：成员 `base·name` 是**独立物理 key**，
 * kvspace 按 `base·` 前缀枚举，不维护 memindex 矩阵。所以成员键与局部变量的
 * 区别不是「谁声明的」，而是「它是不是一段路径」：
 *   - 局部变量：帧根下的一个槽，由 `materialize_locals` 建，要求「先写后读」；
 *   - 成员键：`<帧根>/<base·name>`，**本来就不必先被写过**——读不存在的成员是
 *     确定的失败，不是「变量未定义」。
 *
 * **按完整字节序列匹配，不判首字节**：`0xC2` 单独出现是别的字符的前半。
 */
bool vmkv_rwir_key_is_member(runtimec_key_view_t key);

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
