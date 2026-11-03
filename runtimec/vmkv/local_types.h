#ifndef RUNTIMEC_VMKV_LOCAL_TYPES_H
#define RUNTIMEC_VMKV_LOCAL_TYPES_H

/*
 * 组件：vmkv/local_types
 * 作用：在一个函数源上收集每个帧局部符号（裸标识符）在**写位置**出现的那一个具体
 *       langtype，作为 Program 符号池「这个局部槽多大」的唯一来源。
 * 联系：vmkv/encoder 在编码任何指令之前调一次；登记 LOCAL 符号时按名字查它。
 * 约束：名字与类型都借用函数源内存，随 vmkv_source_close_function 失效。
 *       跨指令类型冲突、只在读位置出现（无定值）、写位置类型不具体，都是编译期
 *       失败——不选其一、不做类型提升、不猜。
 */

#include <stdbool.h>
#include <stddef.h>

#include "contract/allocator.h"
#include "contract/status.h"
#include "contract/view.h"
#include "vmkv/source.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vmkv_local_types vmkv_local_types_t;

/* 单调完一份函数源；任何冲突或无定值局部都在这里失败，输出为 NULL。 */
runtimec_status_t vmkv_local_types_collect(
    vmkv_function_source_t *function, runtimec_allocator_t allocator,
    vmkv_local_types_t **output_local_types, runtimec_error_t *error);

/* 与其他组件的生命周期入口一致：返回状态而不是 void。 */
runtimec_status_t vmkv_local_types_destroy(vmkv_local_types_t *local_types,
                                           runtimec_allocator_t allocator);

/* 查不到返回 false 并把 output_langtype 置空。 */
bool vmkv_local_types_find(const vmkv_local_types_t *local_types,
                           runtimec_string_view_t name,
                           runtimec_string_view_t *output_langtype);

#ifdef __cplusplus
}
#endif

#endif
