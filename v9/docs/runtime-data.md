# v9 执行期数据结构

> 快照：2026-11-12。范围：执行期需要的**数据句柄**，不含指令集与操作数编码的细节。
> runtimec 只列改动清单，逐处待人工审核。

## 一、总览

执行期只有四个数据句柄：

```
cells    const binrwir_t *   指令流 + 常量（8 字节格，只读）
pcs0     int32_t *           指向 ‥pc 的 s0 分量
stack    int64_t             帧锚点（parent_key_anchor；0 = root）
addrbuf  runtimec_addrbuf_t *解析点地址缓冲（帧 = [mark, count)）
```

## 二、逐个定义

| 句柄 | 类型 | 谁拥有 | 生命周期 | 读写方 |
|---|---|---|---|---|
| `cells` | `const binrwir_t *` | `/lib/<pkg>·<func>.bin` 的 XValue body（kvspace 映射内存，**借用**，不拷贝） | key 不被覆盖期间 | 只读；v9 写出，runtimec 取指 |
| `pcs0` | `int32_t *` | vthread 的 `‥pc`（`[2]int32 {stackid, s0}` 的第二个分量） | vthread 生命周期 | 每步由编译器插入的 `pcs0++` 写；解释器只读它判断步边界 |
| `stack` | `int64_t` | 帧（建帧期从帧路径解析出的锚点） | 帧生命周期 | `kvspace·resolve` 的相对 key 锚点；0 表示 root |
| `addrbuf` | `runtimec_addrbuf_t *` | `runtimec_runtime`（已实现） | runtime 生命周期 | 建帧/载入期 push，执行期按索引读 |

四条失败路径都是 abort（p0）：cells 指向非 8 字节对齐、pcs0 为空、stack 在无帧时被读、
addrbuf 越界。

## 三、寄存器与格子

- **寄存器**：`pcs0`、`stack`。它们**不进操作数格**，执行期直接从上下文取。
  - `pcs0++` 是 v9 插入的一条普通 `add`（uint32 类算子），操作数是 `pcs0` 这个寄存器；
  - `stack` 由 `kvspace·resolve` 直接使用，不占格子。
- **格子**：`cells` 里的操作数格，按角色分四类——值、位置、坐标、锚点（锚点即 stack，
  故不出现在格子里）；另有一个正交的 deref 位。
- **常量**：放在 `cells` 里（内联），不另开常量区；key 文本**不复制**，引用 `/lib`
  下已有槽的偏移。

## 四、调用约定

```
建帧：
  pcs0   = (int32_t *)pc_body + 1        /* ‥pc = [stackid, s0] */
  stack  = 帧根锚点（parent_key_anchor）
  mark   = runtimec_addrbuf_mark(addrbuf)
  cells  = .bin 的 body 起点（8 字节对齐）

执行：
  grouprun 从 entry_cell 起，按 next_distance 取 op 字
  每条 op 按 opid 派发；v9 在每两条 op 之间插入 pcs0++（uint32 add）

退帧：
  runtimec_addrbuf_release(addrbuf, mark)
```

## 五、runtimec 改动清单（逐处待人工审核）

| 文件 | 改动 | 说明 |
|---|---|---|
| `runtime/interpreter.h` | 新增执行上下文结构（建议 `runtimec_execution_t`）持有 `.pcs0 / .stack / .addrbuf / .cells` | 与 `runtimec_runtime_t` 分开：runtime 是进程级，上下文是每次执行一份 |
| `runtime/interpreter.c` | `run` 里建上下文：pcs0 取 vthread 的 pc_body、stack 取帧锚点、addrbuf 取 `&runtime->addrbuf`、cells 取 `.bin` body | 只建句柄，不解释 |
| `runtime/addrbuf.h` | 已就绪（initialize/mark/push/release/address） | 不改 |
| `binrwirgrouprun/grouprun.{h,c}` | 入口改为接收执行上下文（cells + pcs0 + stack + addrbuf） | 当前只收 `binrwir_group_t` |
| `binrwir/handler.h` | `binrwir_instruction_t` 之前增加上下文参数（或把上下文挂进 view） | 二选一，待定 |

## 六、v9 侧（本次不实现）

- 编译：读 layout 的槽 → 产出 `cells`；常量内联；操作数格编码；`next_distance`；
- 输出：`/lib/<pkg>·<func>.bin`，langtype `[8N]uint8`；
- 会话级：pcs0++ 的插入（uint32 add）、resolvekey 的落法按决策 B。

## 七、未决（影响格子编码）

1. 全宽常量（int64 / uint64 / float64）与格内 tag 位的关系：内联会挤掉 2 位；
   放 `cells` 的数据格还是走 `/lib` 常量池，需在编码定稿时一并决定。
2. `pcs0`、`stack` 作为「寄存器操作数」的编码：是保留 op 字里的特殊 kind，
   还是约定 handler 按 opid 直接取上下文。
