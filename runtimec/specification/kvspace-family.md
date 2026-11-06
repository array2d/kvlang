# `kvspace·` 族的完整 rwir 定义

本文是 `kvspace·` 族 **11 个 native rwir 算子**的规范定义：名字、读参、写参、结果类型规则、
语义与失败封闭方式。它是 `05-runtimec内建.kv`「`·` 限定内建族」一条的本体。

- **rwir 的唯一标识就是 key 本身**（`kvspace·del` 这一段文本）。**rwir 层没有 opid**。
- **数值 id 只属于 BinRWIR，而且是 runtimec 的私有实现细节**：它是本进程二进制编码的
  紧凑形式，不跨实现、不跨版本承诺，不得出现在 rwir 明文、源码或任何对外契约里。
- **真源**：`specification/myrwircaps.c` 的名字表 → `rwir_catalog.def` 的能力行
  （`read_mask = write_mask = 0`，`RESULT_IDENTITY`）；surface 名字到能力行的映射在
  `rwir_alias_table.def`。能力表里那一列数值是 **BinRWIR 的 id**，只为二进制编码服务。
- **本文定的是语义与形状**；掩码 / 决策名以生成器为准，不得在本文另立一份。
- **兑现方**：`runtimec/vmbin/handlers/host.c`。

## 一、共同规则

0. **方向原则（本族的根规则）：读 key 只读、写 key 只写。** 一个 key 若被修改，它必须以
   **写参**出现；读参永远只读。`set(key, value)`（用读参表达写目标）与 `get(base,key)`
   （把「取哪一段」混进读参）因此**已删除**；写目标一律由写参承载。
   **按名访问一个 key 不需要本族的任何算子**：读就是用该 key 作操作数的具体 rwir
   （值访问层按引用解析 key），写就是 `=` / `->` 降到以该 key 为写目标的普通 rwir
   （`v -> /p/a`、`/p/a = v`）。本族只保留「不能由普通读写表达」的存储动作。
1. **base 参数**（下表「读参」第一列）：一次存储访问的**基座**，它既可能是**容器/struct 值**
   （`for x in m`、`p·x` 这类 layout 内部发出的调用就是如此），也可能是**一段 path 文本**。
   键路径由值访问层从 base 解析：LOCAL 拼帧路径 / STATIC 用符号池文本 / 形参用绑定 key /
   **字面量用其文本值本身**（`"/tmp/x"·*k` 的 base 是字面量字符串，值即父路径）。
   **base 的类型不是 `char/utf8`**——把它写成字符类型会让容器/structref 传不进来（§七的实测）。
   键文本逐字使用，不做归一化、不补 `/`、不展开点段。
1b. **零长值不是缺失**：判据是 **key/head 的存在性**，不是 body 长度。空 body 是一个合法值
   （空串），缺失是 key 不存在。任何把「零长」当「缺失」的写法都是缺陷（reader 给 None、
   writer 不创建）——先例 runtimec2 就是把两者合并的，**不跟**。
   主席位：`stdlib/kvlang/spec/03-类型系统/03-种类-代码与特殊值.kv:22-24`（空串/空容器不是 None）；
   成员访问侧见 `05-runtime语义/03-成员访问.kv:26`。本条是这两条的施工判据。
2. **取目标头的算子**（`watch`）：结果类型取**目标 XValue 头**；头无 langtype ⇒
   失败封闭，不猜、不按写槽声明转换。这条与 #632 同一规则。
3. **复制类**（`cp` / `cplist`）：src 是读参、dst 是**写参**，不改类型——dst 接到的值与 src
   同类型；`cp` / `cplist` 的结果槽即 dst，`cpdir` 不产生值。
4. **无结果的算子**（`del` / `deltree` / `cpdir` / `mkindex` / `extindex` / `delextindex`）：
   不写结果槽，`write_arguments.count == 0`。
5. **写结果槽的算术类**（`listlen` / `listat`）：结果类型固定（见下表），与目标头的类型无关。
6. **创建只发生在写参**：读参永不创建 key；写参不存在时按写参声明的类型创建（#632）。
7. **arity 不符一律失败封闭**：不截断、不补默认值。诊断码见 §六。

## 二、完整定义表

| rwir key | 读参（按序） | 写参 | 结果类型 | 一句话语义 |
|---|---|---|---|---|
| `kvspace·del` | 1：base | 0 | 无 | 删单个键 |
| `kvspace·deltree` | 1：base | 0 | 无 | 删整棵子树 |
| `kvspace·cp` | 1：src base | 1 dst base  | 同读参 | 复制单个键 |
| `kvspace·cpdir` | 1：src base |1 dst base| 无 | 复制整棵子树 |
| `kvspace·cplist` | 1：src base| 1 dst base | 同读参| 按清单复制 |
| `kvspace·listlen` | 1：base | 1 | `int64` | 前缀下直接子项数 |
| `kvspace·listat` | 2：base，index `int64` | 1 | `char/utf8` | 第 index 个子项名 |
| `kvspace·mkindex` | 2：base，capacity `int64` | 0 | 无 | 为前缀建索引 |
| `kvspace·extindex` | 2：base，ext base | 0 | 无 | 挂扩展索引 |
| `kvspace·delextindex` | 1：base | 0 | 无 | 摘扩展索引 |
| `kvspace·watch` | 2：base，expected 任意 | 1 | **目标头**（运行期定型） | 等 key 变成 expected，返回观察值 |

 `watch` 的行**不得**用固定 langtype 描述：它收发的值是任意值，任何固定类型都会
