# 动态 key 在 BinRWIR 与 vmbin 中的支持与实现

本文回答一个问题：**`base·*k`（动态成员键）从源码到执行，经过 layout / vmkv(BinRWIR 编码)
/ vmbin(解释执行) 时，每一步具体发生了什么。** 结论先行：

> **当前实现不支持动态成员键。`·*k` 不是「求值 `k` 后取键」，而是被当成一段普通键文本
> 原样落进符号池**——`m·*k` 就是名为 `m·*k` 的静态成员。
> 写和读都用同一个错误键，所以**同一程序内自洽，跨写法立刻暴露**。这是 p1 禁止的
> 「以奇怪的方式成功」。

## 一、四种「动态」不要混

| 名称 | 形态 | 已实现？ | 归属 |
|---|---|---|---|
| 动态成员键 | `base·*k`，`k` 的值作 memitemkey | **否**（见 §四） | layout → vmkv → vmbin |
| 绝对 key 的运行态类型 | `42 -> /p/a`，写侧携带类型、读侧向目标头取 | 部分（#632） | vmkv / vmbin 值访问 |
| 签名变参 | 末读参 `...`，落主槽 body 的 `dynamic` 字节 | 是 | layout / vmkv |
| 形参引用 | `*[0,±k]`，编码成 PARAMETER_READ/WRITE | 是 | layout / vmkv |

`dynamic` 这个英文词在前两处含义完全不同：签名变参是**元数**（arity）事实，动态成员键是
**键求值**事实。读代码时先分清是哪一种。

## 二、语言层语义（normative，已有）

- `stdlib/kvlang/spec/03-类型系统/15-成员访问.kv`：`base·*k` 是动态键，`*` 令运行时
  解引用/求值 `k`，其值作 memitemkey；显式形式 `kvspace·get(base, k)` / `kvspace·set(base, k, v)`。
- `stdlib/kvlang/spec/05-runtime语义/03-成员访问.kv`：`·*` 与裸 `·name` 的区别就是
  「取 `k` 的值」还是「用字面名 `k`」；键逐字即键侧 formatter 的输出，运行时不归一化。

语言层规则是完整的；缺的是实现。

## 三、表示层：BinRWIR 没有「动态 key」

`runtimec/binrwir/argument.h` 的引用种类只有六种：

```
PARAMETER_READ / PARAMETER_WRITE / LOCAL / STATIC / LITERAL / FUNCTION
```

键只出现在两处，且都是**文本**：

1. `LOCAL` 与 `STATIC` 的 `slot` 是符号池下标，符号池条目 = `名字 + 语言类型`；
   名字就是键的最后一段（LOCAL 相对帧根，STATIC 是绝对路径）。
2. 运行期由 `vmbin` 拼 `<帧路径>/<名字>`（LOCAL，见 `vmbin/frame_target_key.c`）或按
   绝对路径解析（STATIC）。

**没有任何字段表达「这段键要在运行时求值」。** 因此动态成员键要么被 layout 脱糖成别的
算子，要么必须新增引用种类——目前两者都没做。

## 四、实测：`·*k` 被字面化（本树，2026-11-06）

源码 `m:[]char/utf8·int64 = {} ; k = "a" ; 10 -> m·*k ; println(m·*k)`：

layout 落盘（`kvspace list /lib/test/`）：

```
[3,0] rwir   =                 ← 写指令
[3,1] rwir   m·*k   int64      ← 写槽名，原样保留 `·*k`
[3,-1] int64 10
[4,-1] rwir  m·*k   int64      ← 读槽名，同样是字面文本
```

编码后（`runtimec_binrwir_dump`）：

```
[pc 3] opid=0x00000001(3:literal[1]) -> (4:local[3])   ← copy 10
[pc 4] opid=0x00000001(5:local[3]) -> (6:local[0])     ← copy 出去
symbol[3] m·*k : int64                                  ← 名字就是这四个字符
```

运行期：写 `10` 到 key `/vthread/1/[1]/m·*k`，读也从同一 key 读。**`k` 的值从未被求值。**

两行判据把它钉死：

| 用例 | 期望 | 实际 |
|---|---|---|
| `10 -> m·*k ; println(m·*k)` | 10 | 10（**错误键上的自洽**） |
| `10 -> m·*k ; println(m·a)` | 10 | **0**（真正的 `m·a` 没有被写） |

`runtimec/tests/kv/member/dynamic_key.kv` 的最后一行 `m·k == None` 正是为这条判据写的；
它当前先被「非指针判 None」的静态检查拦住（另一条已知缺口），所以这条语料目前报的是
layout 失败，而不是动态键失败——**两个缺陷叠在同一文件上**。

## 五、编码器的现状与边界

`runtimec/vmkv/encoder.c`：

- 键文本**以 `*` 开头** ⇒ `VMKV_ENCODER_DIAG_DYNAMIC_KEY` + `RUNTIMEC_STATUS_UNSUPPORTED`，
  不产生 Program（这是「帧坐标表达式 `*[d,±k]`」一类，不是成员键）。
- 键文本**中间含 `·*`** ⇒ 走「帧内局部名」分支，原样登记符号（**缺陷**：没有识别、没有拒绝）。
- `*[0,±k]` 单独识别为形参引用（`vmkv_encoder_parse_parameter_reference`），与动态键无关。

`myrwircap-binrwir-mapping.md` §10 与 `README.md` 写着「动态 key 必须返回
`RUNTIMEC_STATUS_UNSUPPORTED`」。**这条只对以 `*` 开头的键成立**；含 `·*` 的成员键没有
被拦，直接走了字面名路径。文档与实现由此分叉，且分叉方向是「静默成功」。

