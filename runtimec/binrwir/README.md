# binrwir —— 8 字节格，没有类型，操作数是指向值的二进制指针

## rwir 与 binrwir：一张表分清

| 维度 | **rwir** | **binrwir** |
|---|---|---|
| 定位 | kvlang 执行的核心语义 | runtimec 专属的加速指令 |
| 生产者 | layout | v9 |
| 存储 | kvspace 明文树（`/lib`） | 二进制 `.bin`（`/lib/<pkg>·<func>.bin`） |
| 类型 | 有 langtype：并集 `T1\|T2`、指针 `*T`、容器 | **没有 langtype**；操作数按所指值的字节宽度写：`uint8` / `[N]uint8`（N=2/4/8）/ `[]uint8` |
| 参数风格 | 读写参：`a:T, b:T -> c:T` | **同样的读写参风格** |
| 操作数 | key、值、langtype | 64 位地址（进程内、不透明、不可持久化） |
| 消费者 | kvlang 执行核心语义 | runtimec（解释器 / JIT） |
| 可读性 | 人可读、可从 kvspace 恢复 | 8 字节格，只有 opid 与字 |

**两层的差别只有一条是根：rwir 有类型，binrwir 没有。** 其余差异都从这条长出来——
没有类型，存储就只能是指令字加操作数字；没有类型，操作数就只能是地址（`void`）；
没有类型，精度只能由 opcode 自己带。

## 这一层的唯一形态

**binrwir 没有类型。** 一格 8 字节；操作数字是**二进制指针**（64 位进程地址），不带任何
元素类型。指令集声明（`runtimec_v9_binrwir.kv`）里只写它**所指值的字节宽度**：
`uint8`（1 字节）、`[N]uint8`（N=2、4、8）、`[]uint8`（变长字节）。

**不要写成 `*void`**：`*T` 是 kvlang 指针，以 key 路径表示目标引用，body 是 key 不是
地址；二进制指针 kvlang 源码不表达，也不允许手写（`spec/03-类型系统/16-ptr.kv`）。
op 字里的字段（opcode / read_count / write_count / next_distance）也都是字节位段，
**没有一个字段叫 langtype**。

有类型的是上一层 **rwir**（kvlang 的通用明文指令）：`langtype`、并集 `T1|T2`、指针
`*T`、容器 `[n]T` / `[]T` / `'K'T` 都在那里。v9 编译时把 rwir 的类型全部消化掉，
落到这一层只剩格与地址。

## 边界

| 角色 | 允许做什么 |
|---|---|
| **v9 编译器** | **唯一的产出方**：读 `/lib` 明文 rwir，写 `/lib/<pkg>·<func>.bin` |
| **runtime / 解释器 / JIT** | **唯一的读取方**：按 8 字节格取指、按 `next_distance` 走 |
| **layout** | **永远不参与 binrwir**：不生成、不读取、不嵌入本目录的任何产物 |

## layout 永远不参与 binrwir

layout 的产物是**明文 rwir 树**，到 `/lib` 为止。binrwir 在 layout 之后：由 v9 从明文
编译出来。layout 一旦看见 binrwir，就只有两种可能——把 langtype 带进字节层（破坏
「binrwir 没有类型」），或者把字节格式带进 layout（破坏「layout 只做布局」）。两种都
不允许。

依赖方向因此是单向的一条线：

```
layout →（kvspace 明文 rwir）→ v9 →（binrwir .bin）→ runtime
```

layout 在这条线的最左边，**在 v9 之前结束**。本目录的任何文件都不得出现在 layout 的
include / `include_str!` / 构建输入里。

## 地址解析属于这一层：`resolveprocessaddress`

把 key 解析成**进程内 64 位地址**的动作叫 `resolveprocessaddress`，它**只属于 binrwir**：

- 操作数字 `binrwir_operand_t` 就是一个 64 位地址；执行器拿到它直接访存，不解引用 key、不拼路径；
- 解析发生在 **v9 载入/编译期**：一个 key 只解析一次，结果写进格序列；
- **不提供给 kvlang 上层（rwir）**：rwir 是明文指令层，一个 64 位偏移量对它没有意义——它只认 key、langtype 与值。上层看不到地址，也不需要看到。

```
rwir:  key 表达式 / 值 / langtype
          │  v9 载入期：key·resolveprocessaddress
          ▼
binrwir:  64 位地址（操作数字）
```

这跟「layout 永远不参与 binrwir」是同一条边界的另一面：layout 产出的 rwir 里没有地址；
地址从 v9 开始才存在。

