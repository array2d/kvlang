# 删除 xvalue head 全量解码，改为偏移直取

## 一、目标

消除 `kvspaceDecodeHead` 每次调用的 328 字节结构体填充与 langtype 拷贝，改为：

- `body = data + (1 << data[0])` —— 一步移位得偏移
- `langtype = data + 18` —— 原位引用，0-copy
- 定长标量按**指令已知类型**直接强转读取

不追求「更少的 KV 往返」（那是寻址层的事），只消除「取到一个值之后的解析开销」。

## 二、现状

`kvspaceDecodeHead`（实现于 kvspace-c `src/durable_abi.c:413`）每次调用做四件事：

| # | 动作 | 代价 |
|---|---|---|
| 1 | `memset(out, 0, 328)` | 固定 328B |
| 2 | `kvspaceXhDecode` 校验 pow / `total == data_len` / kind 位合法性 | 分支 |
| 3 | 按 class 推导 `content_len`（见下表） | 查表 / 读 a,b |
| 4 | `memcpy(out->langtype, …)` ≤110B + `parse_dims` 填 `dims[8]` | ≤110B 拷贝 |

`content_len` 的推导规则（**不能简化为「读 a」**）：

| class | 语义 | content_len |
|---|---|---|
| 0 | 短定长（标量 / 目录 / map 头） | `short_width(langtype)` 查表 |
| 1 | 有冗余的长度（字符串 / rwir / 代码类） | `a` |
| 2 | 长定长（tensor） | `a * b`（numel × 元素宽） |
| 3 | 扩展世界 @ext | `a` |

kvlang 侧调用面 **35 处**：`runtime/src/` 30、`layout/src/ffi.rs` 3、`runtime-rs/src/` 5。

已经存在的 0-copy 地基：`kvlangXvalue_t.borrowed` 借用标志、`kvspaceGet` 的借用读约定
（「*out 指向后端常驻/回收空间，调用方不得 free」）、`kvspaceReadReset` 的指令边界回收池。

**现成反例**：`runtime_internal.h:147` 的 `kvlangXvalueNone` 已经裸偏移快判
（`len==32 && data[0]==5 && data[1]==0 && data[18]==0`），却在末尾又调一次
`kvspaceDecodeHead` 兜底 —— 而 `data[18]==0` 本身即「langtype 长度 0」，已充分。

## 三、方案

### 3.1 零解码访问器（`runtime_internal.h`，全 inline）

```c
#define XH_PREFIX 18u

static inline const uint8_t *xh_body(const uint8_t *d) { return d + (1u << d[0]); }
static inline const char    *xh_langtype(const uint8_t *d) { return (const char *)(d + XH_PREFIX); }
static inline uint8_t  xh_class(const uint8_t *d)  { return d[1] & 3u; }
static inline bool     xh_is_ptr(const uint8_t *d) { return (d[1] & 4u) != 0; }
static inline bool     xh_is_none(const uint8_t *d) { return d[1] == 0 && d[XH_PREFIX] == 0; }
static inline uint64_t xh_a(const uint8_t *d);   /* rd64(d + 2)  */
static inline uint64_t xh_b(const uint8_t *d);   /* rd64(d + 10) */
```

`xh_langtype` 返回**指向借用 buffer 的指针**，不拷贝。

### 3.2 长度推导（替代 `content_len`）

class 0 的宽度按调用方已知类型直接给常量（`int64`→8、`float64`→8、`bool`→1…），
不再查表；其余 class 直接读 a / a*b。

### 3.3 指针强转

定长标量（class 0）：

```c
int64_t v = *(const int64_t *)xh_body(d);
```

### 3.4 改造分类（35 处）

| 类 | 特征 | 改法 |
|---|---|---|
| A | 只要 body / len | 换 `xh_body` + 3.2 推导 |
| B | 要 langtype 串 | 换 `xh_langtype`，删 `memcpy` |
| C | 要 dims | 仍需 `kvlangLangtypeParse`；该函数已接受裸指针，无需 head |

`kvlangXvalueNone` 直接删掉尾部 decode 兜底。

## 四、硬约束（实现前必须先验证）

1. ~~**body 对齐**~~ —— **已验证通过（2026-10-04，本机 arm64）**。
   探针 `/tmp/kvlang030/probe.c` 在三后端各写读 3 个 int64：

   | 后端 | `data` | headlen | `body` mod 8 | 强转结果 |
   |---|---|---|---|---|
   | shm | mod8=0（mod16 时 8） | 32 | **0** | 正确 |
   | fs | mod16=0 | 32 | **0** | 正确 |
   | redis | mod16=0 | 32 | **0** | 正确 |

   结论：**body 恒 8 字节对齐**，`*(const int64_t*)` 安全。
   注意 body **不保证 16 字节对齐** —— 将来做 NEON/SSE 批量加载需另议。
2. **校验去向** —— decode 现在做长度精确匹配、pow 合法性与溢出检查。删除后消失。
   需明确：全删（"遇错即崩"）还是保留最小子集。
3. **FS/Redis 后端** —— 借用指针寿命受「指令边界回收池」约束，跨指令/跨帧仍须
   `Materialize`。现有约定不变，本方案不放松它。
4. **langtype 原位引用** —— 同 3，指针同样受借用池约束。

## 五、不做什么

- 不改 kvspace-c（`kvspaceDecodeHead` 保留，供 layout / runtime-rs / 外部 ABI 用）
- 不改 layout 的指令编码（langtype 原位可读，比把类型编进槽更省）
- 不动寻址层（每步 KV 往返次数不变）

## 六、验证

1. 对齐探针（先做）：打印 SHM / FS / Redis 三后端取回的 `data` 基址与 `(1<<pow)` 偏移，确认 `body` 8 字节对齐
2. `tutorial/test.py` 三后端
3. `benchmark/run.py` 与 `results-v0.3.0-*.csv` 逐点对比
4. 新增 head 边界用例：None / 标量 / 字符串 / tensor / ptr 各一

## 七、风险

- 对齐不成立 → 方案退化为「先拷贝到对齐缓冲」，收益大减
- class 0 宽度若调用方判断错误 → 静默读错（本项目「遇错即崩」哲学下可接受，但需在 spec 落条款）
