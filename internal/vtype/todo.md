# vtype 设计问题

## P0-7 `strVType` 是永远报错的死代码
**文件**：`str.go`  
`strVType.Exec` 始终调用 `vthread.SetError` 并返回错误。
`str.set` 被 `IsNativeOp` 优先拦截，永远不会到达 `strVType`。
其他 `str.*` 操作同样无法工作。

两种处理方式选其一：
1. **删除** `str.go`，将 `str.set`（改名为 `tostr`/`str.from`）完全留在 `builtin` 层。
2. **补全**：将 str 相关操作（`str.from`, `str.len`, `str.concat`, `str.slice`）
   实现在 `strVType.Exec` 中，并从 `nativeOps` 中移除 `str.set`，统一走 vtype 路径。
