#ifndef BINRWIR_EXECUTION_PLAN_INTERNAL_H
#define BINRWIR_EXECUTION_PLAN_INTERNAL_H

/*
 * 中立 execution plan 的共享私有布局。binrwir_compiler/optimization 负责构造，
 * binrwir/execution_plan 只负责验证、查询与销毁。全部派生数组放入一个
 * owned_storage 块，避免两个组件重复定义对象布局。
 */

#include "binrwir/execution_plan.h"
#include "contract/allocator.h"

struct binrwir_execution_plan {
    runtimec_allocator_t allocator;
    binrwir_execution_plan_view_t view;
    void *owned_storage;
    size_t owned_storage_size;
    size_t owned_storage_alignment;
};

#endif
