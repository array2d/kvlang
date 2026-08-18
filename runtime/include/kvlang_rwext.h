#pragma once
#include <stdint.h>
#include <stdbool.h>

/* kvlang 扩展 runtime ABI：供第三方语言（Rust/Python/Go）通过 C ABI 嵌入 C runtime，
 * 实现自定义 rwirext（如 term 的 print）。opaque handle + C 字符串，不暴露内部结构。 */

typedef struct rwext_conn rwext_conn;

rwext_conn *rwext_connect(const char *dsn);
void rwext_disconnect(rwext_conn *c);

/* 写 /lib/<opcode> = rwir 签名（幂等） */
int rwext_register(rwext_conn *c, const char *opcode, int32_t nr, int32_t nw, const char *sig);

/* 列目录子项，\n 连接返回（malloc，调用方 free）；空目录返回 "" */
char *rwext_list(rwext_conn *c, const char *prefix);

/* 读 key 的 value_string（malloc）；None 返回 "" */
char *rwext_get(rwext_conn *c, const char *key);

/* 写 char/utf8 值 */
int rwext_set(rwext_conn *c, const char *key, const char *val);

/* 删 key */
int rwext_del(rwext_conn *c, const char *key);

/* 读 key 的原始 TLV 字节（含类型信息，malloc，调用方 free）；None → out=NULL */
int rwext_get_tlv(rwext_conn *c, const char *key, uint8_t **out, uint32_t *out_len);

/* 写原始 TLV 字节 */
int rwext_set_tlv(rwext_conn *c, const char *key, const uint8_t *val, uint32_t val_len);

/* 递归建目录索引 */
int rwext_mkindex(rwext_conn *c, const char *path);

/* 从 pc 解码指令；若 opcode ∈ {print,println,cerr}，resolve 全部 reads 并 display，
 * 以自身 sep（print 无分隔、println/cerr 空格分隔）连接返回（malloc）；
 * 非己方指令返回 NULL（调用方应停止 RunSeq）。
 * rawnl/cerr 输出该指令的换行/流属性：print→rawnl=1（不换行，stdout）；
 * println→rawnl=0（换行，stdout）；cerr→rawnl=0,cerr=1（换行，stderr）。 */
char *rwext_print_line(rwext_conn *c, const char *pc, int *rawnl, int *cerr);

/* 当前指令的下一条 PC（malloc） */
char *rwext_next_pc(const char *pc);

/* 解码指令，返回 opcode + 读参名 + 写参名（\n 分隔：首行 opcode，接 nr 行读参名，接 nw 行写参名，malloc）。
 * 供 numpy/tensor 扩展按路径零拷贝读 raw 数据。 */
char *rwext_params(rwext_conn *c, const char *pc);

/* 解析读参 idx 为字符串（变量 → 帧槽值；路径 → 该路径下的值）。 */
char *rwext_resolve_read(rwext_conn *c, const char *pc, int idx);

/* 解析写参 idx 为 KV 路径（路径 → 直接返回；变量 → 帧槽路径）。 */
char *rwext_resolve_write(rwext_conn *c, const char *pc, int idx);

/* 签名类型表达式（runtime篇-07）——供扩展做实参类型判定。 */
/* 语法校验：type = atom("|"atom)*, atom = [dims](family|kind), dims="[]"|"["dim(","dim)*"]", dim=int|"?"。 */
bool type_expr_valid(const char *expr);
/* 值判定：kind 为实际落盘 kind 串，ndim 为秩（标量 0），dims 为各维长（标量传 NULL）。 */
bool type_expr_match(const char *expr, const char *kind, int32_t ndim, const int32_t *dims);
