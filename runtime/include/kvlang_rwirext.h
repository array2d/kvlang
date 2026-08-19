#pragma once
#include <stdbool.h>
#include <stdint.h>

/* kvlang 扩展 runtime ABI：供第三方语言（Rust/Python/Go）通过 C ABI 嵌入 C
 * runtime， 实现自定义 rwirext（如 term 的 print）。
 *
 * KV 存取（connect/get/set/del/list/mkindex/tlv）不在此——扩展宿主自己连 kvspace
 * ABI （kvspaceConnect/Get/Set/...），把拿到的 kvspace 句柄（void
 * *）传进下列带句柄的函数即可。 本 ABI 只暴露 kvspace 不提供的 runtime
 * 语义：rwir 解码 + resolve + display + PC 推进 + 类型判定。 */

/* 写 /lib/<opcode> = rwir 签名（幂等） */
int kvlang_rwirextRegister(void *kvspace, const char *opcode, int32_t nr,
                         int32_t nw, const char *sig);

/* 从 pc 解码指令；若 opcode ∈ {print,println,cerr}，resolve 全部 reads 并
 * display， 以自身 sep（print 无分隔、println/cerr
 * 空格分隔）连接返回（malloc）； 非己方指令返回 NULL（调用方应停止 RunSeq）。
 * rawnl/cerr 输出该指令的换行/流属性：print→rawnl=1（不换行，stdout）；
 * println→rawnl=0（换行，stdout）；cerr→rawnl=0,cerr=1（换行，stderr）。 */
char *kvlang_rwirextPrintLine(void *kvspace, const char *pc, int *rawnl,
                            int *cerr);

/* 当前指令的下一条 PC（malloc） */
char *kvlang_rwirextNextPc(const char *pc);

/* 解码指令，返回 opcode + 读参名 + 写参名（\n 分隔：首行 opcode，接 nr
 * 行读参名，接 nw 行写参名，malloc）。 供 numpy/tensor 扩展按路径零拷贝读 raw
 * 数据。 */
char *kvlang_rwirextParams(void *kvspace, const char *pc);

/* 解析读参 idx 为字符串（变量 → 帧槽值；路径 → 该路径下的值）。 */
char *kvlang_rwirextResolveRead(void *kvspace, const char *pc, int idx);

/* 解析读参 idx 为 KV 路径（变量 → 帧槽路径；路径 → 直接返回；字面量 → ""）。
 * 供 numpy/tensor 扩展按路径零拷贝读整块 ndarray raw 数据。 */
char *kvlang_rwirextResolveReadPath(void *kvspace, const char *pc, int idx);

/* 解析写参 idx 为 KV 路径（路径 → 直接返回；变量 → 帧槽路径）。 */
char *kvlang_rwirextResolveWrite(void *kvspace, const char *pc, int idx);

/* 签名类型表达式（runtime篇-07）——供扩展做实参类型判定。 */
/* 语法校验：type = atom("|"atom)*, atom = [dims](family|kind),
 * dims="[]"|"["dim(","dim)*"]", dim=int|"?"。 */
bool kvlang_rwirextTypeValid(const char *expr);
/* 值判定：kind 为实际落盘 kind 串，ndim 为秩（标量 0），dims 为各维长（标量传
 * NULL）。 */
bool kvlang_rwirextTypeMatch(const char *expr, const char *kind, int32_t ndim,
                           const int32_t *dims);
