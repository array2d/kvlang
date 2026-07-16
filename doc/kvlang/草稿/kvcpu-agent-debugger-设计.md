# kvcpu Agent 调试机制设计

> 草稿 · 2026-07-15

---

## 0. 定位与边界

**`kvspace` 管空间，调试机制管时间。**

| 能力 | 工具 | 本质 |
|------|------|------|
| 读/写/浏览 KV | `kvlang kvspace get/dump/tree` | 静态快照 |
| 等待/通知 KV 变化 | `kvlang kvspace watch/notify` | 被动监听 |
| **激活调试模式** | `kvlang kvspace set /vthread/<vtid>/.debug "step"` | **时序控制** |
| **追踪单步执行** | `kvlang kvspace trace <vtid>` | **NDJSON 轨迹** |
| **单步/恢复/终止** | `kvlang kvspace notify /vthread/<vtid>/.debug.resume <cmd>` | **时序控制** |

检查变量仍然用 `kvspace dump /vthread/<vtid>/...`，不重复。

---

## 1. 架构原则

### 1.1 不需要特殊启动方式

所有通过 `kvlang run`（含 `kvlang serve`）执行的程序，其 kvcpu 实例均内置调试检查点，
**无需** 使用特殊命令行参数启动，也无需重启程序即可激活调试。

> 即使程序已运行数分钟，agent 随时可以写 `.debug = "step"` 进入单步模式。

### 1.2 调试状态存储在 vthread 自身命名空间

调试相关键全部位于 `/vthread/<vtid>/` 下，以 `.` 开头（引擎保留）：

```
/vthread/<vtid>/
  .debug         调试控制键（agent 写，CPU 读）
                 ""     = 正常执行
                 "step" = 每条指令后暂停
  .debug.pause   暂停事件键（CPU Notify，agent Watch 等待）
                 值：JSON {"pc":"...","func":"...","frame":"...","op":"..."}
  .debug.resume  恢复命令键（agent Notify，CPU Watch 等待）
                 值："step"（步进）| "continue"（恢复全速）| "abort"（终止）
```

**不引入任何全局命名空间**（无 `/dbg/` 等），与现有 `/vthread/` `/func/` `/src/` `/sys/` 完全融合。

### 1.3 性能策略

| 模式 | 检查频率 | 原因 |
|------|----------|------|
| 非单步（正常运行） | 仅在函数入口（`isFuncEntryPC`） | 每次函数调用 1 次 KV 读，函数体内零开销 |
| 单步模式 | 每条指令 | 已在调试中，overhead 可接受 |

`stepping` 是 `Execute` goroutine 的局部变量，无需加锁（每个 vthread 对应一个 goroutine）。

---

## 2. 实现（核心部分）

### 2.1 keytree 键定义（`internal/keytree/vthread.go`）

```go
func VThreadDebug(vtid string) string       { return "/vthread/" + vtid + "/.debug" }
func VThreadDebugPause(vtid string) string  { return "/vthread/" + vtid + "/.debug.pause" }
func VThreadDebugResume(vtid string) string { return "/vthread/" + vtid + "/.debug.resume" }
```

### 2.2 execute.go 内联检查点

```go
stepping := false  // 局部变量，无需加锁

for {
    // ... decode ...

    // 检查点：decode 之后、dispatch 之前（KV 状态一致的时间窗口）
    if stepping || isFuncEntryPC(pc) {
        v, _ := c.kv.Get(keytree.VThreadDebug(vtid))
        switch mode := v.Str(); {
        case mode == "" && stepping:
            stepping = false                          // agent 清除标志 → 退出单步
        case mode != "":
            if !stepping { stepping = true }
            debugNotifyPause(ctx, c.kv, vtid, pc, inst)
            switch cmd := debugWaitResume(c.kv, vtid); cmd {
            case "abort":
                vthread.SetError(ctx, c.kv, vtid, pc, "debug: aborted by agent")
                return fmt.Errorf("debug: aborted by agent")
            case "continue":
                stepping = false
                c.kv.Del(keytree.VThreadDebug(vtid))  // 恢复全速
            // "step" → 保持单步
            }
        }
    }

    // ... dispatch ...
}
```

### 2.3 `kvlang kvspace trace <vtid>`

监听 `.debug.pause` 事件，输出 NDJSON，自动 step：