## 六、裁决

### 6.1 短期（立即）：失败封闭，不许字面化

vmkv 在登记符号前判定：键文本含完整的 `·` + `*` 两字节序列（U+00B7 后紧跟 `*`）⇒
`VMKV_ENCODER_DIAG_DYNAMIC_KEY` / `UNSUPPORTED`。理由按 p0/p1：静默把 `*k` 当成员名，
会让「键取错」在整条链上不可见；失败封闭至少让它在用例里变成**有名字的能力缺口**
（runner 的 blocked），而不是一条输出错的绿。

判据按完整字节序列匹配，不按首字节（与 `vmkv_rwir_key_is_member` 同一条实现原则）。
只加判据，不改任何已能正确运行的形态。

### 6.2 长期：留在执行期，照 C 的 malloc 堆内存建模；**绝不回 vmkv**

**这条是用户裁决（2026-11-06），推翻本文早先两版落点。**

前辈（`runtimec2`）在 vmkv 层解决，是因为它那一层的 vmkv **带着帧跑**：逐函数构建寄存器
时可以读到键变量当前的值（`runtimec2/src/vmkv.cpp:362` `param_member_key_text`、
`:1142-1185`），于是低层执行器看不到动态键。**runtimec 的 vmkv 不是那个东西**——它是
静态编码器：无帧、无值、Program 落盘后多帧复用。把动态键推回它，等于要求编译期知道
运行期的值，正如要求 `malloc` 的大小在编译期已知。**所以绝不回 vmkv。**

参照 **C 的 malloc 堆内存**建模——动态键就是运行期算出来的**地址值**：

| C | kvlang 动态键 |
|---|---|
| `void *p = malloc(n)`，地址是运行期数据 | 键是运行期求值出来的值（`k` 的当前值） |
| 地址不透明，`*(T*)p` 由**写入方** cast 定类型 | 写侧类型由**写入方携带**（写槽已带 langtype，见 #632） |
| 读 `*(T*)p` 的类型同样来自读方 cast | 读侧向**目标 XValue 头**取类型（#632） |
| `p == NULL` 必须检查；解引用空指针是 UB | 基座/成员不存在 ⇒ **失败封闭**，不是 UB、不是猜 |
| 分配失败返回 NULL，不静默用坏内存 | 键无法格式化（非有限浮点、非文本类型）⇒ 失败封闭 |
| 释放后使用是 UB | 存储被整体重建后旧地址失效 ⇒ 每次访问**现解析**，不缓存地址 |

由此落成三条：

1. **layout 保持源码事实**：`base·*k` 里 `k` 是一个**操作数**，不是拼进键文本；指令携带
   `(base 值, 键值)` 两个普通值参数。这一步在 layout，不在 vmkv。
2. **vmkv 不做任何动态键特判**：它只看到两个普通值参数，照常编码引用（#634 的短期判据
   只拦「已字面化」的键文本，防止再出现 `symbol m·*k` 这种假成功）。
3. **vmbin 在访问点按值拼键**：`<base 键路径>·<键值按 key formatter 的文本>` 再
   `Get`/`SetValue`。这不是「把问题推给执行器」，因为执行器本来就拥有运行期值；这正是
   malloc 的位置——地址在运行期才存在。与 #631（组身份是路径）、#632（写指令创造 key）
   同一条规则。

被否掉的方向（全部留痕）：

- **回 vmkv 解决**：runtimec 的 vmkv 无帧无值，做不到；强行做就是要求编译期知道运行期
  值。
- **给 BinRWIR 加 DYNAMIC 引用种类**：把「待求值键」变成 Program 里的第二套键表示，JIT
  恢复语义要各写一份。不取——键值就是普通操作数，不需要新种类。
- **运行期按 `·*` 文本现场解析**：键文本成了半程序，`*` 的规则会分裂在两处。不取。
- **在 layout 脱糖成 `kvspace·get/set`**：会抹掉「`m·a` 与 `m·*k` 是两种语义」这条源码
  事实。不取。

### 6.3 验收

- 短期：`dynamic_key.kv` 从「输出不符」变成「blocked + 具名诊断」，且任何语料不因此从绿变红；
- 长期（脱糖落地后）：
  `10 -> m·*k ; println(m·*k)` 得 10、`println(m·a)` 得 10、`println(m·k == None)` 得 true；
  同时 `m·k`（静态名）与 `m·*k`（`k="a"`）必须落到不同键。

## 七、地基：把 kvspace ABI 提升为 BinRWIR 原语

用户裁决（2026-11-06）：**kvspace 的很多 ABI 应当加入 BinRWIR**——`keyto64`（key 查 64 位
地址）、`malloc`、`kvspace·new` 等，「他们是支持动态 key 的基础」。

完整设计见 `storage-primitives.md`。要点：`base·*k` 的展开链是
**base 键路径 + `·` + `kvspace·keytext(k 的值)` → `kvspace·resolve` → `load`/`store`**，
都是普通 rwir 原语；解释器因此只需要「按地址访问」一条通用路径，不需要任何动态键分支。

## 八、与其它裁决的关系

- **#631（struct/map 即 xvaluegroup）**：动态键就是组内路径在运行期的拼装；组的身份是
  路径，所以拼装必须在运行期做，不能预先烘焙成静态键。
- **#632（绝对 key 类型）**：写侧携带类型、读侧向目标头取。动态键写的类型同样由写入方
  携带（写槽已带 langtype），读侧取目标头。
- **#633（键编码）**：成员访问键逐字铁律不变；本裁决不改变任何键文本的生成规则，只改变
  「谁来生成它、什么时候生成」。
