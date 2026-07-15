# vthread 设计问题

## P1-1 `ctx context.Context` 传入但从不使用
**文件**：`vthread.go`  
`Set`, `SetDone`, `SetError`, `CreateVThread`, `WaitDone` 均接收 `ctx`，
但所有 KV 操作使用包级 `bg = context.Background()`，ctx 的取消/超时对 KV 操作无效。
**解决方案**：全部移除 ctx 参数，或真正将 ctx 传入 KV 操作（需修改 KVSpace 接口）。

## P1-2 `CreateVThread` 使用 `time.Now().UnixNano()` 生成 vtid
**文件**：`vthread.go`  
与 `cmd/kvlang/serve.go:mainWatcher` 使用的 `incrVtid(kv)` 策略并存。
`UnixNano` 在高并发下可碰撞（同一纳秒内多次调用）。
应统一成单一策略：Redis `INCR` 原子自增（需在 KVSpace 接口增加 `Incr` 原语，
或直接用 `kv.Notify` + 序列号的方式）。
