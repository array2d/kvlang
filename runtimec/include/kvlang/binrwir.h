#ifndef BINRWIR_BINRWIR_H
#define BINRWIR_BINRWIR_H

/*
 * 组件：binrwir
 * 作用：v9 的指令表示——**一格 8 字节**，以及执行器调度它的单位 **BinRWIRGroup**。
 *
 * pc 的变化（下一条走哪、跳多远）**由编译器编码在格序列内部**：执行器不推导
 * 控制流，只按 op 字里的 next_distance 走。所以「一条指令之后去哪」是编译产物，
 * 不是运行期计算。
 *
 * BinRWIRGroup 是调度与提交的单位：一组格在一次状态门与一次 `‥pc` 提交之间执行。
 * 组内的格连续（同一份 `.bin` 的相邻格），组的边界由编译器按块可重放条件给出。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BINRWIR_CELL_SIZE 8U

/* 外部 rwir 的 opcode 标记（12 位保留区顶值）。opid 上限因此是 0xFFE。 */
#define BINRWIR_OPCODE_FOREIGN UINT16_C(0x0FFF)

/*
 * 一格：op 字。
 *
 * 位布局（639）：`opcode12 | read_count3 | write_count2 | next_distance16 | 保留31`，
 * 位序按小端从低位排。操作数字与 op 字同宽，但语义不同——它的 64 位整体装一个
 * 地址，按 `binrwir_operand_t` 读，不经过本结构。
 *
 * `opcode` 12 位：当前 1845 条，上限 4094（0xFFF 留给 FOREIGN 标记），约 2.2 倍余量。
 *
 * `read_count`/`write_count` 按实测收窄：1845 条里读最多 3（`C·memcmp`、`branch`）、
 * 写最多 1。3 位给读留到 7，2 位给写留到 3。
 *
 * `next_distance` 16 位：上限 65535 格（512 KB 跨度）。8 位（255 格）不够——
 * cell0 的 goto 要跳过字面量区，稍大的函数就会顶破。
 *
 * 位域布局由编译器决定，本项目只支持 GCC/Clang 的小端目标；`_Static_assert`
 * 把「8 字节」钉死在编译期。
 */
typedef struct binrwir {
    uint64_t opcode : 12;
    uint64_t read_count : 3;
    uint64_t write_count : 2;
    uint64_t next_distance : 16;
    uint64_t reserved : 31;
} binrwir_t;

_Static_assert(sizeof(binrwir_t) == BINRWIR_CELL_SIZE,
               "binrwir cells are exactly 8 bytes");

/*
 * 一组连续的格：执行器以它为调度单位。
 *
 * `cells` 借用 `.bin` 在存储映射上的 body——不拷贝；借用有效期 = 该 key 不被
 * 覆盖/删除且句柄不关闭。
 */
typedef struct binrwir_group {
    /*
     * **必须 8 字节对齐**：`.bin` 的 body = XValue 起点 + 16，slotsboxmalloc 按
     * 8 字节 slot 粒度分配、基址页对齐，因此 body 对齐。执行器按 `binrwir_t` 读格，
     * 对齐是借用契约的一部分。
     */
    const binrwir_t *cells;
    uint32_t cell_count;   /* 组的格数 */
    uint32_t entry_cell;   /* 进入本组时的格下标（相对 cells 起点） */
} binrwir_group_t;

_Static_assert(sizeof(binrwir_group_t) % BINRWIR_CELL_SIZE == 0,
               "binrwir groups stay 8-byte sized");

/*
 * 操作数字：64 位整体装一个地址。
 *
 * 语义上是**二进制指针**：只是进程内地址，不带元素类型，也**不写 `*`**——`*T` 是 kvlang
 * 指针（key 路径），二进制指针 kvlang 源码不表达。指令集声明（`runtimec_v9_binrwir.kv`）
 * 只写所指值的字节宽度（`uint8` / `[N]uint8` / `[]uint8`）。读写参风格保留，类型信息在
 * v9 编译时已经消化进 opcode，格本身没有 langtype。
 *
 * 地址由 **`resolveprocessaddress`** 在 v9 载入/编译期把一个 key 解析出来，写进格序列；
 * 执行期直接访存，不再解析。**这个 64 位地址不提供给 rwir / kvlang 上层**——明文层只认
 * key、langtype 与值，一个进程内偏移量对它没有意义。
 */
typedef uint64_t binrwir_operand_t;

_Static_assert(sizeof(binrwir_operand_t) == BINRWIR_CELL_SIZE,
               "binrwir operands are exactly 8 bytes");

#ifdef __cplusplus
}
#endif

#endif
