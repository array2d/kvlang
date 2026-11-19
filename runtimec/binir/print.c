/*
 * 组件：binir/print 的实现
 * 作用：把 op 字字段与整组格打成文本；不含任何执行或语义推断。
 */

#include "binir/print.h"

#include <inttypes.h>
#include <string.h>

void binir_print(FILE *stream, uint32_t index, binir_t cell) {
    fprintf(stream, "cell[%u] op=0x%04X payloads=%u kvpc_adder=%u\n", index,
            (unsigned)cell.opcode, (unsigned)cell.payload_count,
            (unsigned)cell.kvpc_adder);
}

void binir_print_operand(FILE *stream, uint32_t index,
                           binir_operand_t operand) {
    fprintf(stream, "cell[%u]   payload=0x%016" PRIX64 "\n", index,
            (uint64_t)operand);
}

void binir_group_print(FILE *stream, const binir_group_t *group) {
    if (group == NULL || group->cells == NULL) {
        fprintf(stream, "group: <empty>\n");
        return;
    }
    fprintf(stream, "group: cells=%u entry=%u\n", (unsigned)group->cell_count,
            (unsigned)group->entry_cell);
    uint32_t cell = group->entry_cell;
    while (cell < group->cell_count) {
        const binir_t word = group->cells[cell];
        const uint32_t payload_count = (uint32_t)word.payload_count;
        if (word.opcode == 0U) {
            fprintf(stream, "  <malformed: opcode 0 at cell %u>\n",
                    (unsigned)cell);
            return;
        }
        binir_print(stream, cell, word);
        if (payload_count > group->cell_count - cell - 1U) {
            fprintf(stream, "  <truncated: %u payload words expected>\n",
                    (unsigned)payload_count);
            return;
        }
        for (uint32_t ordinal = 0U; ordinal < payload_count; ++ordinal) {
            binir_operand_t operand = 0U;
            memcpy(&operand, &group->cells[cell + 1U + ordinal],
                   sizeof(operand));
            binir_print_operand(stream, cell + 1U + ordinal, operand);
        }
        if (word.opcode == BINIR_OPCODE_FOREIGN) {
            return;
        }
        cell += 1U + payload_count;
    }
}
