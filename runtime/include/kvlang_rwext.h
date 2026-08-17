#pragma once
#include <stdint.h>

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

/* 从 pc 解码指令；若 opcode ∈ {print,println,cerr}，resolve 全部 reads 并 display，
 * 以自身 sep（print 无分隔、println/cerr 空格分隔）连接返回（malloc）；
 * 非己方指令返回 NULL（调用方应停止 RunSeq）。
 * rawnl/cerr 输出该指令的换行/流属性：print→rawnl=1（不换行，stdout）；
 * println→rawnl=0（换行，stdout）；cerr→rawnl=0,cerr=1（换行，stderr）。 */
char *rwext_print_line(rwext_conn *c, const char *pc, int *rawnl, int *cerr);

/* 当前指令的下一条 PC（malloc） */
char *rwext_next_pc(const char *pc);
