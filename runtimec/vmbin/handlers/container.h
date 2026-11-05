#ifndef VMBIN_HANDLERS_CONTAINER_H
#define VMBIN_HANDLERS_CONTAINER_H

/*
 * 组件：vmbin/handlers/container
 * 作用：容器算子的 handler 表——compactarray / scatterarray / map。
 * 联系：由 engine/session 的装配流程取走条目并装进 `binrwir_registry_t`；
 *       值经 `vmbin/value_access` 按地址读写，与 core 规则共用同一条数据面。
 * 约束：
 *   - 每个参数是**一个具体类型**（vmkv 在编码期已把 union 掩码收敛掉），因此
 *     本模块**不出现 union 分支、不按元素 kind 分叉**：元素宽从声明 langtype
 *     里算出一个数，下标算术对任何 kind 都是同一条 `基址 + i × 宽`。
 *   - 落点归 vmbin 而不是 binrwir：handler 是实现（执行侧），registry 契约归
 *     binrwir。分层规则 `ALLOWED_DEPENDENCIES["binrwir"]` 不含 vmbin，反向边
 *     不存在。
 *   - 条目数组是静态存储，生命周期是整个进程；调用方不得释放。
 *
 * 本批实现（opid / 名字 / catalog 行 / 分组）：
 *
 *   创建（造新 wire 写目标 key；变长分配归存储层）
 *     0x0101 compactarray·newslice       rwir_catalog.def:359  裁剪造新 compact
 *     0x0102 compactarray·new            rwir_catalog.def:366  元素连续打包
 *     0x0103 compactarray·fill           rwir_catalog.def:373  按声明类型定长填充
 *     0x0109 compactarray·newreshape     rwir_catalog.def:415  换 dims，body 原样
 *     0x010A compactarray·newreinterpret rwir_catalog.def:422  换 langtype，body 原样
 *
 *   观测（只读，不算 body）
 *     0x0104 compactarray·numel          rwir_catalog.def:380
 *
 *   读元素（基址 + i × 宽，算术不分 kind）
 *     0x0107 compactarray·at             rwir_catalog.def:401
 *
 *   同槽 patch（只改 body 那一段，head 与 attr 一个字节不动）
 *     0x0108 compactarray·set            rwir_catalog.def:408
 *
 * **每条的 handler 是否 ≤15 行**：`at`/`set`/`numel` 都在 15 行上下（含参数校验），
 * 主体是一句算术。`new`/`newslice`/`newreshape`/`newreinterpret`/`fill` 超过 15 行，
 * 但**没有一条按 langtype 分叉**——超出的行数全在「校验 + 组 wire + 写存储」这三件
 * 与元素种类无关的事上；按 kind 走的只有 `container_langtype_parse` 里那张宽度表，
 * 且它返回的是**一个数**（元素字节宽），不是分支。
 */

#include <stddef.h>
#include <stdint.h>

#include "contract/status.h"
#include "contract/view.h"

#include "binrwir/registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 本模块的失败细节码（`binrwir_execution_exit_t.detail_code`）。
 *
 * **为什么在这里而不是 .c 里**：结构化退出只有码、没有文本通道
 * （`execution_exit.h` 的 `_Static_assert(...==24U, "only control state")`），
 * 所以「为什么失败」只能靠码表达。码一旦发出去就会出现在调用方的
 * `detail_code` 里，必须有**一个**集中可查的定义点——藏在 .c 里等于没人查得到，
 * 测试也没法引用具体值做断言。这也与 `vmbin/interpreter.h` 里
 * `vmbin_interpreter_diagnostic_t` 的做法一致。
 *
 * **编号只增不改**：已发布的码会出现在下游的 `detail_code` 里，插中间或复用
 * 一个编号都会让判据错位。本模块占 0x4300 段。
 */
