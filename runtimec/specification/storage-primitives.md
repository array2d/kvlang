# 存储原语：把 kvspace ABI 提升为 BinRWIR 操作

用户裁决（2026-11-06）：

> kvspace 的很多 ABI，我建议你加入 binrwir。如 keyto64 的转换（通过 key 查找 64 位内存地址）、
> malloc、kvspace·new 等。他们是支持动态 key 的基础。

本文给出这组原语的语义、与现有 kvspace ABI 的对应、地址的生命周期规则，以及它们与动态成员键
（`dynamic-key.md`）的关系。

## 一、目的：让「键 → 地址 → 读写」成为可表达的基本操作

现在的 BinRWIR 只有两类键：**符号池里的文本键**（LOCAL/STATIC）与**运行期值参数**
（`kvspace·get/set` 的实参）。动态键之所以没有位置，是因为「把运行期算出来的文本变成一个
可访问的位置」这一步没有可表达的算子。

照 C 的模型补齐这一步：

| C | 本组原语 |
|---|---|
| `void *p = malloc(n)` | `kvspace·malloc`：分配一个新位置，返回可写地址 |
| 取一个已有对象的地址 | `kvspace·resolve`（keyto64）：key 文本 → 地址 |
| `*(T*)p = v` | `kvspace·store`：按地址写，类型由写入方携带 |
| `T x = *(T*)p` | `kvspace·load`：按地址读，类型向目标头取 |
| `(T){...}` 构造一个值 | `kvspace·new`：构造 XValue（纯 wire） |
| `snprintf` 出键文本 | `kvspace·keytext`：值 → 成员键文本（逐字铁律的唯一实现） |

有了这六条，`base·*k` 不需要解释器里的任何「动态键分支」：
`kvspace·resolve(键文本)` → `kvspace·store` / `kvspace·load`。

## 二、筛选判据：**不是全部 ABI**

用户补充（2026-11-06）：

> 注意，不是全部。

kvspace 的 ABI 里既有「程序语义需要的存储原语」，也有「宿主/工具的管理动作」与「后端实现
细节」。只有前者进 BinRWIR。判据三条，**同时成立**才入选：

1. **语言语义需要它**：源代码/运行期数据流里存在这个动作，且没有它某条语义无法表达；
2. **没有等价表达**：现有 rwir 算子或语言原语表达不了（能表达的一律不新增，见 p6）；
3. **跨后端有同一语义**：或后端未实现时能明确返回 `UNSUPPORTED`，而不是语义随后端漂移。

### 入选（当前）

| 原语 | 为什么必须 |
|---|---|
| `kvspace·resolve` | 「文本键 → 可访问位置」这一步目前没有任何算子；动态键、地址传递都靠它 |
| `kvspace·load` / `store` | 有了地址就必须有按地址读写；否则 resolve 没有出口 |
| `kvspace·new` | 运行期要构造「值」（含 Ptr），不能只有源码字面量 |
| `kvspace·malloc` | 运行期要拿到一个新的可写位置 |
| `kvspace·keytext` | 逐字铁律的唯一实现点；没有它，动态键的格式化会在各处分裂 |

### 不入选（明确排除，理由逐条）

| ABI | 理由 |
|---|---|
| `kvspaceMkindex` / `MkindexExt` / `RmindexExt` | 目录/索引**管理**动作，不是值的读写；空目录可见性是宿主视角，程序语义不需要（#632 已实测：写即创建父级） |
| `kvspaceClear` / `kvspaceWatch` | 宿主运维与观察通道，不是程序内的值操作 |
| `kvspaceListLen` / `ListAt` 等枚举 | 属于语言的容器遍历语义（已有 `kvspace·listlen`/`listat` 算子），**不新增底层形式**；同一事实只留一处 |
| `kvspaceView*`（Sweep/Ack/Quiesce/ResolvePrefix/Resolve/Check/Write/Del/Refresh） | 后端**实现细节**（版本化视图/缓存），语言层不得感知 |
| `kvspaceXvalueBodyView` | 只是 `resolve` 在 shm 后端的**实现手段**，不单独成为算子 |
| `kvspaceGetHead` | 是 `load` 的内部步骤（读类型），不作为独立语言面；把它暴露出去会让「读头」变成可编程的旁路 |
| `kvspaceCp` / `CpTree` / `CpList` / `Del` / `DelTree` | 已有 `kvspace·cp`/`cpdir`/`cplist`/`del`/`deltree` 算子覆盖，**不重复包装** |
| `kvspaceGet` / `SetValue` / `SetPart` / `GetPart` | 已有 `kvspace·get`/`set` 与值访问层覆盖；`load`/`store` 是按地址的同族，不新增按 key 的第三条路 |

一句话：**进 BinRWIR 的是「程序要用的存储原语」，不是 kvspace 的全部能力面。** 管理面留在
宿主（byteseek / 工具链），实现面留在后端。

**这条判据同时是 kvspace ABI 删减的输入**（用户已声明后面会删减一轮）：凡是没有 BinRWIR
原语、也没有宿主工具链使用者的 ABI，就是删减候选。反过来，本表入选的六条是**语言侧依赖**，
删减时不能动它们的语义（可以改名字/合并实现，但要保留等价能力）。

## 三、原语清单与 ABI 对应

