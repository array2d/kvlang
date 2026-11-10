#ifndef RUNTIMEC_CONTRACT_DIAGNOSTIC_BAND_H
#define RUNTIMEC_CONTRACT_DIAGNOSTIC_BAND_H

/*
 * 组件：contract/diagnostic_band
 * 作用：`exit.detail_code` 的**全局命名空间**——每个产生方占一个段。
 * 联系：`binrwir/execution_exit.h` 的 `detail_code` 是裸 32 位整数，没有类型信息；
 *       本文件给它一张段表，让「看到 3」能立刻回答「这是谁的 3」。
 * 约束：
 *   - **段值一经分配不得复用、不得重排**——已发出的码会出现在日志与测试里；
 *   - 每个模块的码必须落在自己的段内，由各自的 `_Static_assert` 兜住；
 *   - `0` 恒为「未设置」，**任何失败路径都不得上报 0**。
 *
 * ── 为什么需要它 ──────────────────────────────────────────────────────
 *
 * 在 2026-11-05 之前，五个模块各自从 1 开始编号。于是同一个 `3` 同时意味着
 * 「解释器：指令非法」与「输出算子：值格式非法」，`7` 同时意味着「解释器：值访问」
 * 与「窗口：编译产物非法」。一晚上因此误判五次——每次分析前都得先问
 * **「这是谁的 3」**。
 *
 * 那不是细心能解决的问题，是**命名空间缺失**。段表把它变成一次查表。
 *
 * ── 报告纪律 ──────────────────────────────────────────────────────────
 *
 * 迁移之后，任何报告里的 detail 值**必须带段前缀解读**（`0x4A02`、`0x4103`），
 * 不要只写十进制数字——只写 `3` 等于把这个问题原样还给读报告的人。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 段表（唯一权威）。每段 256 个值，够一个模块用满。
 *
 * 分配原则：按「产生方」分，不按「错误类别」分。同一个模块的不同成因共用一个段，
 * 段内编号各自只增不改——因为**读者先要的是「谁报的」，其次才是「哪一种」**。
 *
 * ── 新增模块必须领段 ──────────────────────────────────────────────────
 *
 * 段表的初版只有九段，是按「我以为有几个模块」划的，不是按实际产生方划的——
 * `host.c` 因此漏在外面，它有一个统一失败出口、27 个调用点、却一个码都不写，
 * 于是三条语料用例报出 `detail=0`（「失败但没原因」）。
 *
 * 所以：**新模块加入时必须在这里领一个段**，并在自己的头/源里加
 * `_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(...))`。漏领段的表现不是编译错，
 * 而是产出一条无法归因的 `0`。
 */
#define RUNTIMEC_DIAGNOSTIC_BAND_EXECUTION_WINDOW UINT32_C(0x4000)
#define RUNTIMEC_DIAGNOSTIC_BAND_INTERPRETER UINT32_C(0x4100)
#define RUNTIMEC_DIAGNOSTIC_BAND_CALL UINT32_C(0x4200)
#define RUNTIMEC_DIAGNOSTIC_BAND_CONTAINER UINT32_C(0x4300)
#define RUNTIMEC_DIAGNOSTIC_BAND_VALUE_ACCESS UINT32_C(0x4400)
#define RUNTIMEC_DIAGNOSTIC_BAND_TEMP_OUTPUT UINT32_C(0x4500)
#define RUNTIMEC_DIAGNOSTIC_BAND_HOST UINT32_C(0x4600)
#define RUNTIMEC_DIAGNOSTIC_BAND_HANDLER_CALL UINT32_C(0x4A00)
#define RUNTIMEC_DIAGNOSTIC_BAND_HANDLER_CALL_TEXT UINT32_C(0x4B00)
#define RUNTIMEC_DIAGNOSTIC_BAND_SCALAR UINT32_C(0x4C00)

/* 一段的容量：低两位十六进制，即 256 个值。 */
#define RUNTIMEC_DIAGNOSTIC_BAND_SIZE UINT32_C(0x0100)

/*
 * 一个码是否落在指定段内。段内编号从 `段基址 + 1` 起——`段基址 + 0` 不用，
 * 让「漏加偏移」这种错也落不进合法区间。
 */
#define RUNTIMEC_DIAGNOSTIC_IN_BAND(code, band)                              \
    ((code) > (band) && (code) < ((band) + RUNTIMEC_DIAGNOSTIC_BAND_SIZE))

#ifdef __cplusplus
}
#endif

#endif
