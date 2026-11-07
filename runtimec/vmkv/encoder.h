#ifndef RUNTIMEC_VMKV_ENCODER_H
#define RUNTIMEC_VMKV_ENCODER_H

/*
 * 组件：vmkv/encoder
 * 作用：读取并验证明文 RWIR，将 operation name 和 typed named parameters
 *       映射为 opid/argidx，再把静态参数 key 解引用为非零 64 位 argaddr，
 *       最终等价编码为 canonical、不可变的 binrwir_binfunc_t。
 * 联系：engine route 在 compilation repository 未命中时调用；源只经
 *       vmkv/source 与主机服务读取；结果交给 BinRWIR Compiler。
 * 约束：vmkv 只有 RWIR 到 canonical BinRWIR 这一条正常方向。每个静态 key
 *       必须通过 host service resolve_static_xvalue_address 取得 XValue 基址，
 *       再由合同唯一函数推导 V.body 地址；动态 key、零地址、地址溢出及无法保证
 *       snapshot 生命周期的地址返回 UNSUPPORTED 或错误，且不产生 Program。
 *       编码不得优化、融合、生成执行计划或机器码，也不得执行。
 */

#include "binrwir/argument.h"
#include "binrwir/binfunc.h"
#include "contract/allocator.h"
#include "contract/snapshot.h"
#include "contract/status.h"
#include "contract/view.h"
#include "vmkv/local_types.h"
#include "vmkv/rwir.h"
#include "vmkv/source.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vmkv_encoder_t vmkv_encoder_t;

/* 一次等价编码请求。RWIR 验证是强制步骤，不提供绕过开关。 */
typedef struct vmkv_encode_request_t {
    runtimec_kvspace_access_t *kvspace_access;
    runtimec_string_view_t function_path;
    runtimec_snapshot_t expected_snapshot;
} vmkv_encode_request_t;

/* 一条指令的 canonical 形状使用 binrwir_argument_t；具体 langtype 旁挂两份表。 */

/*
 * 函数形参规模与「非形参引用的 argidx」游标。
 *
 * canonical argidx 的编号规则只有这一处：读参按声明顺序取 0..nr-1，写参紧随其后取
 * nr..nr+nw-1；这两个区间进签名，slot 与 argidx 一一对应。非形参引用
 * （LITERAL / STATIC / LOCAL / FUNCTION）从 nr+nw 起按出现次序递增，不进签名、
 * 也不被签名索引，因此 argument_index 在整份 Program 内唯一。
 *
 * next_operand_argument_index 是 in/out 字段：调用方在开始编码一份 Program 前把它
 * 置成 nr+nw，encoder 每发射一个非形参引用就递增。小于该起点即视为调用方错误。
 */
typedef struct vmkv_encode_function {
    uint32_t read_parameter_count;
    uint32_t write_parameter_count;
    uint32_t next_operand_argument_index;
} vmkv_encode_function_t;

/*
 * 一条指令的 canonical 形状。read/write 两个分区与 RWIR 源同序同数。
 *
 * argument_index 的取值按引用种类分两类：
 *   - PARAMETER_READ / PARAMETER_WRITE：被引用形参的 canonical argidx，
 *     slot 是它在所属方向形参表里的序号（0 起）；
 *   - LITERAL / STATIC / LOCAL / FUNCTION：Program 内唯一的出现序号。
 *
 * slot / payload 按 reference kind 分工：
 *   - PARAMETER_READ / PARAMETER_WRITE：slot = 形参序号，payload = 0；
 *   - STATIC：slot = 符号池里绝对路径文本的下标（vmbin 按 key 读写的依据），
 *     payload = 已推导的 body 地址（留给 JIT，phase 1 不消费）；
 *   - LOCAL / FUNCTION：slot = 符号池下标（LOCAL 是变量名，FUNCTION 是函数名，
 *     都不含 `/lib/`），payload = 0；
 *   - LITERAL：slot = 0，payload = Program 字面量池下标。
 *
 * 生命周期：argument 数组与类型表借用 encoder 的内部缓冲，下一次调用即失效；
 * 类型表还借用函数源内存，随 vmkv_source_close_function 失效。需要跨源使用时
 * 由调用方自己深拷贝，Program 物化在 finalize 时深拷贝。
 */