```go
func kvTrace(kv kvspace.KVSpace, vtid string) {
    for {
        val, err := kv.Watch(pauseKey, 10*time.Second)
        if err != nil {
            // 超时：检查 vthread 是否已终止
            if 已终止 || idle >= 3 { return }
            continue
        }
        fmt.Println(val.Str())                           // 输出 NDJSON
        kv.Notify(resumeKey, kvspace.Str("step"))        // 自动 step
    }
}
```

---

## 3. Agent 工作流

### A. 轨迹录制（全自动）

```bash
# 终端 1：启动程序
kvlang run tutorial/06-while/main.kv &

# 获取 vtid（新建 vthread 后 seq 即为 vtid）
VTID=$(kvlang kvspace get /vthread/seq)

# 激活单步模式
kvlang kvspace set /vthread/$VTID/.debug "step"

# 终端 2：开始追踪（输出每条指令的 NDJSON）
kvlang kvspace trace $VTID > /tmp/trace.ndjson
```

### B. 交互式单步检查

```bash
# 激活单步
kvlang kvspace set /vthread/$VTID/.debug "step"

# 等待第一个暂停事件
EVENT=$(kvlang kvspace watch /vthread/$VTID/.debug.pause)
echo "$EVENT" | jq .

# 检查当前帧的变量
FRAME=$(echo "$EVENT" | jq -r .frame)
kvlang kvspace dump "$FRAME"

# 步进
kvlang kvspace notify /vthread/$VTID/.debug.resume "step"

# 恢复全速
kvlang kvspace notify /vthread/$VTID/.debug.resume "continue"

# 终止
kvlang kvspace notify /vthread/$VTID/.debug.resume "abort"
```

### C. 分析轨迹

```bash
# 只看特定函数
jq 'select(.func=="first_div7")' /tmp/trace.ndjson

# 统计各函数执行次数
jq -r '.func' /tmp/trace.ndjson | sort | uniq -c | sort -rn

# 找 return 指令
jq 'select(.op=="return")' /tmp/trace.ndjson
```

---

## 4. 暂停事件 NDJSON 格式

每次暂停输出一行：

```json
{"pc":"/vthread/42/[3,0]/_fn/[1,2]","func":"first_div7","frame":"/vthread/42/[3,0]","op":"ne"}
{"pc":"/vthread/42/[3,0]/_fn/[1,3]","func":"first_div7","frame":"/vthread/42/[3,0]","op":"goto"}
```

| 字段 | 含义 |
|------|------|
| `pc` | 当前指令绝对路径 |
| `func` | 当前函数名（从 `frameRoot/.rootfunc` 读取） |
| `frame` | 当前帧根路径（可用于 `kvspace dump` 检查局部变量） |
| `op` | 即将执行的 opcode（尚未执行） |

---

## 5. 与 GDB / dlv / monkeypatch 对比

| 特性 | GDB | dlv | monkeypatch | kvlang 调试机制 |
|------|-----|-----|-------------|-----------------|
| 目标用户 | 人类 REPL | 人类 REPL | 测试框架 | **Agent / 自动化** |
| 激活方式 | 特殊启动 | 特殊启动 | 代码注入 | **写 KV 键（随时）** |
| 状态检查 | `print x` | `print x` | `assert` | `kvspace dump` |
| 单步 | `next/step` | `next/step` | — | `notify .debug.resume "step"` |
| 轨迹 | — | `trace` | — | `kvspace trace <vtid>` |
| 热替换 | 复杂 | 无 | `setattr` | `kvspace set /func/...` |
| 已运行程序 | 需 attach | 需 attach | 不可用 | **直接写 KV（无需 attach）** |

---

## 6. 高级特性（待实现 / 复杂）

### A. 函数入口断点（break:<func>）

`.debug` 写入 `"break:<funcname>"` 时，CPU 在 `isFuncEntryPC` 处比较函数名，
仅命中指定函数时才暂停，其余函数继续全速执行。

### B. 源码位置映射（PC → file:line）

需要 parser 在 AST 节点上记录 `Pos`，lower 保留，layoutcode 写入
`/src/<pkg>/<func>/<pc>` = `"file.kv:18"`。

### C. `next`（跳过函数调用）

执行到当前帧深度不增加为止：
watch `.debug.pause` 直到 `strings.Count(pc, "/_fn/") == startDepth`。

### D. Patch 模式（函数热替换）

```bash
# 直接通过 kvspace 修改已加载函数的指令序列
kvlang kvspace set /func/main/classify/[0,0] '"PATCHED"'
kvlang kvspace notify /vthread/$VTID/.debug.resume "continue"
```
