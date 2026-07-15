# Zerolang 分析 — 对 kvlang 的启示

> 来源: https://github.com/vercel-labs/zerolang (5,201 stars, 2026-05, C)
> 定位: "The Programming Language for Agents"

---

## 1. 核心创新: Graph-First 架构

传统 source of truth 是文本。Agent 写文本, 编译, 报错, 循环。
Zerolang 反转: 语义图是程序数据库。

graph patch 精确 target 语义节点, 而非猜行号。

### 对 kvlang 的启示

kvlang 的 KV-path 天然就是 graph。不需要额外发明 graph 格式。

| | Zerolang | kvlang |
|--|----------|--------|
| source of truth | zero.graph (binary) | KV tree in Redis |
| agent 编辑 | zero patch --op | kvspace set /path value |
| 人类 review | .0 projection file | KV path 即语义 |
| 校验时机 | patch 时 (shape rules) | 执行时 (runtime) |

Agent 可直接 SET /src/func/body/block_3, 受 kvspace atomic 保护。

---

## 2. Agent-First 设计哲学

从 AGENTS.md 提取:
- 优先 agent-facing 设计, 不为人类做语法糖
- 抛弃兼容性包袱
- Token-efficient inspection (zero query)
- Checked patch: 一次操作 = 编辑 + 过期保护 + shape 校验

### 对 kvlang

kvlang 天然支持 inspection (GET /vthread/1/pc), 但缺:
1. 结构化 query: kvlang inspect --json PATH
2. Patch 协议: kvlang patch --expect-hash H --op SET
3. 诊断 JSON: 错误含 path + expected type + fix suggestion

---

## 3. 测试与质量保障

| 维度 | Zerolang | kvlang |
|------|----------|--------|
| 编译器 | 129 .c + 86 .h | Go (~40 源文件) |
| Conformance | 分片 (shard 1/4) | 175 用例串行 |
| Benchmark | 100+ Rosetta Code | 179 examples |
| Agent 自测 | agent:checks | 无 |
| fail-fast | 支持 | 无 |

### 可借鉴
- 选 20 个算法做 benchmark suite
- run.py --shard 1/4 分片并行
- AGENTS.md 告诉 AI 如何自测
- run.py --fail-fast

---

## 4. 可立即执行

| 借鉴 | kvlang 实现 |
|------|------------|
| AGENTS.md | 创建, 说明方向, 测试, CI |
| vet --json | 输出 JSON 错误 |
| run.py --fail-fast | 首个失败即停 |
| CHANGELOG release 标记 | 加 release:start 标记 |

---

## 5. kvlang 核心优势 vs Zerolang

| 维度 | Zerolang | kvlang 优势 |
|------|----------|------------|
| Agent API | 自定义协议 | 标准 KV (get/set) |
| 分布式 | 单机 | Redis 多进程 |
| 并发 | 编译时单线程 | 128 worker 运行时 |
| 人类可读 | .0 需学习 | KV path 即语义 |
| 源码规模 | 215 文件 | ~40 文件 |
