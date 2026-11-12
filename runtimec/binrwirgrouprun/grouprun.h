#ifndef BINRWIRGROUPRUN_GROUPRUN_H
#define BINRWIRGROUPRUN_GROUPRUN_H

/*
 * 组件：binrwirgrouprun
 * 作用：跑一组 BinRWIR 格（`binrwir_group_t`）。
 *
 * 参数的主体是**组**：连续格 + 入口格。从入口格开始，按 op 字的 next_distance
 * 逐条前进；每条指令的操作数格由 grouprun 切好（读格在 op 字之前、写格在它之后），
 * 交给 opid 对应的组 handler（`runtimec/binrwir/<组>/`）。
 *
 * runtime 只给需要 kvspace 的组透传上下文；纯计算组不看它。
 *
 * pcs0 是 pc 的 s0 分量的**指针**：s0 本身是 uint32，操作数写 `[8]uint8`
 * 是因为它是地址，不是因为它是 8 字节整数。**对 pcs0 的运算一律用 uint32 类算子**——
 * `pcs0++` 就是 `add_uint32_uint32_uint32`。v9 在编译优化前会在每两条 op 之间
 * 插入这条自增，所以 grouprun **自己不自增**，自增是编译产物的一部分。
 */

#include <stdint.h>

#include <kvlang/binrwir.h>

#ifdef __cplusplus
extern "C" {
#endif

struct runtimec_runtime;

/* 非法格（越界、opid 为 0 或超出表、next_distance 为 0）直接 abort（p0）。 */
void binrwirgrouprun(struct runtimec_runtime *runtime,
                     const binrwir_group_t *group);

#ifdef __cplusplus
}
#endif

#endif
