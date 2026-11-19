/*
 * 组件：binirgrouprun 的实现
 * 作用：遍历一组格，按 opid 派发到组 handler。
 */

#include "binirgrouprun/grouprun.h"

#include <stddef.h>
#include <stdlib.h>

#include "binir/handler.h"
#include "binir/opid_control.h"

void binirgrouprun(struct runtimec_runtime *runtime,
                     const binir_group_t *group) {
    if (runtime == NULL || group == NULL || group->cells == NULL) {
        abort();
    }
    uint32_t cell = group->entry_cell;
    if (cell >= group->cell_count) {
        abort();
    }
    while (cell < group->cell_count) {
        const binir_t word = group->cells[cell];
        const uint32_t payload_count = (uint32_t)word.payload_count;
        if (word.opcode == 0U || word.opcode >= binir_handler_count) {
            abort();
        }
        if (payload_count > group->cell_count - cell - 1U) {
            abort();
        }
        /* 控制转移是 kvcpu 核心规则：目标在 payload 里，是 cells 数组的索引（2 字节）。 */
        if (word.opcode == BINIR_OPID_GOTO || word.opcode == BINIR_OPID_BRANCH) {
            const binir_operand_t *payloads =
                (const binir_operand_t *)(const void *)(group->cells + cell + 1U);
            uint32_t target = 0U;
            if (word.opcode == BINIR_OPID_GOTO) {
                target = (uint32_t)(payloads[0] & UINT64_C(0xFFFF));
            } else {
                const uint8_t condition = *(const uint8_t *)(uintptr_t)payloads[0];
                target = (uint32_t)(payloads[condition ? 1U : 2U] & UINT64_C(0xFFFF));
            }
            if (target >= group->cell_count) {
                abort();
            }
            cell = target;
            continue;
        }
        if (word.opcode == BINIR_OPID_RETURN) {
            return;
        }
        const binir_instruction_t view = {
            .opcode = (uint16_t)word.opcode,
            .kvpc_adder = (uint16_t)word.kvpc_adder,
            .payload_count = payload_count,
            .payloads = (const binir_operand_t *)(const void *)
                (group->cells + cell + 1U),
        };
        binir_handler_table[word.opcode](runtime, &view);
        if (word.opcode == BINIR_OPCODE_FOREIGN) {
            return;
        }
        cell += 1U + payload_count;
    }
}
