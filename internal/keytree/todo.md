# keytree todo — 现状与最高标准设计之间的差距

> 最高标准设计见 `最高标准设计.md`。本文档只记录**当前代码与标准之间的偏差**，
> 以及完成修复所需的具体改动。每项完成后标记 **DONE**。

---

## 路径偏差速览

| 当前路径/名称 | 标准路径/名称 | 违反原则 |
|-------------|-------------|---------|
| `/vthread/` 根 | `/vthread/` 根 | 名字偏实现，不类 Unix |
| `/notify/vm` | `/vthread/ready` | 通知散落到独立根，不归属被通知方 |
| `/done/<vtid>` | `/vthread/<vtid>/done` | 完成通知散落，不在 vtid 路径下 |
| `/sys/vtid_counter` | `/vthread/seq` | 计数器不在它所计数的命名空间下 |
| `/sys/heartbeat/vm:<id>` | `/sys/vm/<id>/hb` | `:` 做分隔符；属性不在所有者路径下 |
| `/sys/cmd/vm/<id>` | `/sys/vm/<id>/cmd` | 路径语义倒置；命令队列不在 VM 路径下 |
| `cmd:<instance>` | `/sys/op/<backend>/<n>/.cmd` | `:` 做分隔符；无 `/` 前缀 |
| `/sys/op-plat/<instance>` | `/sys/op/<backend>/<n>` | 连字符；instance 名含 `:` |
| `/sys/heap-plat/<instance>` | `/sys/heap/<backend>/<n>` | 同上 |
| `/op/<backend>/list` | `/sys/op/<backend>/func/`（List 枚举） | 独立根 `/op/`，游离于 sys/ 之外 |
| `/op/<backend>/func/<name>` | `/sys/op/<backend>/func/<name>` | 同上 |
| `/sys/term/<name>/<stream>` | `/dev/tty/<name>/<stream>` | 终端是设备，属于 /dev/；Unix 叫 tty 不叫 term |
| `/func/main` | `/func/main` | 入口描述符是元数据，缺少点前缀 |
| `VthreadRoot` 常量 | `VthreadRoot` | API 命名不对齐 |
| `VThread*(...)` 函数族 | `Proc*(...)` 函数族 | 同上 |
| `VThreadSub` 末尾补 `/` | `VThreadFrame`，不补 `/` | 路径不含结尾斜杠 |
| `FuncCompiled(pkg,name)` | `Func(pkg,name)` | "Compiled" 是实现词，路径层不感知编译 |
| `SrcFunc(pkg,name)` | `Src(pkg,name)` | 同上 |

---

## P1：重命名 keytree 包内 API（不改路径值）

**范围**：`internal/keytree/*.go` + 所有 callsite  
**风险**：纯重命名，编译必须始终通过  
**状态**：待完成

### keytree 包内改动

| 文件 | 旧名 | 新名 |
|------|------|------|
| `entry.go` | `FuncCompiled(pkg, name)` | `Func(pkg, name)` |
| `entry.go` | `FuncMain = "/func/main"` | 路径值不变，P2 再改 |
| `src.go` | `SrcFunc(pkg, name)` | `Src(pkg, name)` |
| `vthread.go` | `VthreadRoot` | `VthreadRoot` |
| `vthread.go` | `VThread(id)` | `VThread(id)` |
| `vthread.go` | `VThreadAt(id, key)` | `ProcAt(id, key)`（过渡名，P2 再细化） |
| `vthread.go` | `VThreadSub(id, pc)` | `ProcFrame(id, pc)`（去掉末尾 `/`） |
| `vthread.go` | `VThreadSlot(id, a, b)` | `ProcSlot(id, "", a, b)` |
| `vthread.go` | `VThreadTerm(id)` | `VThreadTerm(id)` |
| `vthread.go` | `VThreadPattern()` | 删除（调用方直接用 `VthreadRoot`） |
| `notify.go` | `NotifyRoot`, `NotifyVM` | 路径值不变，P2 再改；先保留 |
| `notify.go` | `DoneRoot`, `Done(id)` | 路径值不变，P2 再改；先保留 |
| `sys.go` | `SysVtidCounter` | 路径值不变，P2 再改 |
| `sys.go` | `SysOpPlatRoot`, `SysOpPlatInst` | `SysOp(backend, n)` |
| `sys.go` | `SysHeapPlatRoot`, `SysHeapPlatInst` | `SysHeap(backend, n)` |
| `sys.go` | `CmdQueue(instance)` | `SysOpCmd(backend, n)` |
| `sys.go` | `SysTerm(name, stream)` | `DevTTY(name, stream)`（移到新文件 dev.go）|
| `op.go` | `OpBackendFunc(backend, name)` | `SysOpFunc(backend, name)` |
| `op.go` | `OpBackendList(backend)` | 删除（`List(SysOpFunc(b,""))` 即可枚举）|
| `op.go` | `OpPattern()` | 删除 |

