# cmd/kvlang 设计问题

## P1-2 双重 vtid 生成策略
**文件**：`serve.go` + `internal/vthread/vthread.go`  
- `vthread.CreateVThread` 使用 `time.Now().UnixNano()`
- `mainWatcher` 使用 `incrVtid(kv)`（读改写计数器）

两种策略并存，语义不一致。应统一成一种：Redis `INCR` 原子自增。

## P1-3 `incrVtid` 非原子读改写
**文件**：`util.go`  
```go
valV, _ := kv.Get(keytree.VthreadSeq)
n, _ := strconv.ParseInt(valV.Str(), 10, 64)
n++
kv.Set(keytree.VthreadSeq, kvspace.Str(...))
```
GET → incr → SET 之间无锁，多个 worker 并发时生成重复 vtid。
Redis `INCR` 命令原子实现此语义，需在 KVSpace 接口增加 `Incr(key) (int64, error)` 原语。

## P1-7 JSON 序列化用于 VM 元数据存储，违反"value = 标量"原则
**文件**：`serve.go`  
`registerVM`, `heartbeatLoop`, `registerBuildinOps`, `sysCmdListener` 将结构体 JSON marshal
后存入单个 KV key，读取时再 `json.Unmarshal`。
这违反了 kvspace "key = 命名路径，value = 标量字符串"的设计原则（参见 vthread 包注释）。
VM 状态字段应展开为独立 KV 键：
```
/sys/vm/<id>/status   = "running"
/sys/vm/<id>/pid      = "1234"
/sys/vm/<id>/started  = "1720000000"
```

## P1-8 `mainWatcher` 1 秒轮询替代事件通知
**文件**：`serve.go`  
```go
ticker := time.NewTicker(1 * time.Second)
entryVal, err := kv.Get(keytree.FuncMain)
```
定时轮询 `/func/main`，最多引入 1 秒延迟且 CPU 空转。
正确设计：`kvlang load` 写入后 `kv.Notify(keytree.FuncMain, vtidVal)` 触发，
`mainWatcher` 改用 `kv.Watch(keytree.FuncMain, ...)` 阻塞等待，零延迟。

## P1-9 `executeEntry` 硬编码 vtid = `"run"`
**文件**：`serve.go`  
```go
const vtid = "run"
```
单次执行模式固定用 `"run"` 作为 vtid，不走统一的 vtid 生成路径。
两种模式（single/serve）的 vthread 初始化代码因此不一致，无法复用。
应使用统一的 `incrVtid` 或 `Incr` 生成 vtid。
