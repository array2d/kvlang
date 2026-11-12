#ifndef BINRWIR_EXECUTION_PLAN_INTERNAL_H
#define BINRWIR_EXECUTION_PLAN_INTERNAL_H

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
