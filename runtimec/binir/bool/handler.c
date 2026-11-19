/*
 * 组件：binir/bool
 * 作用：bool 组的 handler。尚未实现——调用即终止，不留静默成功路径。
 */

#include <stdlib.h>

#include "binir/handler.h"

void runtimec_binir_bool_execute(struct runtimec_runtime *runtime,
                                   const binir_instruction_t *instruction) {
    (void)runtime;
    (void)instruction;
    abort();
}