| 原语 | 语义 | 对应 kvspace ABI |
|---|---|---|
| `kvspace·resolve` | key 文本 → 不透明地址 | shm：`kvspaceXvalueBodyView`（head/body 指针）；后端中立：`kvspaceResolveRef`（`kvspaceRef_t`） |
| `kvspace·load` | 地址 → 整值 wire | `kvspaceGetByRef` |
| `kvspace·store` | 地址 + 值 → 写入 | `kvspaceSetValueByRef` / `kvspaceSetPartByRef` |
| `kvspace·new` | 构造 XValue（不落地） | `kvspaceNewInt64` / `kvspaceNewBool` / `kvspaceNewChar` / `kvspaceNewPtr` / `kvspaceNewFloat64` |
| `kvspace·malloc` | 分配新位置，返回可写地址 | `kvspaceWriteNewPlace`；`kvspaceWriteInPlace` 是同族（已有位置的就地写） |
| `kvspace·keytext` | 值 → 成员键文本 | 尚无独立 ABI；规则见 spec `05-runtime语义/03-成员访问`「浮点文本」与逐字铁律 |

`kvspace·resolve` 与 `kvspace·malloc` 是**新增**；`kvspace·new` 是前端已有的纯 wire 构造器；
`load`/`store` 是 `kvspaceGetByRef` / `kvspaceSetValueByRef` 的语言面。

> **右列的 ABI 名只是实现映射，会随 kvspace 的 ABI 删减重核（用户已声明后面会删减一轮）。
> 规范部分是**左列的语义**：原语叫什么、接受什么、失败如何封闭，不随 ABI 改名或合并而变。**
> 实现时把 ABI 调用点限制在本表用到的函数上，不顺手用表外的 ABI——否则删减时每处都是
> 一处待改的暗债。

## 四、地址的语义（照 C 的堆内存，但去掉 UB）

| 规则 | 内容 |
|---|---|
| 不透明 | 语言层不得假设它是内存指针还是后端句柄；shm 后端天然是映射内地址，durable 后端是 ref 结构 |
| 不可算术 | **不做 `addr + i`**：跨后端不一致，且与「键是唯一身份」冲突。元素寻址一律回到 key |
| 不可持久化 | 地址是**进程内**事实。不得写进任何会被恢复的状态；帧槽可以暂存，但 vthread 恢复后一律视为失效，必须重新 `resolve` |
| 写侧带类型 | `store` 的值类型由写入方携带；与目标头类型不一致 ⇒ **失败封闭**（同 #632，不转换、不猜） |
| 读侧取头 | `load` 返回类型取目标 XValue 头；头无类型 ⇒ 失败封闭 |
| 失效即失败 | `resolve`/`load`/`store` 遇到不存在的 key、被删除/搬移的位置、不支持的地址形态 ⇒ **失败封闭**。不做「重新按名字找一遍」的兜底 |
| 分配失败 | `malloc` 分配不到 ⇒ 失败，不返回空地址让后续访问继续 |

**零地址不是合法值**（同 `BINRWIR_BODY_ADDRESS_NONE` 的既有约定）：resolve/malloc 成功必须
给出非零地址，零一律按失败处理。

## 五、地址在类型系统里的位置

三个候选，推荐第一个：

1. **不透明 `int64`**（推荐）：与现有 8 字节标量机制、帧槽宽度、持久化零值规则全部兼容；
   禁止参与算术/比较/字符串化的规则写进 spec（C 里 `void*` 转整数也是靠约定）；
2. 新增专用 kind（如 `addr`）：类型上最干净，但要动 langtype 词表、formatter 与全部容器，
   属语言变更，不随本组原语一起做；
3. 指针形态（复用 Ptr 的 ref=1）：语义最接近，但 Ptr 的目标是「kindexpr 路径」而不是
   「后端地址」，两套地址混在一个 kind 里正是要避免的歧义。

## 六、与动态成员键的关系

`base·*k` 的完整展开链：

1. **base 键路径**：base 是帧槽 → `<帧路径>/<名字>`；是指针 → 解引用后的目标路径；
2. **键文本**：`k` 的值经 `kvspace·keytext` 得到（逐字铁律，浮点负零/最短往返等规则同成员键）；
3. **成员键** = 第 1 步 + `·` + 第 2 步；
4. `kvspace·resolve` → `load`/`store`。

⇒ 解释器只需要「按地址访问」这一条通用路径，**不需要 DYNAMIC 引用种类、不需要在
value_access 里按 `·*` 文本二次解析**。这与 `dynamic-key.md` 的裁决一致：动态键留在执行期，
但执行期看到的只是原语组合。

## 七、实施顺序与验收

1. `kvspace·new`（前端的五个构造器）——最小、无状态、跨后端一致，先落地；
2. `kvspace·keytext`（值 → 键文本）——动态键的另一半；
3. `kvspace·resolve` + `kvspace·load` / `kvspace·store`——地址访问打通；
4. `kvspace·malloc`——分配；需要同时定义「分配出来但未写」的可见性规则（同 `mkindex` 只影响
   空目录可见性）；
5. 最后由 layout 把 `base·*k` 表达成原语组合（在 `dynamic-key.md` 的长期方案里）。

验收（每一层都要有反向对照）：

- `kvspace·new` 构造的 wire 与 `kvspaceNew*` 逐字节相同，且跨后端一致；
- `resolve` → `store` → `resolve` → `load` 的往返值不变；类型不符时 `store` 失败；
- 地址跨 vthread 恢复后访问**必须失败**（不得静默读到旧位置）；
- 不存在的 key `resolve` 失败、分配失败 `malloc` 失败，二者都有具名诊断码；
- 上述任一原语在某个后端未实现时，返回 `RUNTIMEC_STATUS_UNSUPPORTED`，不是静默降级。

## 八、与既有裁决的关系

- `dynamic-key.md`：动态键不新增引用种类；本组原语就是它的地基；
- #631（struct/map 即 xvaluegroup）：组的身份是路径，地址只是一次解析的结果；
- #632（绝对 key 类型）：写侧携带类型、读侧向目标头取——`store`/`load` 逐字沿用；
- #633（键编码）：`kvspace·keytext` 是逐字铁律的唯一实现点，不得旁路。