### callsite 改动

| 文件 | 旧调用 | 新调用 |
|------|--------|--------|
| `internal/layoutcode/layoutcode.go` | `FuncCompiled` | `Func` |
| `internal/layoutcode/layoutcode.go` | `FuncIdx` | `FuncIdx`（不变） |
| `internal/vthread/vthread.go` | `VThread(vtid)` | `Proc(vtid)` |
| `internal/vthread/vthread.go` | `VThreadSlot(vtid,a,b)` | `ProcSlot(vtid,"",a,b)` |
| `internal/vthread/vthread.go` | `Done(vtid)` | 路径不变，P2 再迁 |
| `internal/kvcpu/sched.go` | `VthreadRoot` | `VthreadRoot` |
| `internal/kvcpu/sched.go` | `VThread(vtid)` | `Proc(vtid)` |
| `internal/kvcpu/sched.go` | `NotifyVM` | 路径不变，P2 再迁 |
| `internal/kvcpu/controlflow.go` | `VThreadAt(vtid,key)` | `ProcAt(vtid,key)` |
| `internal/kvcpu/controlflow.go` | `VThreadSlot(vtid,0,0)` | `ProcSlot(vtid,"",0,0)` |
| `internal/kvcpu/controlflow.go` | `FuncIdx` | `FuncIdx`（不变） |
| `internal/op/builtin/helper.go` | `VThreadAt` | `ProcAt` |
| `internal/op/dispatch/dispatch.go` | `VThreadAt` | `ProcAt` |
| `internal/op/dispatch/router.go` | `SysOpPlatRoot`, `SysHeapPlatRoot` | `SysOp`, `SysHeap` |
| `internal/op/dispatch/router.go` | `CmdQueue` | `SysOpCmd` / `SysHeapCmd` |
| `internal/op/dispatch/router.go` | `OpBackendFunc`, `OpBackendList` | `SysOpFunc` |
| `internal/device/term_ws.go` | `SysTerm` | `DevTTY` |
| `cmd/kvlang/serve.go` | `SysVM`, `SysHeartbeat`, `SysCmdVM` | `SysVM`, `SysVMHB`, `SysVMCmd` |
| `cmd/kvlang/serve.go` | `VThread`, `VThreadSlot`, `VThreadTerm` | `Proc`, `VThreadSlot`, `ProcTerm` |
| `cmd/kvlang/serve.go` | `NotifyVM` | 路径不变，P2 再迁 |
| `cmd/kvlang/serve.go` | `FuncMain` | 路径不变，P2 再迁 |
| `cmd/kvlang/util.go` | `SysVtidCounter` | 路径不变，P2 再迁 |

---

## P2：迁移路径值（API 名称已对齐后执行）

**依赖**：P1 完成  
**风险**：路径变更需要 redis 中数据清空重新 load（开发环境 `FLUSHALL` 即可）  
**状态**：待完成

### 路径值变更表

| 常量/函数 | 当前路径值 | 标准路径值 |
|----------|-----------|-----------|
| `FuncMain` | `/func/main` | `/func/main` |
| `VThread(id)` | `/vthread/<id>` | `/proc/<id>` |
| `VthreadRoot` | `/vthread` | `/proc` |
| `VthreadSeq`（新建） | — | `/vthread/seq` |
| `VthreadReady`（新建） | — | `/vthread/ready` |
| `VThreadDone(id)`（新建） | — | `/proc/<id>/.done` |
| `NotifyVM`（弃用） | `/notify/vm` | 合并到 `VthreadReady` |
| `Done(id)`（弃用） | `/done/<id>` | 合并到 `VThreadDone(id)` |
| `SysVtidCounter`（弃用） | `/sys/vtid_counter` | `ProcSeq = /proc/.seq` |
| `SysVMHB(id)` | `/sys/heartbeat/vm:<id>` | `/sys/vm/<id>/hb` |
| `SysVMCmd(id)` | `/sys/cmd/vm/<id>` | `/sys/vm/<id>/cmd` |
| `SysOpCmd(b,n)` | `cmd:<instance>` | `/sys/op/<backend>/<n>/.cmd` |
| `SysOp(b,n)` | `/sys/op-plat/<instance>` | `/sys/op/<backend>/<n>` |
| `SysHeap(b,n)` | `/sys/heap-plat/<instance>` | `/sys/heap/<backend>/<n>` |
| `SysOpFunc(b,name)` | `/op/<backend>/func/<name>` | `/sys/op/<backend>/func/<name>` |
| `DevTTY(name,stream)` | `/sys/term/<name>/<stream>` | `/dev/tty/<name>/<stream>` |

