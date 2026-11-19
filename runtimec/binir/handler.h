#ifndef BINIR_HANDLER_H
#define BINIR_HANDLER_H

/*
 * 组件：binir/handler
 * 作用：按算子组放置 handler 的统一入口契约。
 *
 * 分组与产物文件一一对应：`runtimec_v9_binir_<组>.kv` ↔ `<组>/handler.c`。
 * 每组只暴露下面一个入口，由解释器按 opid 所属组派发。
 *
 * 一条指令进入 handler 前，payload 已经从格序列取出：**全部 payload 都在 op 字
 * 之后**，顺序由 opcode 完全确定（生成表：声明顺序，读在前、写在后）。bin 层不
 * 再区分读参与写参——handler 按自己 opcode 的固定位置取 payload。
 * 地址在 v9 载入期解析完毕，执行期不再查 key。
 *
 * 操作数类型只有四种，直接并列：
 *   `uint8`（1 字节）、`[N]uint8`（N=2、4、8）、`[]uint8`（变长字节）、
 *   `pcs0`（`[8]uint8`：指向 pc 的 s0 分量的**指针**。s0 逻辑上是 uint32；
 *         操作数写 8 字节是因为它是地址，不是因为它是 8 字节整数。
 *         对 pcs0 的运算一律用 **uint32 类算子**，例如 `pcs0++` 是
 *         `add_uint32_uint32_uint32`）、
 *   `stack`（`[8]uint8`：帧栈锚点，与 pcs0 同级的常用 key。相对 key 靠它
 *         补成绝对位置，`kvspace·resolve` 的第二个操作数就是它）。
 * 类型信息在 v9 编译时已消化进 opid，执行期只看 opid。
 *
 * 失败路径：非法指令/非法操作数直接 abort（p0：不给控制流兜底）；语义失败
 * （存储缺失、类型不符）写 vthread 的 `‥status`/`‥msg` 后返回。
 */

#include <stdint.h>

#include <kvlang/binir.h>

#ifdef __cplusplus
extern "C" {
#endif

struct runtimec_runtime;

/* 一条指令的已解析视图。 */
typedef struct binir_instruction {
    uint16_t opcode;              /* 本指令的 opid（组的入口按它再分派） */
    uint16_t kvpc_adder;          /* 执行完后 pcs0 的增量（o0 恒为 1） */
    uint32_t payload_count;       /* 紧随 op 字的 payload 格数（M） */
    const binir_operand_t *payloads; /* M 个 8 字节 payload，语义由 opcode 决定 */
} binir_instruction_t;

typedef void (*binir_handler_t)(struct runtimec_runtime *runtime,
                                  const binir_instruction_t *instruction);

/* 生成的 opid → 组 handler 表；下标就是 opid，0 号不用。
 * 条数由生成物定义，不在头里手写。 */
extern const binir_handler_t binir_handler_table[];
extern const uint32_t binir_handler_count;

void runtimec_binir_arithmetic_execute(struct runtimec_runtime *runtime,
                                         const binir_instruction_t *instruction);
void runtimec_binir_unary_execute(struct runtimec_runtime *runtime,
                                    const binir_instruction_t *instruction);
void runtimec_binir_integer_execute(struct runtimec_runtime *runtime,
                                      const binir_instruction_t *instruction);
void runtimec_binir_equality_execute(struct runtimec_runtime *runtime,
                                       const binir_instruction_t *instruction);
void runtimec_binir_order_execute(struct runtimec_runtime *runtime,
                                    const binir_instruction_t *instruction);
void runtimec_binir_logic_execute(struct runtimec_runtime *runtime,
                                    const binir_instruction_t *instruction);
void runtimec_binir_cast_execute(struct runtimec_runtime *runtime,
                                   const binir_instruction_t *instruction);
void runtimec_binir_memory_execute(struct runtimec_runtime *runtime,
                                     const binir_instruction_t *instruction);
void runtimec_binir_control_execute(struct runtimec_runtime *runtime,
                                      const binir_instruction_t *instruction);
void runtimec_binir_string_execute(struct runtimec_runtime *runtime,
                                     const binir_instruction_t *instruction);
void runtimec_binir_storage_execute(struct runtimec_runtime *runtime,
                                      const binir_instruction_t *instruction);
void runtimec_binir_key_execute(struct runtimec_runtime *runtime,
                                  const binir_instruction_t *instruction);
void runtimec_binir_time_execute(struct runtimec_runtime *runtime,
                                   const binir_instruction_t *instruction);
void runtimec_binir_bool_execute(struct runtimec_runtime *runtime,
                                   const binir_instruction_t *instruction);

#ifdef __cplusplus
}
#endif

#endif
