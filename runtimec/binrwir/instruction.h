#ifndef BINRWIR_INSTRUCTION_H
#define BINRWIR_INSTRUCTION_H

/*
 * 组件：binrwir/instruction
 * 作用：定义只含 opid 的 BinRWIR 二进制指令头，以及非二进制只读视图。
 * 联系：Program 私有存储负责 framing；vmbin 与 BinRWIR JIT 通过 view 消费。
 * 约束：二进制指令头不得携带 offset、count、key、类型、名称或值。FOREIGN 标记
 *       复用同一个 4 字节头（只有 opid），因此指令头布局不因它改变。
 */

#include <stddef.h>
#include <stdint.h>

#include "binrwir/argument.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BINRWIR_OPERATION_IDENTIFIER_NONE UINT32_MAX

/*
 * FOREIGN：不属于本 runtime 的能力、但必须留在 Program 里的结构标记。
 *
 * 它**不是算子**，所以不进 capability 表——catalog 是「runtimec 能执行的能力」
 * 清单，把标记写进去等于宣称存在一个叫 foreign 的算子，那是污染。生成器分配的
 * opid 一律 < BINRWIR_OPERATION_IDENTIFIER_RESERVED_BASE，保留区留给结构标记。
 *
 * 语义：执行到它就地停下，把控制交出去；它不执行任何操作、不携带操作数、不写
 * 存储。source logical program counter 记录了它在源里的位置，而那个位置来自
 * layout 的 irseq、不是 Program 内下标——所以 Program 重新编码后标记仍然有效。
 */
#define BINRWIR_OPERATION_IDENTIFIER_FOREIGN UINT32_C(0xFFFFFFFE)
#define BINRWIR_OPERATION_IDENTIFIER_RESERVED_BASE UINT32_C(0xFFFFFF00)

typedef struct binrwir_instruction {
    uint32_t operation_identifier;
} binrwir_instruction_t;

typedef struct binrwir_argument_span {
    const binrwir_argument_t *data;
    size_t count;
} binrwir_argument_span_t;

typedef struct binrwir_instruction_build_request {
    uint32_t source_logical_program_counter;
    uint32_t operation_identifier;
    binrwir_argument_span_t read_arguments;
    binrwir_argument_span_t write_arguments;
} binrwir_instruction_build_request_t;

typedef struct binrwir_instruction_view {
    const binrwir_instruction_t *instruction;
    uint32_t source_logical_program_counter;
    uint32_t reserved_32_bits;
    binrwir_argument_span_t read_arguments;
    binrwir_argument_span_t write_arguments;
} binrwir_instruction_view_t;

runtimec_status_t binrwir_instruction_validate_build_request(
    const binrwir_instruction_build_request_t *request);
runtimec_status_t binrwir_instruction_get_view(
    const binrwir_instruction_t *instruction,
    uint32_t source_logical_program_counter,
    binrwir_argument_span_t read_arguments,
    binrwir_argument_span_t write_arguments,
    binrwir_instruction_view_t *output_view);

_Static_assert(sizeof(binrwir_instruction_t) == 4U,
               "binrwir instruction heads must contain only opid");
_Static_assert(offsetof(binrwir_instruction_t, operation_identifier) == 0U,
               "binrwir opid must begin each instruction");

#ifdef __cplusplus
}
#endif

#endif
