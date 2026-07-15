# kvspace 设计问题

## P1-5 `links` 进程内缓存在多实例场景下会过期失效（当前可接受）
**文件**：`redis/redis.go`

**分析**：多 VM 进程共享同一 Redis 时，进程 A 的 `links` 缓存不感知进程 B 的 Unlink。
**结论**：当前架构下实际无风险——vthread 由单个 worker 进程执行到底（HandleCall/Unlink
在同一进程内完成），不存在跨进程 link 缓存竞争。若未来引入 vthread 迁移（work stealing），
需要 Redis keyspace notification 或去掉 link 缓存。

## P1-6（剩余）单条 `Set` 仍逐层发送独立 SADD
`SetMany` 已用 pipeline 优化。单条 `Set` 对 pipeline 额外开销大于收益，维持现状合理。
若性能成瓶颈，可考虑将相邻 `Set` 调用点合并为 `SetMany`。
