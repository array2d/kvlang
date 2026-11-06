# `kvspace·` 族的完整 rwir 定义

本文是 `kvspace·` 族 **13 个 native rwir 算子**的规范定义：名字、读参、写参、结果类型规则、
语义与失败封闭方式。它是 `05-runtimec内建.kv`「`·` 限定内建族」一条的本体。

- **rwir 的唯一标识就是 key 本身**（`kvspace·get` 这一段文本）。**rwir 层没有 opid**。
- **数值 id 只属于 BinRWIR，而且是 runtimec 的私有实现细节**：它是本进程二进制编码的
  紧凑形式，不跨实现、不跨版本承诺，不得出现在 rwir 明文、源码或任何对外契约里。
- **真源**：`runtime/src/myrwircaps.c` 的注册表 → `rwir_catalog.def` 的能力行
  （`read_mask = write_mask = 0`，`RESULT_IDENTITY`）；surface 名字到能力行的映射在
  `rwir_alias_table.def`。能力表里那一列数值是 **BinRWIR 的 id**，只为二进制编码服务。
- **本文定的是语义与形状**；掩码 / 决策名以生成器为准，不得在本文另立一份。
- **兑现方**：`runtimec/vmbin/handlers/host.c`。

## 一、共同规则

1. **base 参数**（下表「读参」第一列）：一次存储访问的**基座**，它既可能是**容器/struct 值**
   （`for x in m`、`p·x` 这类 layout 内部发出的调用就是如此），也可能是**一段 path 文本**。
   键路径由值访问层从 base 解析：LOCAL 拼帧路径 / STATIC 用符号池文本 / 形参用绑定 key。
   **base 的类型不是 `char/utf8`**——把它写成字符类型会让容器/structref 传不进来（§七的实测）。
   键文本逐字使用，不做归一化、不补 `/`、不展开点段。
2. **结果是整值 wire 的算子**（`get` / `watch`）：结果类型取**目标 XValue 头**；头无 langtype ⇒
   失败封闭，不猜、不按写槽声明转换。这条与 #632 同一规则。
3. **无结果的算子**（`set` / `del` / `deltree` / `cp` / `cpdir` / `cplist` / `mkindex` /
   `extindex` / `rmindexext`）：不写结果槽，`write_arguments.count == 0`。
4. **写结果槽的算术类**（`listlen` / `listat`）：结果类型固定（见下表），与目标头的类型无关。
5. **不创建键**：`set` 只写已存在的 key（创建走生命周期指令，见 #632）；`get` 读不存在的 key
   得 None（矩阵决策 `kvspace-get-missing-key`），不是错误。
6. **arity 不符一律失败封闭**：不截断、不补默认值。诊断码见 §六。

## 二、完整定义表

| rwir key | 读参（按序） | 写参 | 结果类型 | 一句话语义 |
|---|---|---|---|---|
| `kvspace·get` | 1：base | 1 | **目标头**（运行期定型） | 读一条已存在的 key 的整值 |
| `kvspace·set` | 2：base，value 任意 | 0 | 无 | 整值写一条已存在的 key |
| `kvspace·del` | 1：base | 0 | 无 | 删单个键 |
| `kvspace·deltree` | 1：base | 0 | 无 | 删整棵子树 |
| `kvspace·cp` | 2：src base，dst base | 0 | 无 | 复制单个键 |
| `kvspace·cpdir` | 2：src base，dst base | 0 | 无 | 复制整棵子树 |
| `kvspace·cplist` | 2：src base，dst base | 0 | 无 | 按清单复制 |
| `kvspace·listlen` | 1：base | 1 | `int64` | 前缀下直接子项数 |
| `kvspace·listat` | 2：base，index `int64` | 1 | `char/utf8` | 第 index 个子项名 |
| `kvspace·mkindex` | 2：base，capacity `int64` | 0 | 无 | 为前缀建索引 |
| `kvspace·extindex` | 2：base，ext base | 0 | 无 | 挂扩展索引 |
| `kvspace·rmindexext` | 1：base | 0 | 无 | 摘扩展索引 |
| `kvspace·watch` | 2：base，expected 任意 | 1 | **目标头**（运行期定型） | 等 key 变成 expected，返回观察值 |

