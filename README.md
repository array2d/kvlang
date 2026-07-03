# kvlang

基于 Redis KV 空间的声明式图解释器。Livebyte 运行时的核心引擎。

## 定位

```
livebyte（语言+工具链）
    │
    ▼
kvlang（解释执行引擎）  ← 本项目
    │
    ├── VM        图遍历、模板解析、拓扑排序、上下文压缩
    ├── Scheduler LLM API 网关：优先级队列、速率限制、重试熔断
    └── Workers   执行器：bash/file/http/llm/search/browser/subagent
    │
    ▼
Redis（KV 存储）    所有状态持久化：fn/graph/session/tool/worker/lock
```

## 核心概念

**kvlang 不编程，只解释。** 输入是编译后的函数图（JSON），输出是执行结果。
语言层（LBL 语法、编译优化）属于 `livebyte` 项目，kvlang 只负责跑图。

```
 LBL 函数（YAML）                   编译图（JSON）
┌─────────────────┐     lbc      ┌──────────────────┐     kvlang
│ fn: review      │  ────────▶   │ nodes / edges    │  ──────────▶  结果
│ graph: ...      │   编译       │ deps / status    │    解释执行
└─────────────────┘              └──────────────────┘
```

## 安装

```bash
pip install kvlang
# 需要在本地或远端运行 Redis（默认 localhost:6379）
```

## 快速开始

```bash
# 启动 Worker
kvlang worker start --all

# 启动调度器
kvlang scheduler start

# 提交图执行
kvlang run --fn code_review --input path=src/main.py

# 查看状态
kvlang status --graph graph:abc123
```

## 命令

| 命令 | 说明 |
|------|------|
| `kvlang run` | 提交函数图到 Redis，VM 自动开始执行 |
| `kvlang worker` | 启动/停止各类 Worker |
| `kvlang scheduler` | 启动调度器（LLM API 网关） |
| `kvlang status` | 查看图执行进度、Worker 负载 |
| `kvlang inspect` | 导出 Redis 中任意 key 的完整状态 |
| `kvlang compact` | 手动触发某个 session 的上下文压缩 |
| `kvlang clean` | 清理已完成的图和过期 session |

## Redis Schema

```
kvlang:fn:{name}            函数定义
kvlang:graph:{id}           执行中的图实例
kvlang:graph:{id}:nodes     HASH  node_id → {status,output,elapsed}
kvlang:graph:{id}:events    STREAM  执行事件流
kvlang:session:{id}         HASH  对话 session
kvlang:session:{id}:msgs    LIST  消息历史
kvlang:tool:{name}          HASH  工具注册信息
kvlang:worker:{id}          HASH  Worker 状态与心跳
kvlang:worker:{id}:queue    LIST  任务队列
kvlang:scheduler:queue      ZSET  优先级队列
kvlang:lock:{resource}      STRING  分布式锁
```

## 架构

```
                    ┌────────────────────────┐
                    │        kvlang          │
                    │                        │
  编译图 ──────────▶│  ┌──────────────────┐  │
  (from Redis)     │  │       VM         │  │
                    │  │  拓扑排序+执行    │  │
                    │  └──────┬───────────┘  │
                    │         │               │
                    │    ┌────┴────┐          │
                    │    │ dispatch│          │
                    │    └────┬────┘          │
                    │         │               │
                    │  ┌──────┴───────┐       │
                    │  │              │       │
                    │  ▼              ▼       │
                    │ Scheduler    Worker池   │
                    │ (LLM 网关)  (bash/file  │
                    │ 优先级队列   http/...)  │
                    │  │              │       │
                    └──┼──────────────┼───────┘
                       │              │
                       ▼              ▼
                    ┌─────────────────────┐
                    │       Redis         │
                    └─────────────────────┘
```

## License

MIT
