# rwir 能力表待定项

`runtimec_rwircap.kv` 与 `layout/src/crash_resume.rs` 的 `OP_TABLE` 不一致，共 25 条待裁决。
每条只填「决定」一列：**补**（写进能力表）/ **删**（删 OP_TABLE 行）/ **等**（等文法或实现，暂不动）。

## 一、真实能力，能力表未登记（12 条）

| 条目 | 现状与依据 | 决定 |
|---|---|---|
| `nonnull` | spec 16-ptr：存在性判定，标「尚未实现」 | |      //miaobyt,我加了rwir kvspace·exist([]char/utf8 #key) -> (bool #exists)
| `nonnull·not` | 同上 | |   //miaobyt,我加了rwir kvspace·exist([]char/utf8 #key) -> (bool #exists)
| `vthread·pc` | vthread 已在 layout/stdlib 使用 | |  //miaobyte，从这到下面的random系列都等待增加，等待确定起读写参。
| `vthread·create` | 同上（7 个文件使用） | |
| `vthread·run` | 同上 | |
| `vthread·call` | 同上 | |
| `vthread·setstatus` | 同上 | |
| `vthread·sleep` | 同上 | |
| `random·uint64` | layout 使用 | |
| `random·int63` | layout 使用 | |
| `random·intn` | layout 使用（4 个文件） | |
| `debugger` | layout 使用（18 个文件） | | //miaobyte 删除，后续会封装为stdlib的rwfunc()

## 二、旧名残留，能力表已有正确名字（4 条）

| 条目 | 能力表里的正确名字 | 决定 |
|---|---|---|
| `kvspace/key·abs` | `key·abs` | |  //miaobyte。走新命名
| `kvspace/key·tostr` | `key·tostr` | | //miaobyte。走新命名
| `string·concat` | `concat` | |  //miaobyte。 我已改为 string·concat
| `compactarray·newslice` | `compactarray·newreshape`（语义是否等价待确认） | |//miaobyte。删除compactarray·newslice这个。

## 三、能力表明写「整族已删除」（5 条）

依据：caps 尾注释「sparse 数组整体移除——由 map 系列覆盖」。
//miaobyte。是的，全部删除
| 条目 | 决定 |
|---|---|
| `scatterarray·new` | |
| `scatterarray·len` | |
| `scatterarray·newscatter` | |
| `scatterarray·newcompact` | |
| `scatterarray·slice` | |

## 四、已知缺口：类型文法表达不了（4 条）

依据：caps 尾注释「缺口：类型文法表达不了，暂不声明」。

| 条目 | 缺口原因 | 决定 |
|---|---|---|
| `compactarray·newreinterpret` | 元素类型未知，文法无类型变量 | |
| `compactarray·langtype` | 结果类型文法不接受 | |
| `xv·parselangtype` | 同上（泛型容器） | |
//miaobyte。以上langtype，改为以下rwir getlangtype([]char/utf8 #key)->([]char/utf8 #langtype);rwir setlangtype([]char/utf8 #newtype)->([]char/utf8 #key)
| `kvspace·extindex` | 能力表只有 `kvspace·delextindex`，读侧缺 | |//miaobyte。已不再适用extindex，所以删除。

## 五、非能力表的挂起项（供参考，不需要在这里逐条填）

- **语料迁移**：tutorial/stdlib 里 `time·sub`（19 处）、`duration·as_nanos`（24 处）、`time·since` 等旧算子调用，能力表删除后必报错。
- **CLI 深度检查**：等 layout 转绿后派工。
- **byteseek**：`kvlang/byteseek/src/ffi.rs` 仍声明旧构造器 ABI，未派工。
