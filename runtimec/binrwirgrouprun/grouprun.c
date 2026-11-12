/*
 * 组件：binrwirgrouprun 的实现
 * 作用：遍历一组格，按 opid 派发到组 handler。
 */

#include "binrwirgrouprun/grouprun.h"

#include <stddef.h>
#include <stdlib.h>

#include "binrwir/handler.h"

void binrwirgrouprun(struct runtimec_runtime *runtime,
                     const binrwir_group_t *group) {
    if (group == NULL || group->cells == NULL) {
        abort();
    }
    uint32_t cell = group->entry_cell;
    while (cell < group->cell_count) {
        const binrwir_t instruction = group->cells[cell];
        const uint32_t read_count = (uint32_t)instruction.read_count;
        const uint32_t write_count = (uint32_t)instruction.write_count;
        if (cell < read_count ||
            cell + write_count >= group->cell_count) {
            abort();
        }
        const binrwir_instruction_t view = {
            .opcode = (uint16_t)instruction.opcode,
            .read_count = read_count,
            .write_count = write_count,
            .reads = (const binrwir_operand_t *)(const void *)
                (group->cells + (cell - read_count)),
            .writes = (binrwir_operand_t *)(void *)(group->cells + cell + 1U),
        };
        if (view.opcode == 0U || view.opcode >= binrwir_handler_count) {
            abort();
        }
        binrwir_handler_table[view.opcode](runtime, &view);
        if (instruction.next_distance == 0U) {
            abort();
        }
        cell += (uint32_t)instruction.next_distance;
    }
}
