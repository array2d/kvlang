# kvcpu todo

> 对齐目标：`最高标准设计.md`

---

## ✅ P1 vthread 状态存储去 JSON 化
**完成**：`internal/vthread/vthread.go` 全部重写。

---

## ✅ P2 PC 改为绝对路径
**完成**：`CreateVThread` 写绝对 PC；`keytree.VtidFromPC` 新增。

---

## ✅ P3 Execute 签名：vtid → pc
**完成**：`Execute(pc string) error`；`op.Decode` 去掉 vtid 参数。

---

## ✅ P4 CPU interface 落地
**完成**：新建 `kvcpu/cpu.go`；`pick`/`wait` 私有；`New(kv, vmID) CPU`。

---

## ✅ P6 栈深度保护
**完成**：Execute 循环内检查 `stackDepth(pc) > MaxStackDepth`，超限 SetError。

---

## ✅ P12 MaxStackDepth 统一为 256
**完成**：`execute.go` 改为 256，同步 `最高标准设计.md §十`。

---

## ✅ P11 PC 更新契约文档化
**完成**：`vthread.Set` / `SetDone` / `SetError` godoc 添加"所有 PC 变更必须经由此函数"说明。

---

## P8 vthread GC goroutine
**现状**：done/error 的 vthread 不自动清理，kvspace 会堆积。
**标准**：独立 goroutine 定期扫描，TTL 到期后 `kv.DelR`。

涉及文件：新建 `internal/kvcpu/gc.go`，`CPU` interface 添加 `RunGC()`，`serve.go` 启动

---

## 【layoutcode 职责】P5 HandleCall Copy → Link

**归属**：`internal/layoutcode/layoutcode.go`
**标准**：将 `copyFunc`（逐槽复制）改为 `kv.Link(/func/<pkg>/<name>, pc)` 零拷贝挂载，
`kv.DelR(framePath)` 原子销毁。
**前提**：需验证 kvspace.Link 在帧路径下的解析正确性。

---

## 【layoutcode 职责】P7 unknown opcode 通知 error worker

**归属**：`internal/layoutcode/layoutcode.go`
**标准**：`HandleCall` 内 FuncIdx 失败时，额外调用
`kv.Notify(keytree.SysVMErr(vmID), errPayload)` 通知注册的 error worker。
**依赖**：HandleCall 需接收 vmID 参数（或通过闭包注入）。

---

## 【lower 职责】P10 OpIf 兼容路径移除

**归属**：`internal/lower/lower.go` + `internal/kvcpu/controlflow.go`
**标准**：`lower` 确保所有 `if` 指令在写入 `/func/` 前降级为 `br`；
降级完成后删除 `controlflow.go` 中的 `OpIf` 兼容 case。
