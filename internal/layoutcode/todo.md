# layoutcode 设计问题

## P0-8 块标签被强制包装为伪函数签名
**文件**：`layoutcode.go:RegisterBlocks`  
编译器生成的块（`_then_1`, `_merge_2`）写入函数注册表时伪造了签名字符串：
```go
kv.Set(blockKey, kvspace.Str("def "+b.Label+"() -> ()"))
```
块不是函数，是 TCO 跳转目标，不应在函数注册表中用签名来表示。
解决方向：为块标签单独建立索引（如 `/func/block/idx/<label>`），
与函数签名索引（`/func/idx/<name>`）分离。

## P1-1 `ctx context.Context` 传入但从不使用
**文件**：`layoutcode.go`  
`HandleCall`, `HandleReturn` 均接收 `ctx`，但所有 KV 操作使用包级 `bg`，
ctx 的取消/超时对 KV 操作无效。
**解决方案**：全部移除 ctx 参数，或真正将 ctx 传入 KV 操作。

## P1-11 `kv.Set` 错误在写指令/帧元数据时被全量忽略
**文件**：`layoutcode.go:writeStmt`, `RegisterBlocks`, `WriteFunc`  
所有 `kv.Set(...)` 丢弃错误返回值。Redis 写失败时 VM 静默执行错误状态。
应在 `WriteFunc` 等批量写入入口处检查错误，或改用 `SetMany` 并统一检查。
