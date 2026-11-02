#ifndef BINRWIR_EXECUTION_PLAN_INTERNAL_H
#define BINRWIR_EXECUTION_PLAN_INTERNAL_H

/*
 * 中立 execution plan 的共享私有布局。binrwir_compiler/optimization 负责构造，
 * binrwir/execution_plan 只负责验证、查询与销毁。允许的四类派生数组放入
 * 一个 owned_storage 块。Program 自身的 instruction/argument 二进制和其中
 * 的 V.body 地址不复制到 plan，也不在这里建立缓存或寄存器状态。
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
