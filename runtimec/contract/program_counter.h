#ifndef RUNTIMEC_CONTRACT_PROGRAM_COUNTER_H
#define RUNTIMEC_CONTRACT_PROGRAM_COUNTER_H

/*
 * 组件：contract/program_counter
 * 作用：执行位置的值形态。pc 是**两个整数**，不是路径文本——比较、递增、范围
 *       判断全部是整数运算，这是优化的依赖型。checkpoint 与 error 都用它，
 *       所以单独成头，避免 error ↔ checkpoint 的循环依赖。
 * 联系：engine/execution_window 产出，contract/kvspace_access 落存储，读方在 layout。
 * 约束：进程内表示就是这个结构体，不是字符串；线程身份（vid）由存放路径给出，
 *       不进本结构；函数身份由帧的 `‥lib` 给出，也不进。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 程序计数器是**两个整数**，不是路径文本：比较、递增、范围判断全部是整数运算，
 * 这是优化的依赖型（见 `routing/pc-structure.md`）。
 *
 *   stack_identifier           栈帧号，随调用增加；函数身份由该帧的 `‥lib` 给出
 *   source_instruction_ordinal 帧内指令序号 s0，与 `/lib/<func>` 的序号一致
 *
 * `s0 < 0` 是**迁移哨兵**：改 stackid 时要分三步写（哨兵 → stackid → 新 s0），
 * 读方看到负值必须按「pc 正在迁移」重读，**绝不照着执行**。
 *
 * 线程身份（vid）不进本结构——它是路径前缀，由存放位置给出。
 */
typedef struct runtimec_program_counter {
    int32_t stack_identifier;
    int32_t source_instruction_ordinal;
} runtimec_program_counter_t;

/*
 * 两个分量各自的无效值。都取 -1：合法帧号与指令序号都 >= 0，不存在歧义。
 *
 * 三种取值形态（读方必须能区分，不能把无效值当数据）：
 *
 *   {>=0, >=0}                     运行时位置——某帧的第几条指令
 *   {STACK_NONE, >=0}              **源码侧位置**——知道第几条指令，没有帧
 *                                  （vmkv 编译期用它：只知道源码行，不知道运行时帧）
 *   {STACK_NONE, ORDINAL_NONE}     完全没有位置
 *
 * 为什么源码侧不能写 `{0, line}`：0 是一个**合法的帧号**，那等于发明了一个帧，
 * 而不是声明「没有帧信息」。无效值必须是数据域之外的取值。
 */
#define RUNTIMEC_PROGRAM_COUNTER_STACK_IDENTIFIER_NONE INT32_C(-1)
#define RUNTIMEC_PROGRAM_COUNTER_ORDINAL_NONE INT32_C(-1)

/* 「没有位置」的规范值：只在这里定义一次，调用点一律用它，不各写一遍。 */
#define RUNTIMEC_PROGRAM_COUNTER_NONE                                    \
    ((runtimec_program_counter_t){                                       \
        RUNTIMEC_PROGRAM_COUNTER_STACK_IDENTIFIER_NONE,                  \
        RUNTIMEC_PROGRAM_COUNTER_ORDINAL_NONE})

#ifdef __cplusplus
}
#endif

#endif
