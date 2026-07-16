# kvlang 系统栈 `.` 键全量梳理

> 草稿 · 2026-07-15
> 通过调试器在运行时实际 dump 验证（`fib(3)` 递归至 depth=2 时快照）

---

## 0. 约定

kvlang 标识符不能以 `.` 开头，因此 **所有 `.` 前缀键均为引擎保留**，
用户 kv 代码无法读写（类比 Linux 隐藏文件）。

`./` 前缀是**相对路径引用**（用户代码的变量），与此处的 **`.` 系统键**不同。

---

## 1. vthread 根层键

路径前缀：`/vthread/<vtid>/`（vtid 为时间戳或固定字符串）

| 键 | 类型 | 写入方 | 读取方 | 说明 |
|----|------|--------|--------|------|
| `.pc` | String | `vthread.Set` / `vthread.SetError` | `Execute` 循环末尾 | 当前指令绝对 PC |
| `.status` | String / Notify | `vthread.Set` → Del+Notify（终态） | `Execute` 循环状态检查 | 生命周期：`init`\|`running`\|`wait`；终态：Del+Notify(retVal) |
| `.<statusVal>/msg` | String | `vthread.SetError` | 外部监控 / agent | 终态附加描述，路径动态生成：`.error/msg`、`.timeout/msg` 等 |
| `.rootfunc` | String | `layoutcode.Bootstrap` | `resolveLabel`（TCO label 解析） | 入口函数名，仅顶层帧（Bootstrap 写入，HandleCall 不覆写此层） |
| `.debug` | String | agent（`kvspace set`） | `Execute` 循环（`isFuncEntryPC` 处） | 调试控制：`""` = 正常，`"step"` = 单步 |
| `.debug.pause` | Notify 队列 | `kvcpu/debug.go:debugNotifyPause` | agent（`kvspace watch` / `kvspace trace`） | 暂停事件 JSON；非持久键，Notify/Watch 语义 |
| `.debug.resume` | Notify 队列 | agent（`kvspace notify`） | `kvcpu/debug.go:debugWaitResume` | 恢复命令：`"step"` / `"continue"` / `"abort"` |

**特别说明：**

- `.status` 有两种 Redis 类型：
  - 运行中：String（`init` / `running` / `wait`），可 `kv.Get`
  - 终态后：**List**（LPUSH by Notify），原 String 先被 Del，再 LPUSH 终态值
    - `abort` → list 内容 = `encode("error")`
    - 正常结束 → list 内容 = `encode("ok")`
    - 对终态 `.status` 调用 `kv.Get` 会得到 Redis `WRONGTYPE` 错误
    - `vthread.WaitDone` 使用 `kv.Watch`（BLPOP）消费此 list

- `.debug.pause` / `.debug.resume` 同样是 Notify 队列（LPUSH/BLPOP），实时消费后消失，不可 `Get`

---

## 2. 子帧层键

路径前缀：`<frameRoot>/`，其中 `frameRoot` = `/vthread/<vtid>/[i,j]`（或更深嵌套）

| 键 | 类型 | 写入方 | 读取方 | 说明 |
|----|------|--------|--------|------|
| `.callpc` | String | `layoutcode.HandleCall` | `layoutcode.HandleReturn` | 调用指令的绝对地址；HandleReturn 从此处推算写槽路径（`[addr0,1]`, `[addr0,2]`…）并恢复父 PC |
| `.rootfunc` | String | `layoutcode.HandleCall` | `kvcpu/controlflow.go:resolveLabel` | 函数名；TCO（goto/br）复用帧时**不更新**，保持递归入口函数名 |

**写槽说明（无额外系统键）：**

kvlang 是纯读写码，函数调用 `add(x,y) -> ./s` 的 `-> ./s` 是调用方指定的写目标路径，
该路径已存储在调用指令本身（`[addr0,1]`、`[addr0,2]`…）。  
HandleReturn 通过 `.callpc` 推导写槽路径（`op.WriteSlotPC`），直接读取后将被调方局部变量写入父帧——
没有"返回值"，只有写槽绑定，也不需要额外的 `.w{N}` 系统键。

