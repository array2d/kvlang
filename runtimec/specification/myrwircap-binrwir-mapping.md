# myrwircap to addressed BinRWIR mapping

本规范定义 runtimec 从 `myrwircaps` 与 kvspace 明文 RWIR 进入 canonical BinRWIR 的唯一
映射。mapping 是进程内编译合同，不是持久化格式，也不是执行器派发表。

## 1. Authority chain

```text
five-language behavior decision
    -> one capability row (semantic operator, union masks, constraint, result rule)
    -> one alias record: surface operation name -> capability row
    -> RWIR operation name and typed named parameters
    -> one canonical opid and stable argidx values
    -> host dereferences static keys to nonzero uint64 XValue base addresses
    -> vmkv alone validates the fixed 16-byte head and derives argaddr = xvalue_address + 16
    -> addressed canonical BinRWIR
```

前一层不完整时，后一层不得产生输出。

## 2. RWIR source format

RWIR 是 kvspace 中的明文、可恢复存储格式。每条调用至少表达：

- operation name；
- 每个参数的类型；
- 每个参数的名称；
- 每个参数的 Read 或 Write 方向；
- 参数名称对应的 kvspace key。

这些明文字段是重建 BinRWIR 的权威输入。它们不复制进热 BinRWIR instruction。

## 3. Required mapping record

映射分两层，粒度不同：

| 层 | 文件 | 数量 | 粒度 |
|---|---|---|---|
| 能力表 | `rwir_catalog.def` | 131 行 | **一个语义算子一行**，操作数类型是 union |
| 别名表 | `rwir_alias_table.def` | 316 条 | **一个注册 surface 名字一条**，指向能力行 |

**三个「名字」不是一回事**（层次规则，勿混）：rwir 侧唯一标识是 **operator key 文本**
（如 `kvspace·get`）；`Stable operation identifier` 是 runtimec 代码里引用的 C 符号名；
`Canonical BinRWIR opid` 是二进制编码 id，**runtimec 私有**，不跨实现、不进 rwir 明文。

能力行至少包含：

| Field | Requirement |
|---|---|
| Stable operation identifier | 显式分配，长期稳定，不依赖任何容器位置 |
| Semantic operator name | 语义算子的规范名；它与任何 surface 名字都不必然相同 |
| Canonical BinRWIR `opid` | 显式分配；热 instruction 中唯一保留的 operation identity |
| Read union mask | TypeSet 位集：该能力接受的读操作数类型集合；`0` 表示该行不在标量 union 通道 |
| Write union mask | TypeSet 位集：该能力可能产生的写结果类型集合；`0` 语义同上 |
| Constraint | `NONE`、`SAME_CLASS`、`NUMERIC_COMPATIBLE`、`NUMERIC_COMPARE`：操作数之间的关系 |
| Result rule | `SAME_FIRST`、`SAME_SECOND`、`WIDEN`、`WIDEN_INTEGER`、`BOOLEAN`、`FLOAT64`、`TARGET_TYPE`、`IDENTITY`、`CHARACTER_UTF8`、`NONE` |
| Alias policy | `FORBIDDEN` 或 `DECLARED`：不同 `argidx` 是否允许指向同一 body 地址 |
| Behavior decisions | 一个或多个已存在的稳定 decision；第一个是该行的主决策 |

掩码为 `0` 的行不复制参数签名：参数与结果类型由 `/lib/<operator>` 的 `def rwir` 槽在
安装时声明，那是唯一签名来源。`ResultRule IDENTITY` 表示结果类型取该安装签名，不是
wildcard。`control` 行不产生值。掩码位 0–18 之外的任何位都是 schema 错误。

别名表的每条记录只做 `surface name → 能力行` 的解析，不携带类型，也不构成第二条签名
来源。`concat`、`streq`、`ptreq`、`bool·eq` 没有注册 surface 名字，由编译器按操作数
静态类型选择；别名表用 `RUNTIMEC_RWIR_SPECIALIZATION` 声明这条改选规则。

record 是 operation 与 parameter identity 的唯一来源。生成表的位置不是 identity。

## 4. Canonical binary shape

vmkv 对一次已确定调用生成的热二进制语义形状只有：

```text
opid
  argidx + uint64 argaddr
  argidx + uint64 argaddr
  ...
```

- `opid` 取代 RWIR operation name。
- `argidx` 取代热路径中的参数类型、参数名和方向字段；消费者按 `(opid, argidx)` 查询
  canonical signature。
- 参数 key 解引用得到的是 64 位 XValue 基址，不是任意 `V.body` 地址。
- XValue head ABI 固定为 16 字节；只有 vmkv 执行带溢出检查的
  `argaddr = xvalue_address + 16`，因此 `argaddr` 直接指向 kvspace `V.body`。
- BinRWIR 不保存 operation name、参数类型、参数名、key 文本、xvalue 或 body 副本。
- RWIR 参数数量可以不固定；一次具体调用必须编码全部参数对，不能截断或拆分。

