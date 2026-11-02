/*
 * 组件：contract/allocator 的实现
 * 作用：对显式分配器做参数校验后转发；不缓存、不统计、不分配自身状态。
 * 联系：host/vmbin/vmkv/BinRWIR Compiler/engine 的冷路径分配经这里；测试可注入
 *       计数或失败分配器，验证内存耗尽与失败封闭行为。
 */

#include "contract/allocator.h"

static bool runtimec_alignment_is_valid(size_t alignment) {
    return alignment != 0U && (alignment & (alignment - 1U)) == 0U;
}

void runtimec_allocator_reset(runtimec_allocator_t *allocator) {
    if (allocator != NULL) {
        allocator->context = NULL;
        allocator->allocate = NULL;
        allocator->deallocate = NULL;
    }
}

bool runtimec_allocator_is_valid(const runtimec_allocator_t *allocator) {
    return allocator != NULL && allocator->allocate != NULL &&
           allocator->deallocate != NULL;
}

void *runtimec_allocator_allocate(const runtimec_allocator_t *allocator,
                                  size_t size, size_t alignment) {
    if (!runtimec_allocator_is_valid(allocator) || size == 0U ||
        !runtimec_alignment_is_valid(alignment)) {
        return NULL;
    }
    return allocator->allocate(allocator->context, size, alignment);
}

runtimec_status_t runtimec_allocator_deallocate(
    const runtimec_allocator_t *allocator, void *allocation,
    size_t size, size_t alignment) {
    if (allocation == NULL || !runtimec_allocator_is_valid(allocator) ||
        size == 0U || !runtimec_alignment_is_valid(alignment)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    allocator->deallocate(allocator->context, allocation, size, alignment);
    return RUNTIMEC_STATUS_SUCCESS;
}
