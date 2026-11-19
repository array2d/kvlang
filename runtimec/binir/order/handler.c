/*
 * 组件：binir/order
 * 作用：order 组的 handler。尚未实现——调用即终止，不留静默成功路径。
 */

#include <stdlib.h>

#include "binir/handler.h"

void runtimec_binir_order_execute(struct runtimec_runtime *runtime,
                                    const binir_instruction_t *instruction) {
    (void)runtime;
    (void)instruction;
    abort();
}
