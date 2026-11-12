#ifndef BINRWIR_PRINT_H
#define BINRWIR_PRINT_H

/*
 * 组件：binrwir/print
 * 作用：把 BinRWIR 的产物打成一行/一段，供人核对 `.bin` 里到底是什么。
 *
 * 只读、不执行、不解析操作数的语义——操作数字只有 64 位地址，语义在载入期
 * 已解析，打印时不做任何推断。
 */

#include <stdio.h>

#include <kvlang/binrwir.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 一个 op 字：`cell[<index>] op=0x.... read=.. write=.. next=..`。 */
void binrwir_print(FILE *stream, uint32_t index, binrwir_t cell);

/* 一个操作数字：`cell[<index>] operand=0x................`。 */
void binrwir_print_operand(FILE *stream, uint32_t index,
                           binrwir_operand_t operand);

/*
 * 一整组：按**物理布局**走——op 字之后紧跟 `read_count + write_count` 个
 * 操作数字，然后才是下一个 op 字。数量不匹配（组在操作数中途结束）是畸形产物。
 */
void binrwir_group_print(FILE *stream, const binrwir_group_t *group);

#ifdef __cplusplus
}
#endif

#endif
