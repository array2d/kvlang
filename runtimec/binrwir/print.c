/*
 * 组件：binrwir/print 的实现
 * 作用：把 op 字的三个字段与整组格打成文本；不含任何执行或语义推断。
 */

#include "binrwir/print.h"

#include <inttypes.h>
#include <string.h>

void binrwir_print(FILE *stream, uint32_t index, binrwir_t cell) {
    fprintf(stream, "cell[%u] op=0x%04X read=%u write=%u next=%u\n", index,
            (unsigned)cell.opcode, (unsigned)cell.read_count,
            (unsigned)cell.write_count, (unsigned)cell.next_distance);
}

void binrwir_print_operand(FILE *stream, uint32_t index,
                           binrwir_operand_t operand) {
    fprintf(stream, "cell[%u]   operand=0x%016" PRIX64 "\n", index,
            (uint64_t)operand);
}

void binrwir_group_print(FILE *stream, const binrwir_group_t *group) {
    if (group == NULL || group->cells == NULL) {
        fprintf(stream, "group: <empty>\n");
        return;
    }
    fprintf(stream, "group: cells=%u entry=%u\n", (unsigned)group->cell_count,
            (unsigned)group->entry_cell);
    uint32_t cell = group->entry_cell;
    while (cell < group->cell_count) {
        const binrwir_t instruction = group->cells[cell];
        const uint32_t read_count = (uint32_t)instruction.read_count;
        const uint32_t write_count = (uint32_t)instruction.write_count;
        if (cell < read_count) {
            fprintf(stream, "  <malformed: %u read operands before cell %u>\n",
                    (unsigned)read_count, (unsigned)cell);
            return;
        }
        /* 读操作数在 op 字**之前**：r1、r2 … 紧挨着 op。 */
        for (uint32_t ordinal = 0U; ordinal < read_count; ++ordinal) {
            binrwir_operand_t operand = 0U;
            memcpy(&operand, &group->cells[cell - read_count + ordinal],
                   sizeof(operand));
            binrwir_print_operand(stream, cell - read_count + ordinal, operand);
        }
        binrwir_print(stream, cell, instruction);
        /* 写操作数在 op 字**之后**：w1、w2 … 紧跟着 op。 */
        if (cell + write_count >= group->cell_count) {
            fprintf(stream, "  <truncated: %u write operands expected>\n",
                    (unsigned)write_count);
            return;
        }
        for (uint32_t ordinal = 0U; ordinal < write_count; ++ordinal) {
            binrwir_operand_t operand = 0U;
            memcpy(&operand, &group->cells[cell + 1U + ordinal],
                   sizeof(operand));
            binrwir_print_operand(stream, cell + 1U + ordinal, operand);
        }
        if (instruction.opcode == BINRWIR_OPCODE_FOREIGN) {
            return;
        }
        if (instruction.next_distance == 0U) {
            fprintf(stream, "  <malformed: next_distance == 0 at cell %u>\n",
                    (unsigned)cell);
            return;
        }
        cell += instruction.next_distance;
    }
}
