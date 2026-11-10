#ifndef RUNTIMEC_CONTRACT_KEY_ORDER_H
#define RUNTIMEC_CONTRACT_KEY_ORDER_H

/*
 * 组件：contract/key_order
 * 作用：目录子项名的**唯一**比较器。存储层不再排 `ListAt` 的顺序（kvspace 第二阶段
 *       `include/kvspace/storage.h`：`kvspaceList` 按后端顺序返回、不排序），排序
 *       责任上移到调用方；本模块就是那个调用方侧的单一实现。
 * 联系：只依赖自身。`vmkv` / `vmbin` / `cli` 需要按坐标顺序取第 i 个子项时，一律经
 *       本模块，禁止各写一套比较器——两套比较器迟早对「小数坐标 / 字符串坐标 / 目录
 *       尾斜杠」这类边角判不同，症状是「同一个目录，两个算子取到不同的第 i 项」。
 * 约束：
 *   - 比较规则**照抄规范** `stdlib/kvlang/spec/02-kvspace模型/07-数组两种物理形态.kv`
 *     的 `cmp_coord`：坐标段恒排在非坐标段之前；两坐标段**都可**整数解析时按数值
 *     升序，否则按字典序（注意：不是「整数优先」——一边可解析一边不可时走字典序）。
 *   - **必须与后端旧 `ListAt` 逐项一致**。后端 `kvspace-c/src/kvspace.c:child_cmp`
 *     是 `kvspaceCoordCmp(去尾斜杠) ?: strcmp(原名)`：坐标判等时再用**原始名字**
 *     （含尾 `/`）兜底。本模块照此实现，否则切换调用点后同一个目录的顺序会变。
 *   - 目录项名字带尾 `/`（`kvspaceList` 与旧 `ListAt` 一致），比较前剥掉；剥掉之后
 *     的文本才是规范说的「成员名」。
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 名字是否是坐标段：`[` 开头、`]` 结尾、内部无嵌套方括号，且长度 >= 3。
 * **不要求整数**——`[12.5,3]`、`[my,name]` 也算坐标段（规范的结构判定）。 */
int runtimec_key_is_coord(const char *name, size_t name_size);

/* `cmp_coord`：返回 <0 / 0 / >0。传入的名字**不带**尾 `/`；
 * 调用方先用 `runtimec_key_member_name` 剥掉。 */
int runtimec_key_compare_coord(const char *left, size_t left_size,
                               const char *right, size_t right_size);

/* 目录子项的完整比较：`cmp_coord(去尾斜杠)`，判等时退回 `strcmp(原始名字)`。
 * 这是与后端 `child_cmp` 逐项一致的那一个——排序请用它，不要直接用上一行。 */
int runtimec_key_compare_directory_entry(const char *left, size_t left_size,
                                         const char *right, size_t right_size);

/* 目录项名的成员名视图：剥掉尾 `/`（目录项）。不是目录项则原样返回。 */
void runtimec_key_member_name(const char *name, size_t name_size,
                              const char **output_name, size_t *output_size);

#ifdef __cplusplus
}
#endif

#endif
