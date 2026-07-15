# kvcpu 设计问题

## P0-2 `isCopyOp` 内嵌了与 `builtin.isImmediateNumber` 重复的判断逻辑
**文件**：`execute.go`  
`isCopyOp` 中逐字符扫描 `[0-9.eE]` 判断数字字面量，与 `internal/op/builtin/resolve.go`
中的 `isImmediateNumber` 完全一致。应直接调用 `builtin.IsImmediateNumber`
和 `builtin.IsImmediateBool`（需将这两个函数导出）。

## P0-3 `isCopyOp` 不支持负数字面量
**文件**：`execute.go`  
`-3 -> ./x` 无法被识别为 copy 操作，`-` 不在允许字符集中。
负数字面量会落入 default 分支，被当作用户函数调用，产生 SetError。
需支持可选前导 `-` 号。

## P1-1 `ctx context.Context` 传入但从不使用
**文件**：`execute.go`, `controlflow.go`  
`handleControl`, `brToCall`, `gotoBlock` 均接收 `ctx`，
但所有 KV 操作使用包级 `bg = context.Background()`，ctx 取消/超时对 KV 操作无效。
**解决方案**：全部移除 ctx 参数，或真正将 ctx 传入 KV 操作（需修改 KVSpace 接口）。