typedef enum runtimec_container_diagnostic {
    RUNTIMEC_CONTAINER_DIAG_NONE = 0,
    /*
     * `compactarray·new` 的 body 超过定长缓冲 `CONTAINER_NEW_BODY_CAPACITY`
     * （512 字节，no-malloc 基线）。
     *
     * 伴随的状态码是 `OUT_OF_MEMORY`，但**它不是内存不足**：没有分配失败，
     * 是本模块的栈缓冲装不下。两者下一步动作完全不同——看到本码去查缓冲上限，
     * 看到裸 `OUT_OF_MEMORY` 才去查分配器。正解是流式写或让存储层分配，
     * 属下一批。
     */
    RUNTIMEC_CONTAINER_DIAG_NEW_BODY_EXCEEDS_BUFFER = 0x4301,
    /* 声明 langtype 解析不出元素宽（形状非法或基种类不认识）。 */
    RUNTIMEC_CONTAINER_DIAG_UNPARSEABLE_LANGTYPE = 0x4302,
    /* 下标越界或为负（判据是长度，与元素类型无关）。 */
    RUNTIMEC_CONTAINER_DIAG_INDEX_OUT_OF_RANGE = 0x4303,
    /*
     * 待写入的值字节宽与元素宽不符。
     *
     * 与 `INDEX_OUT_OF_RANGE` **分开**：两者动作相同（都是拒绝这次写），但成因不同
     * ——一个是位置不存在，一个是值的形状不对。共用一个码会让「看到 0x4303」
     * 的人分不清该查下标还是查类型。
     */
    RUNTIMEC_CONTAINER_DIAG_VALUE_WIDTH_MISMATCH = 0x4304,
    /*
     * 散列容器有**洞**：成员序列中间缺坐标。跨形态搬运要求源能完整填满目标，
     * 洞让「成员数」与「坐标空间」分离，填零是发明数据、跳过是自洽的谎言。
     */
    RUNTIMEC_CONTAINER_DIAG_SCATTER_HAS_HOLE = 0x4306,
    /*
     * 源成员数与目标声明元素数不符。
     *
     * 与 `HAS_HOLE` **分开**：洞是「序列中间缺」，数量不符是「总数不对」——
     * 修法不同（前者补成员，后者改声明的 N）。
     */
    RUNTIMEC_CONTAINER_DIAG_ELEMENT_COUNT_MISMATCH = 0x4307,
    /*
     * `newreshape` 的新旧元素总数不守恒。
     *
     * `[4]int64 → [3,3]int64`：4 ≠ 9，body 长度装不下。与 `VALUE_WIDTH_MISMATCH`
     * 分开——那个是「单个值的宽不对」，这个是「整体元素数变了」。
     */
    RUNTIMEC_CONTAINER_DIAG_RESHAPE_SIZE_MISMATCH = 0x4305
} runtimec_container_diagnostic_t;

/*
 * 一个 compact 数组的声明 langtype 解析结果：基种类 + 元素字节宽 + 维数。
 *
 * 导出给 `scatter`：`scatterarray·newscatter` 要把 compact body 按元素宽切开，
 * 它需要同一个解析。**宽度表只有一份**——放在这里而不是各模块各写一张。
 */
typedef struct runtimec_container_langtype {
    /* 基种类（`int64`、`char/utf8`…），指向声明文本内部，不拷贝。 */
    runtimec_string_view_t base;
    uint32_t element_size;
    int32_t dimension_count;
} runtimec_container_langtype_t;

/*
 * 声明 langtype → 元素宽与维数。形状是 `[a][b]…kind`：连续维段在方括号里，
 * 基种类在最后一段之后。不认识的基种类返回 UNSUPPORTED——**不猜宽度**，
 * 也不按 body 长度反推。
 */
runtimec_status_t runtimec_container_langtype_parse(
    runtimec_string_view_t langtype, runtimec_container_langtype_t *output);

/*
 * 形状段 `[d1,d2,…]` 的**元素个数**（乘积）。非法形状返回 -1。
 *
 * 导出给 `scatterarray·newcompact`：它要核「源成员数 == 目标声明的 N」，
 * 而 `N` 只能从声明类型里算——那是同一份解析，不该各写一遍。
 */
int64_t runtimec_container_shape_numel(runtimec_string_view_t langtype);

/*
 * 返回本模块的静态条目数组，并把条目数写入 output_count。
 * output_count 为 NULL 时返回 NULL（调用方错用，不猜）。
 */
const binrwir_registry_entry_t *runtimec_container_handler_entries(
    size_t *output_count);

#ifdef __cplusplus
}
#endif

#endif
