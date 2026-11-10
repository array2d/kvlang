# 声明了但执行路径不通的算子（检查器产出）

**这份文件是 `tools/architecture/check_handler_definitions.py` 的缺口清单，不是它会自动重写的
生成物。** 生成物 `specification/rwir_catalog_gaps.md` 讲的是**另一件事**（已注册但收敛不到
capability row 的名字，当前 0 条），两者名字相近、含义不同——**一个词不能两义**，故本文件
取名 `registered-without-handler.md`。

## 判据

`runtimec/specification/myrwircaps.c` 声明「这个 surface 名字由本 runtime 兑现」。
兑现它可以走**四条路**，本清单列出的是**四条都不通**的那些：

| # | 路径 | 判据来源 |
|---|---|---|
| 1 | `binrwir_registry` 里查得到 opid 的 handler | `runtimec/vmbin/handlers/*.c` 的条目表 |
| 2 | 解释器**内联分派**（不进 registry） | `vmbin/interpreter.c` 的 `switch`，只有 `=`/`goto`/`br`/`call`/`return` |
| 3 | **编译期特化**：基础 opid 被改写成 0x8000+ | `binrwir/specialization_table.c` 第一列 |
| 4 | 名字 → opid 的映射本身存在 | `specification/rwir_alias_table.def` |

第 3 条不是推断，是**实测**：探针跑 `a + b`（基础 opid 0x0002 `add`）得到 `executed`。
通用算术 handler 已按 p10「先删后增」删除，opid 在执行前就被换成特化 opid——所以基础
opid「没有 handler」是设计使然，**不是**缺口。

## 清单（15 条，逐条带 opid）

| 族 | surface | opid | 备注 |
|---|---|---|---|
| compactarray | `compactarray·shape` | `0x0106` | 同族的 `numel`/`at`/`set` 已注册，只缺这一个 |
| kvspace | `kvspace·cplist` | `0x0176` | host 侧复制族 |
| kvspace | `kvspace·delextindex` | `0x017B` | 扩展存储族 |
| kvspace | `kvspace·extindex` | `0x017A` | 扩展存储族 |
| kvspace | `kvspace·watch` | `0x017C` | 同族 `get`/`set` 等已注册 |
| ndarray | `ndarray·numel` | `0x0140` | 整个 `ndarray·*` 族都缺（3 条） |
| ndarray | `ndarray·dim` | `0x0141` | |
| ndarray | `ndarray·shape` | `0x0142` | |
| xv | `xv·at` | `0x0130` | 整个 `xv·*` 族都缺（6 条） |
| xv | `xv·set` | `0x0131` | |
| xv | `xv·reshape` | `0x0132` | |
| xv | `xv·reinterpret` | `0x0133` | |
| xv | `xv·langtype` | `0x0134` | |
| xv | `xv·bodylen` | `0x0135` | |
| （映射缺失） | `obj` | — | 登记表里有，`rwir_alias_table.def` 没有它的映射 |

## 实测证据

`04-ndarray/xv_shape.kv` 使用 `compactarray·shape`，在语料里判「执行期失败」：

```
FAIL 04-ndarray/xv_shape.kv: execute failed status=5（exit.kind=6 status=5 detail=16643 pc=5）
```

`16643 = 0x4103 = VMBIN_INTERPRETER_DIAG_INVALID_INSTRUCTION`——**正是 registry 未命中**。
`01-basics/kv_tree.kv` 同样用了它，在编译期就失败。所以这一族是真的没接上，不是判据噪声。

其余各族的用例归属可在 `KVRUN_LIST_OUTCOMES=1` 的逐条输出里按路径核对。

## 复现

```bash
python3 runtimec/tools/architecture/check_handler_definitions.py \
    --source-root . --kvspace-include=<kvspace 仓的 include/>
# → check_handler_definitions: FAIL (15 finding(s))
```

`--kvspace-include` 是**命令行参数而不是环境变量**：kvlang 只有一个环境变量 `KVSPACE`
（用户裁决），与 `kv_runner --operators=a,b` 同一理由、同一形状。缺参数时检查器不会退回
猜路径，而是报「不可检查」并给出可照抄的命令。

## 处置

**本轮不修。** 用户已明确收敛后不再展开新功能。这份清单作为**下一轮输入**：
按族补齐（`xv·*` 与 `ndarray·*` 各是一整族未接，可能各是一次性工作；`kvspace·*` 4 条在
host 侧，取决于 TASK-042 的排期）。

补的时候**不要**为了让检查器变绿去调判据，也不要直接往 registry 里塞条目而不实现语义——
检查器只证明「有人接」，不证明「接得对」。