公共二进制布局固定为：instruction header 共 2 字节且只含 `opid`；parameter record
在偏移 0 保存 32 位 `argidx`，偏移 4 是必须为零的 32 位地址对齐填充，偏移 8 保存
64 位 `argaddr`。填充不是语义字段。Read/Write span、source logical program counter 与
可变记录 framing 是 Program 私有元数据，只通过非二进制 view 暴露，不得成为
`binrwir_instruction_t` 的字段。

BinRWIR 是进程内派生物。进程重启、body relocation 或完整 `snapshot.source_revision` 变化后，
旧 Program 立即无效，必须从 kvspace 中的 RWIR 与当前 body 地址重新生成。

## 5. Exact mapping rules

1. **Total**：runtimec `myrwircaps` 中的每个注册 surface 名字都必须在别名表中有且只有
   一条记录，且其像必须是一条能力行。
2. **Single-valued**：一个 capability row 只能产生一个 canonical `opid`；一个 surface
   名字只能解析到一条能力行。
3. **Unique identity**：stable RWIR identifier 与 canonical `opid` 都不得重复。
4. **Stable argument identity**：一个 signature 内每个参数只能有一个 `argidx`，重排其它
   manifest 记录不能改变它。
5. **Signature preserving**：Read/Write 数量、顺序、类型约束和 alias policy 必须等价。
6. **Behavior preserving**：canonical operation 必须引用同一个行为决策，不得近似替换。
7. **Address derivation**：host 只返回非零 64 位 XValue 基址，不能选择 body 偏移；vmkv
   唯一验证固定 16 字节 head ABI，并以溢出检查执行 `xvalue_address + 16`。
8. **Address validity**：每个 emitted `argaddr` 必须非零、直接指向 `V.body`，并在
   compilation 租约期间有效。
9. **Body uniqueness**：地址只定位 kvspace 中唯一权威的 `V.body`；任何层都不得复制 body
   作为 BinRWIR 状态。
10. **No fallback**：旧名称、兼容 identifier、自动名称归一化、零地址和延迟间接 key
   lookup 都不建立映射。

已删除 identifier 永久保留为不可复用。语义发生不兼容变化时必须分配新 identifier，并
更新 manifest version；不能在旧 identifier 下静默改变语义。

## 6. Address lifetime scope

当前 BinRWIR 只接受可证明地址生命周期长于 compilation 租约的静态 key：

- vthread `pc`；
- vthread `status`；
- `/lib` 下函数源等长期存在的 key/xvalue body。

host 对 key 解引用只返回 `uint64 xvalue_address`。vmkv 验证 XValue head 固定为 16 字节，
检查无符号地址加法不会溢出，再生成最终 `uint64 argaddr`。BinRWIR 只保存该最终地址，
vmbin 只消费它；两者都不缓存 key 对象、XValue 或 `V.body`。vmbin 每次使用参数时都从
`argaddr` 读取当前 body；写参数必须写回同一地址。

map、struct 等能力可由运行时数据动态拼接 key。目标 key 可能尚未分配，已解析地址也可能
因 key/xvalue 生命周期变化而失效。在生命周期 ABI 明确定义 pin、generation、relocation
和 invalidation 以前，这些参数必须返回 `RUNTIMEC_STATUS_UNSUPPORTED`，并且不产生
canonical Program 或 compilation。JIT 不能绕过该限制。

## 7. vmkv obligations

vmkv 对每条 RWIR instruction 执行以下固定流程：

1. 读取明文 operation name 与全部 typed, named Read/Write parameters。
2. 经别名表把 operation name 解析到唯一能力行，取得 `opid` 与 union 掩码。
3. 把每个实参的具体 langtype 收敛到该行的读写掩码内；掩码为 `0` 的行改为校验
   `/lib/<operator>` 安装签名。超出掩码、或掩码均值无法唯一确定结果类型时，编译失败。
4. 验证参数数量、名称、类型、方向和 alias declaration。
5. 为每个参数取得 canonical `argidx`。
6. 请求 host 解引用参数 key；host 只能返回 64 位 XValue 基址，不能返回自行选择偏移的
   body 地址。
7. 验证 XValue 基址非零、生命周期可证明且 XValue head ABI 恰好为 16 字节。
8. 检查 `xvalue_address + 16` 不溢出，并由 vmkv 唯一执行该加法得到直接指向 `V.body` 的
   `uint64 argaddr`。
9. 按 source 参数顺序写入全部 `argidx + argaddr`，并保留 source logical program counter。
10. finalize 前验证每条 instruction、每个参数和每个地址都可追溯到唯一 mapping 与租约。

vmkv 不能进行指令融合、控制流优化、register allocation、机器码生成或 execution target
选择。它也不能为动态 key 生成占位地址、运行时间接查找描述或 fallback Program。

## 8. Read/Write alias rule

