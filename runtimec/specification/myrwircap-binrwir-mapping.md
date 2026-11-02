# myrwircap to canonical BinRWIR mapping

本规范定义 runtimec 从 `myrwircaps` 与 RWIR specification 进入 canonical BinRWIR 的
唯一映射。这里的 mapping 是编译合同，不是执行器派发表。

## 1. Authority chain

```text
five-language behavior decision
    -> one myrwircap specification record
    -> one canonical BinRWIR operation identifier
    -> vmkv equivalent emission
    -> BinRWIR Compiler execution plan
```

前一层不完整时，后一层不得产生输出。

## 2. Required record

每个 myrwircap 必须在 `rwir_catalog.def` 中拥有一条且只有一条记录。每条记录至少包含：

| Field | Requirement |
|---|---|
| Stable RWIR operation identifier | 显式分配，长期稳定，不依赖任何容器位置 |
| Canonical capability name | 与 RWIR specification 和 `myrwircaps` 中的名称完全一致 |
| Canonical BinRWIR operation identifier | 显式分配，并写入 BinRWIR instruction 的 `operation_code` |
| Read parameter contract | 参数数量规则、顺序、类型与读取权限 |
| Write parameter contract | 参数数量规则、顺序、类型与写入权限 |
| Alias policy | Read 与 Write 是否允许、要求或禁止绑定同一 canonical Cell |
| Behavior decision | 引用五语言矩阵中已存在的稳定 decision |

record 是 operation identity 的唯一来源。生成器可以建立查找表，但查找表位置不是 identity。

## 3. Exact mapping rules

1. **Total**：runtimec `myrwircaps` 中的每个 capability 都必须有记录。
2. **Single-valued**：一个 capability 只能产生一个 canonical BinRWIR operation。
3. **Unique identity**：stable RWIR identifier 与 canonical BinRWIR identifier 都不得重复。
4. **Order independent**：重排、插入或删除其它记录不能改变既有 identifier。
5. **Signature preserving**：Read/Write 数量、顺序、类型约束和 alias policy 必须等价。
6. **Behavior preserving**：canonical operation 必须引用同一个行为决策，不得近似替换。
7. **No aliases**：旧名称、第二名称、兼容 identifier 与自动名称归一化都不建立映射。

已删除 identifier 永久保留为不可复用。语义发生不兼容变化时必须分配新 identifier，并更新
manifest version；不能在旧 identifier 下静默改变语义。

## 4. vmkv obligations

vmkv 对每条 RWIR instruction 执行以下固定流程：

1. 读取 source operation identity 与 Read/Write operands。
2. 在生成的 specification table 中按稳定 identity 找到唯一 record。
3. 验证 capability name、参数数量、参数方向、类型约束与 alias declaration。
4. 使用 record 指定的 canonical operation identifier 构造 BinRWIR instruction。
5. 保留 source logical program counter，并按 `[ReadReference ...][WriteReference ...]`
   写入完整 reference 分区。
6. finalize 前验证 Program 中每条 instruction 都可追溯到唯一 specification record。

vmkv 不能进行类型固化、KV 寻址优化、融合、虚拟寄存器分配、控制流优化、机器码生成或
execution target 选择。这些工作只属于 BinRWIR Compiler 的 execution plan 阶段。

## 5. Compile errors

下列情况必须立即产生编译错误，不得生成可执行 canonical Program 或 compilation：

- capability 存在但 mapping 缺失；
- 同一个 capability 有零条或多条 mapping；
- stable RWIR identifier 重复或与名称不一致；
- canonical BinRWIR identifier 重复或无法唯一解析；
- identifier 来自数组下标、文件偏移、声明次序或运行时注册次序；
- Read/Write 参数数量、顺序、类型或 alias policy 不一致；
- `behavior-decision` 缺失或引用不存在；
- source operation 不属于当前 specification generation；
- manifest version 与 Program identity 不一致。

这些都是 `ERROR`，不是 `NOT_COMPILABLE`。`NOT_COMPILABLE` 只表示合法 canonical
BinRWIR 已成功完成 vmjit eligibility 判定，并应建立 VMBIN compilation。

## 6. Compiler boundary

BinRWIR Compiler 只接收已经 finalize 且通过 mapping validation 的 canonical Program。
Compiler 生成 execution plan，并在编译阶段决定：

- `COMPILED`：建立 VMJIT compilation，包含机器码；
- `NOT_COMPILABLE`：建立 VMBIN compilation，不包含机器码；
- `ERROR`：不建立可执行 compilation，立即返回真实错误。

repository 缓存最终 compilation。route 在执行前只读取已经决定的 target，并调用
`vmjit_execute` 或 `vmbin_execute` 中的一个。

## 7. Acceptance checks

- 对 `myrwircaps` 与 `rwir_catalog.def` 做集合相等检查。
- 对 stable RWIR identifier 和 canonical identifier 分别做唯一性检查。
- 随机重排 manifest 后，生成 identifier 与 canonical Program 保持不变。
- 每条 mapping 都能解析到存在的五语言 behavior decision。
- 每条 generated BinRWIR instruction 都能反查唯一 mapping record。
- 删除、重复或歧义 mapping 的 fixture 必须编译失败。
