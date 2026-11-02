/*
 * 组件：contract/status 的实现
 * 作用：只做枚举范围、失败分类与诊断名的纯函数判断；不读 kvspace、不分配。
 * 联系：被 host/vmbin/vmkv/BinRWIR Compiler/engine/interface 共同调用，是组件边界的
 *       唯一状态词汇表。
 */

#include "contract/status.h"

bool runtimec_status_is_valid(runtimec_status_t status) {
    return status >= RUNTIMEC_STATUS_SUCCESS &&
           status <= RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
}

bool runtimec_status_is_failure(runtimec_status_t status) {
    return status == RUNTIMEC_STATUS_UNSUPPORTED ||
           status == RUNTIMEC_STATUS_NOT_IMPLEMENTED ||
           status == RUNTIMEC_STATUS_INVALID ||
           status == RUNTIMEC_STATUS_OUT_OF_MEMORY ||
           status == RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY ||
           status == RUNTIMEC_STATUS_ERROR;
}

const char *runtimec_status_name(runtimec_status_t status) {
    switch (status) {
    case RUNTIMEC_STATUS_SUCCESS:
        return "success";
    case RUNTIMEC_STATUS_YIELD:
        return "yield";
    case RUNTIMEC_STATUS_WAIT:
        return "wait";
    case RUNTIMEC_STATUS_UNSUPPORTED:
        return "unsupported";
    case RUNTIMEC_STATUS_NOT_IMPLEMENTED:
        return "not_implemented";
    case RUNTIMEC_STATUS_INVALID:
        return "invalid";
    case RUNTIMEC_STATUS_OUT_OF_MEMORY:
        return "out_of_memory";
    case RUNTIMEC_STATUS_ERROR:
        return "error";
    case RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY:
        return "insufficient_capacity";
    default:
        return "invalid_status";
    }
}