Read 与 Write 参数保持不同 `argidx`。若 signature 明确允许 alias，vmkv 可以让二者保存
同一个 `argaddr`：

```text
Read argidx  -> same uint64 argaddr -> one kvspace V.body
Write argidx -> same uint64 argaddr -> one kvspace V.body
```

写入后，后续 Read 必须从该地址观察到新 body。vmbin 不得保留旧 body snapshot；JIT 必须
在当前逻辑指令的 speculative state 内维护 alias dependency。只有该指令完整完成后，
全部 aliased dirty state 才能一起 materialize；未完成状态必须丢弃。未声明 alias 而地址
相同属于 `RUNTIMEC_STATUS_INVALID`。

## 9. VMBIN and JIT boundary

VMBIN 是 address-driven memory executor：

- 不拥有 xvalue cache、body cache 或 software register file；
- Read 从 `argaddr` 指向的 kvspace body 取得当前值；
- 每条逻辑 instruction 完成前，Write 结果必须已位于对应 body；
- 每条 logical instruction completion 都是不可关闭的 recovery/observable boundary；
- dispatch、`opid` 特化和融合不能消除 body 写回、合并完成边界或延迟 checkpoint 语义。

JIT 是唯一允许在当前逻辑指令内部把 body 临时提升到机器寄存器或 spill 并执行 register
allocation 的路径。每条 logical instruction completion 都必须建立 safepoint，且只有两种
互斥动作：

- `COMMIT_COMPLETED_STATE`：仅当指令完整完成时，把该指令全部 completed dirty state
  materialize 到原 `argaddr`；全部写回成功后才能推进 logical program counter/checkpoint。
- `DISCARD_INCOMPLETE_STATE`：guard failure、error、recovery 或 resume 遇到当前指令未完成
  时，丢弃全部 speculative/partial register 与 spill state；不含 materialization entry，
  不写任何 `argaddr`，也不推进 logical program counter/checkpoint。

恢复只从最后一个已提交完成边界的 kvspace body 重建，不能从机器状态恢复。

## 10. Failure classification

以下 mapping 错误必须返回明确编译错误，不得产生 Program：

- capability 缺失、重复或存在歧义；
- stable RWIR identifier、`opid` 或 signature 内 `argidx` 重复；
- operation name、参数名称、类型、方向或 alias policy 不一致；
- `behavior-decision` 缺失或引用不存在；
- identifier 来自数组下标、文件偏移、声明次序或运行时注册次序；
- manifest version 与 Program identity 不一致。

合法 RWIR 使用当前不支持的动态 key 地址模型时返回 `RUNTIMEC_STATUS_UNSUPPORTED`；这
不是 `NOT_COMPILABLE`。`NOT_COMPILABLE` 只表示已经成功生成合法 addressed BinRWIR，
内部 JIT eligibility 判定不适用，因而应建立 VMBIN compilation。

零 XValue 基址、`xvalue_address + 16` 溢出、非 16 字节 XValue head ABI 或无法证明地址
生命周期都必须在 Program 产生前失败封闭。host 返回任意 body 偏移也违反合同；vmkv
不得接受、猜测、修正或建立 fallback。

## 11. Acceptance checks

- 对 `myrwircaps` 的注册名字集合与 `rwir_alias_table.def` 的名字集合做双向相等检查。
- 对 `rwir_alias_table.def` 的像集合与 `rwir_catalog.def` 的 identifier 集合做双向检查：
  差的唯一允许成分是由编译器选择的 `concat`、`streq`、`ptreq`、`bool·eq` 四行。
- 每条能力行的读写掩码只使用位 0–18；掩码为 `0` 的行必须是 parameterized 或 control 行。
- 对 stable RWIR identifier、`opid` 和每个 signature 的 `argidx` 做唯一性检查。
- 每条 generated instruction 只含 `opid` 和完整 `argidx + uint64 argaddr` 参数对。
- host fixture 只返回非零 64 位 XValue 基址，不返回 body 地址或偏移选择。
- vmkv fixture 唯一执行 `argaddr = xvalue_address + 16`；每个结果非零、直接指向 kvspace
  `V.body`，且受 compilation 地址生命周期约束。
- 两个允许 alias 的不同 `argidx` 可指向同一地址，后续 Read 观察先前 Write。
- BinRWIR、execution plan 与 VMBIN 不含 body cache 或 register file。
- 每条 logical instruction completion 都产生不可关闭的 recovery/observable boundary。
- completed-state fixture 只有在全部 dirty body 写回成功后才推进 logical program counter/checkpoint。
- guard failure、error、recovery 与 resume fixture 丢弃未完成 register/spill，且零写回。
- 动态 map/struct key fixture 返回 `RUNTIMEC_STATUS_UNSUPPORTED`，不产生 Program。
- 零 XValue 基址、`+16` 溢出、非 16 字节 head ABI、间接 key fallback 和 JIT 动态 key
  bypass fixture 必须失败。
