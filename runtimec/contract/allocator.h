#ifndef RUNTIMEC_CONTRACT_ALLOCATOR_H
#define RUNTIMEC_CONTRACT_ALLOCATOR_H

/*
 * 组件：contract/allocator
 * 作用：所有冷路径动态内存的显式来源。指令执行热路径不得动态分配；
 *       分配器不得藏进全局状态或线程局部存储。
 * 联系：contract 层只依赖自身；host、vmbin、vmkv、vmjit、engine 的 arena、
 *       Program 存储、code memory 之外的冷分配都经本接口，便于测试注入、
 *       内存耗尽分类与 session 级生命周期。
 * 约束：分配/释放必须成对且 size/alignment 一致；alignment 为 2 的幂。
 */

#include <stdbool.h>
#include <stddef.h>

#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *(*runtimec_allocate_function_t)(void *context, size_t size,
                                              size_t alignment);
typedef void (*runtimec_deallocate_function_t)(void *context,
                                               void *allocation,
                                               size_t size,
                                               size_t alignment);

/* 显式分配器：context 由实现解释，两个函数指针必须同时存在。 */
typedef struct runtimec_allocator {
    void *context;
    runtimec_allocate_function_t allocate;
    runtimec_deallocate_function_t deallocate;
} runtimec_allocator_t;

/* 置为空分配器（两个函数指针为 NULL）；空分配器不是有效分配器。 */
void runtimec_allocator_reset(runtimec_allocator_t *allocator);
/* 两个函数指针均非空才算有效。 */
bool runtimec_allocator_is_valid(const runtimec_allocator_t *allocator);
/* size==0、alignment 非 2 的幂或分配器无效时返回 NULL，不调用实现。 */
void *runtimec_allocator_allocate(const runtimec_allocator_t *allocator,
                                  size_t size, size_t alignment);
/* 参数无效时返回 INVALID；仅在释放函数确实被调用后返回 SUCCESS。 */
runtimec_status_t runtimec_allocator_deallocate(
    const runtimec_allocator_t *allocator, void *allocation,
    size_t size, size_t alignment);

#ifdef __cplusplus
}
#endif

#endif
