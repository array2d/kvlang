#ifndef BINIR_BINIR_H
#define BINIR_BINIR_H

/*
 * 组件：binir
 * 作用：v9 的指令表示——**一格 8 字节**，以及执行器调度它的单位 **BinIRGroup**。
 *
 * 一条指令 = 1 个 op 字 + `payload_count` 个 payload 格；下一条 op 字在
 * `cell + 1 + payload_count`。payload 的**数量、顺序与类型由 opcode 完全确定**
 * （生成表：声明顺序，读在前、写在后）；bin 层不再区分读参与写参。
 * `kvpc_adder` 是执行完这条指令后 pcs0 的增量（o0 恒为 1）。
 *
 * BinIRGroup 是调度与提交的单位：一组格在一次状态门与一次 `‥pc` 提交之间执行。
 * 组内的格连续（同一份 `.bin` 的相邻格），组的边界由编译器按块可重放条件给出。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BINIR_CELL_SIZE 8U

/* 外部 rwir 的 opcode 标记（12 位保留区顶值）。opid 上限因此是 0xFFE。 */
#define BINIR_OPCODE_FOREIGN UINT16_C(0x0FFF)

/*
 * 一格：op 字。
 *
 * 位布局（2026-11-12 设计）：`opcode16 | payload_count24 | kvpc_adder16 | 保留8`，
 * 位序按小端从低位排。payload 格与 op 字同宽，但语义不同——它整体装一个地址，
 * 按 `binir_operand_t` 读，不经过本结构。
 *
 * `opcode` 16 位：当前 1851 条，上限 65535。
 *
 * `payload_count` 24 位：紧随 op 字的 payload 格数 M；同时也是到下一条 op 字的
 * 跨度依据（下一条 op 字 = 当前 + 1 + M）。参数区 byte 数 = 8×M。
 *
 * `kvpc_adder` 16 位：执行完本条后 pcs0 的增量。o0 恒为 1；o1 可能为 0 或更大。
 *
 * `reserved` 8 位：必须为 0。
 *
 * 位域布局由编译器决定，本项目只支持 GCC/Clang 的小端目标；`_Static_assert`
 * 把「8 字节」钉死在编译期。
 */
typedef struct binir {
    uint64_t opcode : 16;//opcode id，虽然目前的数量是 1845，后续可能会压缩。
    uint64_t payload_count : 24;//binir的操作数数量，每个操作数都是8byte。
    uint64_t kvpc_adder : 16;//kvpc的增量，kvpc是kvlang的程序计数器，binir指令执行完后，kvpc会增加这个值。
                             // o0优化模式，这个值通常是1；
                             // 在o1优化模式下，这个值是可能是0，也可能更大，取决于编译器；
    uint64_t reserved : 8;
} binir_t;

_Static_assert(sizeof(binir_t) == BINIR_CELL_SIZE,
               "binir cells are exactly 8 bytes");

/*
 * 一组连续的格：执行器以它为调度单位。
 *
 * `cells` 借用 `.bin` 在存储映射上的 body——不拷贝；借用有效期 = 该 key 不被
 * 覆盖/删除且句柄不关闭。
 */
typedef struct binir_group {
    /*
     * **必须 8 字节对齐**：`.bin` 的 body = XValue 起点 + 16，slotsboxmalloc 按
     * 8 字节 slot 粒度分配、基址页对齐，因此 body 对齐。执行器按 `binir_t` 读格，
     * 对齐是借用契约的一部分。
     */
    const binir_t *cells;
    uint32_t cell_count;   /* 组的格数 */
    uint32_t entry_cell;   /* 进入本组时的格下标（相对 cells 起点） */
} binir_group_t;

_Static_assert(sizeof(binir_group_t) % BINIR_CELL_SIZE == 0,
               "binir groups stay 8-byte sized");

/*
 * 操作数字：64 位整体装一个地址。
 *
 * 语义上是**二进制指针**：只是进程内地址，不带元素类型，也**不写 `*`**——`*T` 是 kvlang
 * 指针（key 路径），二进制指针 kvlang 源码不表达。指令集声明（`runtimec_v9_binir.kv`）
 * 只写所指值的字节宽度（`uint8` / `[N]uint8` / `[]uint8`）。读写参风格保留，类型信息在
 * v9 编译时已经消化进 opcode，格本身没有 langtype。
 *
 * 地址由 **`resolveprocessaddress`** 在 v9 载入/编译期把一个 key 解析出来，写进格序列；
 * 执行期直接访存，不再解析。**这个 64 位地址不提供给 rwir / kvlang 上层**——明文层只认
 * key、langtype 与值，一个进程内偏移量对它没有意义。
 */
typedef uint64_t binir_operand_t;

_Static_assert(sizeof(binir_operand_t) == BINIR_CELL_SIZE,
               "binir operands are exactly 8 bytes");

#ifdef __cplusplus
}
#endif

#endif
