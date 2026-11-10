# capability_probe

能力行探测工具：给一条能力行（opid 或算子名）跑一次执行，**只报事实、不下结论**。

四线作者（A/B/C/D）需要同一个口径回答「这一行现在是什么状态」，各自发明验收方式会让
同一件事有四种说法。这个工具是那个唯一口径。

## 用法

```
capability_probe <dsn> <能力行标识> [源码文件]
```

- **dsn**：kvspace DSN，例如 `shm:///workspace/tmp/coding2/probe/store.shm`
- **能力行标识**：算子名（`add`、`copy`、`输出算子`…）。工具据此拼默认模板。
- **源码文件**：可选。不给时用内置模板（`rwfunc probe() -> (*int64 $r) { <op> -> *r }`）。

## 输出（TSV，一行）

```
<行标识>\t<exit.kind>\t<detail_code>\t<首行输出>
```

`exit.kind` 是数值 + 名字，例如 `3(HANDOFF)`。

## 判定口径

工具**不判定**。判定规则在 `coding1/builtin-registry-contract.md`：

| exit.kind | 含义 |
|---|---|
| `DONE` | 已实现 |
| `HANDOFF` + `UNSUPPORTED` | 未实现 |
| `ERROR` + `detail_code != 0` | 实现了但错了 |
| `STAGE_FAILED` | **不是能力状态**：准备期（快照/编码/编译）就失败了，没跑到指令 |
| `LAYOUT_FAILED` | **不是能力状态**：layout 编不出这段源码 |
| `NO_TEMPLATE` | **不是能力状态**：内置模板没覆盖该算子，需给源码 |

`STAGE_FAILED` 的判据：`ERROR` 且 `logical_program_counter == NONE`。真正的执行期
`ERROR` 一定带着出错那条指令的位置；准备期失败没有位置。

**这条区分不是装饰**：今天实测所有能力行都因为「读不到 source revision」在编码前失败，
若不分开会统一显示成 `ERROR`，读者会以为每条能力都「实现了但错了」。

**工具只报这三列事实**，由读者按上表归类。把分类写进工具就等于把「当前状态」固化成
断言——今天未实现的行明天实现了，工具会开始说谎。

## 为什么不写成 ctest

它是**分类器，不是测试**：输出随能力落地而变化，没有任何期望值。写成 ctest 会对
一个持续变化的事实做断言，那正是「给未来挖的维护坑」。

## 依赖

- `libkvspace`：经 `dlopen` 按路径加载（同其它 runtimec 工具）
- `libkvlanglayout`：把内置模板 layout 进 store
- runtimec 自身：`vmkv` 编码 + `execution_window` 执行

三者路径由环境变量或 argv 提供，不硬编码。
