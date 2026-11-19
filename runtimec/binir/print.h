#ifndef BINIR_PRINT_H
#define BINIR_PRINT_H

/*
 * 组件：binir/print
 * 作用：把 BinIR 的产物打成一行/一段，供人核对 `.bin` 里到底是什么。
 *
 * 只读、不执行、不解析操作数的语义——操作数字只有 64 位地址，语义在载入期
 * 已解析，打印时不做任何推断。
 */

#include <stdio.h>

#include <kvlang/binir.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 一个 op 字：`cell[<index>] op=0x.... payloads=.. kvpc_adder=..`。 */
void binir_print(FILE *stream, uint32_t index, binir_t cell);

/* 一个操作数字：`cell[<index>] operand=0x................`。 */
void binir_print_operand(FILE *stream, uint32_t index,
                           binir_operand_t operand);

/*
 * 一整组：按**物理布局**走——op 字之后紧跟 `payload_count` 个 payload 格，
 * 然后才是下一个 op 字。payload 语义由 opcode 决定，打印不做解读；
 * 数量不匹配（组在 payload 中途结束）是畸形产物。
 */
void binir_group_print(FILE *stream, const binir_group_t *group);

#ifdef __cplusplus
}
#endif

#endif