### 涉及的 callsite 文件（路径值变更后需验证）

- `cmd/kvlang/serve.go`：`FuncMain`、`NotifyVM` → `VthreadReady`、`Done` → `ProcDone`
- `cmd/kvlang/util.go`：`SysVtidCounter` → `VthreadSeq`
- `internal/kvcpu/sched.go`：`NotifyVM` → `VthreadReady`
- `internal/vthread/vthread.go`：`Done(vtid)` → `ProcDone(vtid)`
- `internal/op/dispatch/dispatch.go`：`CmdQueue` → `SysOpCmd`/`SysHeapCmd`
- `internal/op/dispatch/router.go`：`SysOpPlatRoot` → `SysOp`，`SysHeapPlatRoot` → `SysHeap`
- `internal/device/term_ws.go`：`SysTerm` → `DevTTY`

---

## P3：删除弃用 API 和弃用文件

**依赖**：P2 完成，集成测试通过  
**状态**：待完成

- 删除 `keytree/notify.go`（`NotifyRoot`、`NotifyVM`、`DoneRoot`、`Done` 已被替代）
- 删除 `keytree/op.go`（`OpRoot`、`OpBackendFunc`、`OpBackendList`、`OpPattern` 已被替代）
- 删除 `keytree/vthread.go`（全部迁移到 `keytree/proc.go`）
- 删除 `sys.go` 中 `SysVtidCounter`、`SysOpPlatRoot`、`SysOpPlatInst`、`SysHeapPlatRoot`、`SysHeapPlatInst`、`CmdQueue`、`SysTerm`
- 验证：`grep -r 'VThread\|NotifyVM\|DoneRoot\|OpBackend\|op-plat\|heap-plat\|vtid_counter' --include='*.go'` 结果为空

---

## P4：HandleCall 用 Link 替代 Copy

**依赖**：P2 完成（路径已对齐）  
**影响**：执行语义变更，需完整集成测试  
**状态**：待完成

### 问题

当前 `internal/layoutcode/layoutcode.go` 的 `HandleCall` 将 `/func/<pkg>/<name>/[i,j]`
**逐槽复制**到 `/vthread/<vtid>/<frame>/[i,j]`，并在复制时做参数替换（形参 → 实参路径）。

这破坏了设计标准中的核心优雅性：
- 代码在每个活跃帧都有一份副本，浪费空间
- 返回时需逐槽删除，而不是一次 `DelR`
- 概念上"帧 = 代码链接 + 局部参数槽"的纯洁性丢失

### 标准模型（Link-based）

```
call f(x=3, y=4) -> result          在 frame=/vthread/1/main 中执行

1. Link  /func/math/f  →  /vthread/1/main/f        挂载代码（零拷贝）
2. Set   /vthread/1/main/f/x  =  3                 绑定实参
3. Set   /vthread/1/main/f/y  =  4
   PC = /vthread/1/main/f/[0,0]                    跳入

f 执行完毕，return：
4. Get   /vthread/1/main/f/z        →  读取返回值
5. Set   /vthread/1/main/result  =  <z 的值>       写回调用方槽
6. DelR  /vthread/1/main/f                         销毁帧（含 Link 和所有局部槽）
   PC 回到 /vthread/1/main 的下一条指令
```

**不变式**：路径深度 = 调用栈深度。`/vthread/1/main/calc/add` 即是调用链，无需额外 stack 结构。

### 涉及文件

| 文件 | 当前做法 | 标准做法 |
|------|---------|---------|
| `internal/layoutcode/layoutcode.go` `HandleCall` | `copyFunc` 逐槽复制 + 参数替换 | `kv.Link(funcPath, framePath)` + `kv.Set` 绑参 |
| `internal/layoutcode/layoutcode.go` `HandleReturn` | 逐槽删除子栈 | `kv.DelR(framePath)` |
| `internal/layoutcode/layoutcode.go` `copyFunc` | 递归复制 | 整函数删除 |
| `internal/kvcpu/controlflow.go` `resolveLabel` | 基于复制后的槽查找 | 基于 Link 后的路径查找（逻辑不变） |

---

## 优先级

| 编号 | 内容 | 影响 | 状态 |
|------|------|------|------|
| P1 | API 重命名（路径值不变） | 无运行时影响，纯编译期 | 待完成 |
| P2 | 路径值迁移 | 需重启 serve + redis flush | 待完成 |
| P3 | 删除弃用 API | 清理 | 待完成 |
| P4 | HandleCall Copy → Link | 执行语义变更，需集成测试 | 待完成 |
