#ifndef VMBIN_INTERPRETER_DISPATCH_H
#define VMBIN_INTERPRETER_DISPATCH_H

/*
 * 组件：vmbin/interpreter 内部派生 dispatch
 * 作用：把不可变 Program 派生为解释器私有的处理函数选择表。
 * 联系：只有 interpreter 构造和消费；Program 与 vmjit 均不依赖本表。
 * 约束：可整层丢弃重建，不拥有语义，不构成执行入口或第三执行器。
 */

#include <stdint.h>

#include "contract/allocator.h"
#include "contract/status.h"
#include "vmbin/program.h"

typedef struct vmbin_interpreter_dispatch_table
    vmbin_interpreter_dispatch_table_t;

typedef struct vmbin_interpreter_dispatch_entry {
    uint16_t handler_identifier;
    uint8_t handler_form;
    uint8_t flags;
} vmbin_interpreter_dispatch_entry_t;

runtimec_status_t vmbin_interpreter_dispatch_table_create(
    const vmbin_program_t *program,
    const runtimec_allocator_t *allocator,
    vmbin_interpreter_dispatch_table_t **output_table);

runtimec_status_t vmbin_interpreter_dispatch_table_destroy(
    vmbin_interpreter_dispatch_table_t *table);

runtimec_status_t vmbin_interpreter_dispatch_table_get(
    const vmbin_interpreter_dispatch_table_t *table,
    uint32_t instruction_identifier,
    vmbin_interpreter_dispatch_entry_t *output_entry);

#endif