## 产物布局（`.bin` 的 body）

```
cell 0        goto：next_distance = 入口指令格，跳过字面量区
cell 1..n     字面量区：key 文本、常量、立即数的原始字节（变长，按字节铺开）
cell n+1..    指令流：读格 / op 字 / 写格
```

字面量区解决的是「key 字符串长度动态、塞不进 8 字节格」：文本原样铺在
cell 1..n，操作数格引用它，载入期再经 kvspace 解析成 64 位真实地址。
cell 0 的 goto 让取指直接落到第一条指令，不经过数据区。

## 本目录的内容

```
binrwir_t       8 字节 op 字（位段：opcode/read/write/next/reserved）
binrwir_operand_t   64 位地址（二进制指针，不写 `*`）
runtimec_v9_binrwir_*.kv   指令全集，按算子族拆成 14 个文件：一行一条，
                        操作数写所指值的字节宽度
                        8 个特化（算术/一元/整数/相等/大小/逻辑/转换/内存）+
                        6 个逐条（控制/字符串/存储/键族/时间/布尔）+
                        bin 独有：C·memcpy/memmove/memset/memcmp + kvspace·resolve/realloc/free
                        （逐字对齐 `kvspace/include/kvspace/kvspace.h` 的三条存储原语）
runtimec_v9_binrwir_opids.yml   opid 表：从 1 自动递增，runtimec 私有编号；
                        每条带参数名与参数类型（read/write）
generate_v9_binrwir.py     生成上面所有产物：从 ../rwir/runtimec_rwircap.kv 展开类型元组
binrwir_group_t    执行器的调度单位：连续格 + 入口格
handler.h       按组 handler 的入口契约：binrwir_instruction_t + opid → 组 表声明
dispatch_table.c   生成物：opid → 组 handler，下标就是 opid
print.*         把格打成文本（只读字节，不解释类型）
```

操作数类型五种，直接并列：`uint8`（1 字节）、`[N]uint8`（N=2、4、8）、`[]uint8`（变长字节）、
`pcs0`（`[8]uint8`：指向 pc 的 s0 分量的**指针**——s0 逻辑上是 uint32，写 8 字节是因为
操作数是地址，不是因为它本身是 8 字节整数；**对 pcs0 的运算一律用 uint32 类算子**）、
`stack`（`[8]uint8`：帧栈锚点，与 pcs0 同级的常用 key；相对 key 靠它补成绝对位置，
`kvspace·resolve` 的第二个操作数就是它）。
v9 在编译优化前会在每两条 op 之间插入 `pcs0++`，即 `add_uint32_uint32_uint32`；
grouprun 自己不自增，自增是编译产物。

opid 是 **runtimec 私有的内部编号**：从 1 起按产物顺序自动递增（`runtimec_v9_binrwir_opids.yml`），
不跨仓库共享、不进 kvspace。当前 1846 条，opid 1..1846，在 16 位 opcode 内。

## handler 按组放置

实现按算子组落目录，与产物文件一一对应：

```
arithmetic/ unary/ integer/ equality/ order/ logic/ cast/ memory/    特化组
control/ string/ storage/ key/ time/ bool/                           逐条组
```

每组一个 `<组>/handler.c`，实现 `handler.h` 里的 `runtimec_binrwir_<组>_execute`，
在本组 opid 内再分派。解释器只认这 13 个入口，组内怎么分派由该组自己决定。
opid → 组的映射由 `dispatch_table.c` 给出，不手写。

跑一组的是隔壁 `../binrwirgrouprun/`：`binrwirgrouprun(runtime, group)` 从入口格
按 `next_distance` 逐条前进，切好读写格后按上表派发。

rwir 的 `concat`、`copy`、`string·slice` 在 binrwir 里**没有同名指令**：v9 把它们编译成
`C·memcpy`——字符串拼接、整值复制、子串截取都是字节拷贝，不按类型展开特化。
`string·cmp` 同样编译成 `C·memcmp`。

各种 `new`（`compactarray·new`、`struct·new`、`map·new`）同样没有同名指令：
走 `kvspace·realloc`——它是 kvspace.h 的唯一写原语，key 不存在就创建。

容器整体不出现在 binrwir：`compactarray·` 的其余算子由 rwfunc 实现，
没有单条 bin 指令。

容器算子整体不出现在 binrwir。`runtimec_v9_binrwir_container.kv` 暂时保留，
去向待定。

格式定义在 `runtimec/include/kvlang/binrwir.h`；本目录只放实现与工具。
