# kvspace 设计问题

## ✅ P0-1 `floatbits.go` 是无意义的封装层
**已修复**：删除 `floatbits.go`，在 `encode.go` 中直接 `import "math"`，
使用 `math.Float64bits` / `math.Float64frombits`。

## ✅ P0-4 `resolveCore` O(n²) 路径前缀扫描 → 用 `strings.IndexByte` 优化
**已修复**：将内层逐字符扫描替换为 `strings.IndexByte`（Go runtime 有 SIMD 优化）。
逻辑提取为 `kvspace.ResolveCore`（公共函数），供 `memImpl` 和 `redisImpl` 共用。

## ✅ P0-5 `""` 作为否定缓存与"未检查"用 map presence 区分，语义混淆
**已修复**（`redis/redis.go`）：`links map[string]string` → `links map[string]linkEntry`，
其中 `linkEntry{checked bool; target string}` 显式区分三种状态：
- `{checked:false}` — 零值，尚未检查
- `{checked:true, target:""}` — 确认非链接（否定缓存）
- `{checked:true, target:"x"}` — 链接，目标为 `x`

注：`memImpl` 仍使用 `map[string]string`（否定缓存语义相同）。

## ✅ P1-6 `SetMany` 中 N 对 key 各自独立 SADD → pipeline 批量
**已修复**（`redis/redis.go`）：`SetMany` 改用 `r.rdb.Pipeline()`，
新增 `pipeIndex` 将所有 key 的层级索引维护命令追加到 pipeline，
最终 `pipe.Exec` 单次执行，N 次 round trip → 1 次。

## ✅ Redis 实现移入 `kvspace/redis/` 子包；注册模式 + 环境变量选后端
- `internal/kvspace/redis.go` → `internal/kvspace/redis/redis.go`（package `redis`）
- `redis.init()` 调用 `kvspace.Register("redis", ConnPool)`
- `kvspace.conn.go`：`Conn/ConnPool` 读取 `KVLANG_KV` 环境变量（默认 `"redis"`）选择后端
- `cmd/kvlang/main.go` 增加 `_ "kvlang/internal/kvspace/redis"` 空白导入触发注册
- 所有调用方（`kvspace.go / load.go / serve.go / op_test.go`）改为 `kvspace.Conn/ConnPool`
- `ResolveCore` 公共函数留在 `kvspace` 包，子包可直接调用无需循环导入
- **已验证**：`go build ./...` 和 `go test ./...` 全部通过

---

## P1-5 `links` 进程内缓存在多实例场景下会过期失效（已分析，当前可接受）
**文件**：`redis/redis.go`

**分析**：多 VM 进程共享同一 Redis 时，进程 A 的 `links` 缓存不感知进程 B 的 Unlink。
**结论**：当前架构下实际无风险——vthread 由单个 worker 进程执行到底（HandleCall/Unlink 
在同一进程内完成），不存在跨进程 link 缓存竞争。若未来引入 vthread 迁移（work stealing），
需要 Redis keyspace notification 或去掉 link 缓存。

## P1-6（剩余）`maintainIndex` 为每次 `Set` 发出独立 SADD 命令
`SetMany` 已优化为 pipeline。单条 `Set` 仍逐层发送独立 SADD，
但 pipeline 对单 key 场景额外开销大于收益，维持现状合理。
若性能成瓶颈，可考虑合并相邻 Set 为 SetMany 的调用点重构。