`get` / `set` / `watch` 的行**不得**用固定 langtype 描述：它们收发的值是任意值，任何固定类型都会
把「值」和「容器」混为一谈（本表在 `map·new` / `struct·new` 上已犯过两次）。

## 三、逐条细则

### `kvspace·get(key) -> v`

- 读一条已存在的 key 的**完整 wire**，写进写参；**成功才写写参，失败只写 `exit`**。
- 结果类型：取目标 XValue 头。头无 langtype ⇒ 失败封闭（`0x4A02` 一类由
  值访问层报，不在此伪装成 0）。
- 不存在的 key ⇒ `None`（矩阵决策 `kvspace-get-missing-key`），不是错误。

### `kvspace·set(key, value)`

- `value` 取**完整 wire**（head + body + attr）：对字面量，`locate` 给整条 wire；对普通槽，
  `locate` 给的是 body——判据只认 `locate` 的 `is_whole_wire`，不自己推断该不该剥。
- **不创建 key**：没有「目标不存在就建」的分支。
- 写侧类型由写入方携带；与目标头不一致 ⇒ 失败封闭（#632），不转换。

### `kvspace·watch(key, expected) -> observed`

- 读参 1 是 key、读参 2 是期望值；等 key 的值变成 expected 后，把**观察到的值**写进写参。
- 超时不是错误：后端返回 0 且无观察值时，写参保持原样。
- 结果类型同 `get`：取目标头。

### `kvspace·listlen(path) -> int64`

- 前缀下**直接子项**数。枚举失败不算错误：`count` 保持 0（与旧实现一致）。
- 结果恒为 `int64`，与子项值类型无关。

### `kvspace·listat(path, index) -> char/utf8`

- 取第 `index` 个子项名，按 UTF-8 字节写入写参。
- `index < 0` 或取不到 ⇒ **不写结果槽**。写空串会把「没有这一项」变成「这一项是空名」，
  那是 p1 禁止的「以奇怪的方式成功」。
- index 参数必须是 8 字节整数体；长度不符 ⇒ 失败封闭。

### `kvspace·mkindex(path, capacity)`

- 第二个读参是**标量容量**不是路径；`prepare_call` 只解析第一个，第二个单独按 8 字节整数读。
- 索引只影响「空前缀的可见性」，不改变任何值的读写语义。

### `kvspace·cp` / `cpdir` / `cplist`

- 两个读参都是路径，都是编码期可定的 key 文本（`extindex` 同）。
- 复制是**存储动作**，不产生值；源不存在 ⇒ 后端失败，不静默算成功。

### `kvspace·del` / `deltree` / `rmindexext`

- 单路径、无结果。删不存在的键按后端语义处理（幂等删除不等于创建）。

## 四、目标形态：`(base, key)` 两参形式（#634）

动态成员键 `base·*k` 不新增引用种类；它落在本族的**多参形态**上（见 `dynamic-key.md` §六）：

| 算子 | 目标形态 | 读参 |
|---|---|---|
| `kvspace·get` | `(base, key)` | base 的键路径 + 运行期求值的成员键文本 |
| `kvspace·set` | `(base, key, value)` | 同上 + 值 |
| `kvspace·watch` | `(base, key, expected)` | 同上 + 期望值 |

- base 的键路径用 `vmbin_value_access_argument_key` 取：LOCAL 拼帧路径 / STATIC 用符号池文本 /
  形参用绑定 key；其余形态 INVALID。
- 成员键文本用 `member_key_text` 生成，逐字铁律的唯一实现点（与 `float_format` 共用核心算法）。
- **键文本不许定长缓冲**（`0x4501` 的 64 字节教训）。
- 结果类型仍按 §一.2：取目标头；`type_from_target_locals` 从「写来源是 STATIC 引用」扩成
  「写来源是运行期定型算子」。