把「值」和「容器」混为一谈（本表在 `map·new` / `struct·new` 上已犯过两次）。

## 三、逐条细则

### `kvspace·watch(key, expected) -> observed`

- 读参 1 是 key、读参 2 是期望值；等 key 的值变成 expected 后，把**观察到的值**写进写参。
- 超时不是错误：后端返回 0 且无观察值时，写参保持原样。
- 结果类型取目标头（§一.2）。

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

- **src 是读参、dst 是写参**：目的 key 由写参承载，不从读参文本里读第二段路径。
  `cpdir` / `cplist` 的 dst 是子树/清单目标，同样走写参。
- 复制不改类型：dst 接到的值与 src 同类型；`cp` / `cplist` 的结果槽就是 dst。
- 复制是**存储动作**；源不存在 ⇒ 后端失败，不静默算成功。

### `kvspace·del` / `deltree` / `delextindex`

- 单路径、无结果。删不存在的键按后端语义处理（幂等删除不等于创建）。

### `kvspace·extindex` / `delextindex`

- `extindex(base, ext)`：挂扩展索引；两个读参都是路径（`ext` 是索引路径文本，不是写目标）。
- `delextindex(base)`：摘扩展索引，单路径、无结果。

## 四、目标形态：`(base, key)` 两参形式（#634）

动态成员键 `base·*k` 不新增引用种类；它落在本族的**多参形态**上（见 `dynamic-key.md` §六）。

**⚠ 2026-11-06 用户裁决：`get` / `set` 已从本族删除**（读 key 只读、写 key 只写）。
原先建立在它们上的动态键两参形态因此**失效，待重新裁决**：

| 原形态 | 现状 |
|---|---|
| `kvspace·get(base, key)` | **删除**；动态成员**读**的新形态待定 |
| `kvspace·set(base, key, value)` | **删除**；动态成员**写**的新形态待定 |

新形态必须满足的方向原则（§一.0）：成员键是**运行期值**，读操作只能有只读的 key 操作数；
写操作的**目标必须是写参**。候选方向（未裁决，仅记录约束，不得据此实现）：
`base` 与运行期成员键作为读参参与定位，目的位置以写参出现；`watch` 同理。

- base 的键路径用 `vmbin_value_access_argument_key` 取：LOCAL 拼帧路径 / STATIC 用符号池文本 /
  形参用绑定 key / **字面量用其文本值**；其余形态 INVALID。
- 成员键文本用 `member_key_text` 生成，逐字铁律的唯一实现点（与 `float_format` 共用核心算法）。
- **键文本不许定长缓冲**（`0x4501` 的 64 字节教训）。
- 结果类型仍按 §一.2：取目标头；`type_from_target_locals` 从「写来源是 STATIC 引用」扩成
  「写来源是运行期定型算子」。

**当前实现状态**：`get` / `set` 在代码里仍然存在（`host.c` 与注册表尚未跟随本裁决删除），
但**语言面已移除**；`dynamic-key.md` 与 #634 的多参方案按上表作废，等新形态裁决后再写。

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
| 全部 11 条 | **不可** | 第一个读参是 base（容器值 / structref / path 文本三态），没有任何单一 langtype 能收敛它 |

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
- layout 侧的返回类型由类型推导给出（`watch` 取基座的容器值类型，`listlen` → `int64`，
  `listat` → `char/utf8`），今天是几条手写特例——它们是 §一.2 在 layout 侧的对应物，
  不属于配置文件常量；
- 不要再试「只给 path 类算子补声明」：`del`/`cp` 一类同样收 base，同上。

## 八、验收

- 本表的 11 行与 `05-runtimec内建.kv` 的 `kvspace·` 方法集**逐条对齐**；多一个少一个都是缺陷。
- **已知偏差（待代码跟随）**：`rwir_alias_table.def` / 注册表 / `host.c` 里仍有
  `kvspace·get`、`kvspace·set`、`kvspace·rmindexext`，共 13 个 surface 名字。本裁决要求删除
  前两个、把第三个改名为 `delextindex`——**这是代码改动，不是文档改动**，落地前两边不一致
  是预期状态，不得用「文档对齐」掩盖。
- 每个「不写结果槽」的场合（`listat` 越界、`watch` 超时）都有反向对照：写槽保持原值，不被清零。
- arity 不符、key 解析失败、后端失败三条各有用例，且都带非零诊断码。
