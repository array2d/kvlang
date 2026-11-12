/*
 * 组件：binrwir/logic
 * 作用：logic 组的 handler。尚未实现——调用即终止，不留静默成功路径。
 */

#include <stdlib.h>

#include "binrwir/handler.h"

void runtimec_binrwir_logic_execute(struct runtimec_runtime *runtime,
                                    const binrwir_instruction_t *instruction) {
    (void)runtime;
    (void)instruction;
    abort();
}