typedef struct vmkv_canonical_instruction {
    binrwir_operation_identifier_t operation_identifier;
    uint32_t logical_program_counter;
    binrwir_argument_span_t read_arguments;
    binrwir_argument_span_t write_arguments;
    /* 与两个分区同长同序；每个条目是该操作数唯一的具体 langtype。 */
    const runtimec_string_view_t *read_parameter_types;
    const runtimec_string_view_t *write_parameter_types;
    /* 能力行的 alias policy 是行级的：为真表示该行的读写操作数允许指向同一
     * body，Program 组装时据此登记 (read, write) 规则对。 */
    bool aliasing_declared;
} vmkv_canonical_instruction_t;

runtimec_status_t vmkv_encoder_create(
    runtimec_allocator_t allocator,
    vmkv_encoder_t **output_encoder,
    runtimec_error_t *error);

runtimec_status_t vmkv_encoder_destroy(vmkv_encoder_t *encoder);

runtimec_status_t vmkv_encoder_get_canonical_encoding_revision(
    const vmkv_encoder_t *encoder,
    uint32_t *output_canonical_encoding_revision);

/*
 * VMKV 地址化的唯一窄入口。它先验证明文 parameter type/name/key，再拒绝
 * dynamic key；静态 key 只能由 host service 的非零 XValue 基址安全推导 V.body
 * 地址后生成 argument。该函数不读取 V.body，也不缓存 key、XValue 或 body。
 *
 * 绝对路径文本同时登记进 Program 符号池：slot = 符号下标（vmbin 按 key 访问），
 * payload = body 地址（JIT 用）。同名路径重复登记由 builder 幂等处理。
 */
runtimec_status_t vmkv_encode_static_argument(
    const vmkv_encode_request_t *request,
    binrwir_binfunc_builder_t *builder,
    const vmkv_rwir_argument_view_t *source_argument,
    uint32_t argument_index,
    binrwir_argument_t *output_argument,
    runtimec_error_t *error);

/*
 * 把一条明文 RWIR 指令收敛成 canonical 指令：opid 经别名表解析（含编译器
 * 改选特化），每个实参收敛成单一具体 langtype 并与能力行掩码求交，写参类型
 * 按能力行的结果规则算出。输出借用 encoder 的内部缓冲，下一次调用即失效。
 *
 * builder 是符号池与字面量池的登记目标：STATIC / LOCAL / FUNCTION 参数项的 slot、
 * LITERAL 参数项的 payload 都由它给出，因此 canonical 指令只能发射进这份
 * Program 的池空间。builder 为 NULL 时 INVALID——没有池就没有可解析的引用。
 *
 * 失败封闭：未注册 operation、并集参数、掩码外类型、无法唯一确定的结果类型、
 * 无法表达的键（指针/扩展参数、非 `*[0,±k]` 的帧内槽表达式）都不产生 canonical
 * 指令；这些情形返回 RUNTIMEC_STATUS_UNSUPPORTED。
 */
runtimec_status_t vmkv_encode_instruction(
    vmkv_encoder_t *encoder,
    const vmkv_encode_request_t *request,
    binrwir_binfunc_builder_t *builder,
    const vmkv_local_types_t *local_types,
    vmkv_encode_function_t *function,
    const vmkv_rwir_instruction_view_t *source_instruction,
    vmkv_canonical_instruction_t *output_instruction,
    runtimec_error_t *error);

/*
 * 成功时只返回与已验证 RWIR 等价的 canonical BinRWIR Program。任一 instruction
 * 的 operation name、parameter type/name 或 argidx 映射不匹配时失败；遇到动态
 * key 必须返回 UNSUPPORTED，并保持 output_program 为 NULL。
 */
runtimec_status_t vmkv_encode(
    vmkv_encoder_t *encoder,
    const vmkv_encode_request_t *request,
    binrwir_binfunc_t **output_program,
    runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