**当前实现状态**：`get` 是 1 读参（整条 key 文本），`set` 是 2 读参且第一个必须是 STATIC key。
两参形式尚未落地（#634）；落地前不得在文档外假设它已存在。

## 五、与存储原语的分工

`storage-primitives.md` 的六条（`resolve` / `load` / `store` / `new` / `malloc` / `keytext`）
是**按地址**的存储原语，与本族**按 key**的算子不是同一层：

- 本族是「键 → 值」的一步式语义算子，已有能力面，不再包一层；
- 六条原语是「键 → 地址 → 读写」的拆步形式，服务于动态键与地址传递；
- 管理面 ABI（`Mkindex*`、`Clear`、`View*`）不进 BinRWIR，理由见 `storage-primitives.md` §二。

## 六、诊断码

| 情形 | 码 |
|---|---|
| arity 不符 | `RUNTIMEC_HOST_DIAG_ARITY`（0x4A05） |
| 键解析不出 / 第二个路径不是路径 | `RUNTIMEC_HOST_DIAG_KEY_UNRESOLVED` |
| 后端调用失败 | `RUNTIMEC_HOST_DIAG_BACKEND_FAILED` |
| 整数体长度不符、值形态非法 | `RUNTIMEC_HOST_DIAG_OPERAND_INVALID` |
| 结果类型取不到（目标头无 langtype） | 值访问层码（0x4A02 一类），不在此另造 |

**不得报 0**：任何失败都要带段内码，`0` 表示「没有诊断」，不是合法状态。

## 七、layout 侧签名来源：**本族不进 `native_rwir_decls.kv`**

`native_rwir_decls.kv` 只收**无前因的常量类型**（`time`、`vthread·*` 一类）。本族**一行都不能进**：

| 算子 | 可声明 | 理由 |
|---|---|---|
| 全部 13 条 | **不可** | 第一个读参是 base（容器值 / structref / path 文本三态），没有任何单一 langtype 能收敛它 |

实测（2026-11-06，`generate_rwir_catalog.py` 给 10 条补 `char/utf8` 后重建 layout，206 条语料）：

```
03-control/for.kv            TypeError: cannot pass [int64]·int64 to char/utf8 (parameter argument0)
04-ndarray/array_slice_hole  TypeError: cannot pass [int64]·int64 to char/utf8 (parameter argument0)
08-leetcode/100_same_tree    TypeError: cannot pass /lib/TNode to char/utf8 (parameter argument0)
08-leetcode/104_max_tree_depth 同上
12-struct/10-member-chain    TypeError: cannot pass /lib/N to char/utf8 (parameter argument0)
```

五条从 blocked 变 fail（36/53 → 31/58）。反证也齐：去掉声明后回到 113/36/53。这五条源码里**没有**
`kvspace·` 字样——调用是 layout 内部发出的（for-in over map、struct 成员访问），第一个实参
就是容器/structref。

因此：

- **本族的形状定义只在本文**，不进任何签名产物；
- layout 侧的返回类型由类型推导给出（`get`/`watch` 取基座的容器值类型，`listlen` → `int64`，
  `listat` → `char/utf8`），今天是几条手写特例——它们是 §一.2 在 layout 侧的对应物，
  不属于配置文件常量；
- 不要再试「只给 path 类算子补声明」：`del`/`cp` 一类同样收 base，同上。

## 八、验收

- 本表的 13 行与 `rwir_alias_table.def` 的 13 个 surface 名字**逐条对齐**；多一个少一个都是缺陷。
- `05-runtimec内建.kv` 的 `kvspace·` 方法集与本表一致（此前写成 `list` / `listn`，是另一套
  不存在的名字）。
- 每个「不写结果槽」的场合（`listat` 越界、`watch` 超时）都有反向对照：写槽保持原值，不被清零。
- arity 不符、key 解析失败、后端失败三条各有用例，且都带非零诊断码。
