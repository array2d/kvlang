# rwir 委托接到 kvlang master / kvspace 0.3

8 月 PR #88 钉在当时的 C runtime 和未合入的 Notify/Take/Incr/Expire/WatchAny。
当前 master（deps.json：kvspace-c v0.3.2、kvspace-durable v0.3.0）的 ABI 只有
Get/Set/List/Del/Watch。本分支按这个 ABI 重做，不在 kvlang 里仿造那五个方法。

## 已经按 0.3 落地

- 调度：native（含 control / copy）→ 在岗委托 → def rwir handoff → 用户 call。
- 在岗：`ready|busy` 且心跳 15 秒内。否定结果缓存 250ms。
- 任务是 `/sys/rwir-backend/<b>/cmd/<task_id>` 上的一条 Char。后端 List 后 Get+Del。
  忙着的后端回头还能看见，因为键一直在。
- 完成看 `/sys/task/<id>/‥status` 变成 `done`，再逐个输出槽必须非空。
- 写槽拒绝 `/lib` `/sys` `/dev` `/done` 和别人的 `/vthread`。
- 在岗后端和同名 rwfunc 同时存在，派发拒绝。layout 写盘也拒绝 rwir 声明加 rwfunc 体。

## 还缺，属于 kvspace，不属于这里

**Incr。** 任务号是 `rwir:<backend>:<vtid>:<pid>:<boot ns>:<进程内序号>`。
status 键已经存在就 abort。这不是 Get-then-Set，也不是跨进程的原子计数。
两个进程不会撞号；进程重启后 pid 或启动时刻变了也不会撞上还在的锚点。
真要紧凑、可持久的序号，得 kvspace 提供 Incr，两边后端同一语义。

**Expire。** 成功后 `.status` 留着 `done`，List 看得到。0.3 做不到
「List 马上摘掉、Get 在 TTL 内还在」。成功即删会丢掉去重锚点，所以不删。