---

## 3. 运行时快照（fib(3) depth=2，实测）

```
/vthread/mrd/.debug         string:step                ← agent 写
/vthread/mrd/.pc            string:/vthread/mrd/[0,0]/[1,0]/.fn/[0,0]
/vthread/mrd/.rootfunc      string:main                ← Bootstrap 写，入口函数
/vthread/mrd/.status        string:running

/vthread/mrd/[0,0]/.callpc  string:/vthread/mrd/.fn/[0,0]
/vthread/mrd/[0,0]/.rootfunc string:fib

/vthread/mrd/[0,0]/[1,0]/.callpc   string:/vthread/mrd/[0,0]/.fn/[1,0]
/vthread/mrd/[0,0]/[1,0]/.rootfunc string:fib
```

写槽路径不再作为系统键存储，HandleReturn 直接从 `.callpc` 指令坐标推算：
- `[0,0]` 的写槽 → `/vthread/mrd/.fn/[0,1]`（经 parent 的 `.fn` 链接解析）
- `[0,0]/[1,0]` 的写槽 → `/vthread/mrd/[0,0]/.fn/[1,1]`

---

## 4. 顶层帧 vs 子帧的差异

| 键 | 顶层帧（Bootstrap 建立） | 子帧（HandleCall 建立） |
|----|--------------------------|------------------------|
| `.pc` | ✅ 存在 | ❌ 不存在（PC 在根层统一维护） |
| `.status` | ✅ 存在 | ❌ 不存在 |
| `.<statusVal>/msg` | ✅ 终态时写入 | ❌ 不存在 |
| `.rootfunc` | ✅ 存在（入口函数名） | ✅ 存在（被调函数名，TCO 不更新） |
| `.callpc` | ❌ 不存在 | ✅ 存在 |
| `.fn` (软链接) | ✅ 存在 | ✅ 存在 |
| `.debug` | ✅ 存在（agent 写） | ❌ 不存在 |
| `.debug.pause` | ✅ Notify 队列 | ❌ 不存在 |
| `.debug.resume` | ✅ Notify 队列 | ❌ 不存在 |

**判断方式：** `frameRoot == vthreadRoot` → 顶层帧（`HandleReturn` 用此条件决定调用 `SetDone` 还是恢复父 PC）

---

## 5. 所有 `.` 前缀键速查表

| 键名 | 所在层 | 语义 |
|------|--------|------|
| `.fn` | 每一帧 | 软链接 → `/func/<pkg>/<name>`，只读指令区；TCO 时重链 |
| `.pc` | vthread 根 | 当前执行 PC |
| `.status` | vthread 根 | 生命周期状态 |
| `.rootfunc` | 每一帧 | 帧对应的函数名（TCO 时根帧不更新） |
| `.callpc` | 子帧 | 调用该帧的指令绝对路径，用于 return 后恢复 |
| `.debug` | vthread 根 | 调试模式控制 |
| `.debug.pause` | vthread 根 | 暂停事件 Notify 队列 |
| `.debug.resume` | vthread 根 | 恢复命令 Notify 队列 |
| `.<status>/msg` | vthread 根 | 终态附加信息（`.error/msg` 等） |

---

## 6. 写入 / 清理时序

```
Bootstrap:      写 .rootfunc, .pc, .status(init)，Link .fn
Execute 启动:   改 .status → running
HandleCall:     子帧写 .callpc, .rootfunc，绑定参数（裸名），Link .fn
TCO (goto/br):  Unlink + Re-Link .fn，不写 .callpc（.rootfunc 不变）
HandleReturn:   读 .callpc → 推算写槽路径 [addr0,i+1] → 写入父帧，Unlink .fn, DelTree frameRoot
SetDone:        Del .status, Notify .status(retVal)
SetError:       写 .error/msg, Del .status, Notify .status("error")
Debug pause:    Notify .debug.pause(JSON)
Debug resume:   Watch .debug.resume
Debug clear:    Del .debug（"continue" 命令后）
```
