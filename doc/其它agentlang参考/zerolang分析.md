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

---

## 6. 为什么 Zerolang 能 5000 stars？(kvlang 0 star)

### 硬数据

| 指标 | 数据 |
|------|------|
| 创建 | 2026-05-15（仅 60 天） |
| 贡献者 | **1 人**: Chris Tate (@ctate), 1186 commits |
| 组织 | **vercel-labs**（Vercel 官方） |
| 作者 | Chris Tate, Vercel 员工, 2199 followers, 154 repos |
| Issue | 47 open, 外部 contributor 深度参与 |
| 媒体 | Fireship newsletter |

### 5000 stars 的真相

**不是技术，是信任代理：**

```
Vercel 品牌 (VC-backed, 知名)
  × 作者 2199 followers (个人影响力)
  × "Programming Language for Agents" (2026 最热叙事)
  × Fireship newsletter (开发者媒体)
  × "graph-first" 概念新颖性
  ────────────────────────────
  = 5000 stars in 60 天
```

zerolang 只是 1 人 2 个月的 C 代码。社区 issue 里 `PeterXMR` 做了 18 项 bug audit，12 项 still reproduces on main。代码质量远不如 kvlang（128 worker、TCO、类型化 Value、Redis 分布式）。但 Vercel 品牌给了它**信任杠杆**。

### kvlang 0 star 的原因

| 差距 | Zerolang | kvlang |
|------|----------|--------|
| 组织 | vercel-labs (VC 背书) | array2d (个人) |
| 作者影响力 | 2199 followers | 0 |
| 叙事 | "agent-first" 踩中风口 | "declarative VM" 不够性感 |
| README | 英文, 叙事驱动 | 刚英文化 |
| 媒体 | Fireship | 无 |
| 社交证明 | 47 issue 活跃讨论 | 0 |

---

## 7. 破局策略

### 叙事重构

当前: "基于 KV 路径寻址的声明式 VM" ← 工程师视角

建议: **"The VM where code IS data — agents inspect and edit programs through the same KV tree"**

kvlang 的 KV-path 天然 agent-friendly（GET 任何变量、SET 任何指令），这是 zerolang 费大力气发明 zero.graph + zero.patch 才做到的事——kvlang 天生就有，但**没人知道**。

### 制造社交证明

1. 在 zerolang issue 区发有深度评论（graph-first vs KV-path），自然引入 kvlang
2. 写对比文章: "Zerolang vs kvlang: Two approaches to agent-native programming"
3. 投 Hacker News / Reddit / Twitter — 借 zerolang 热度

### 品牌建设

- [ ] 创建 org repo（从个人迁移）
- [ ] 作者 Twitter/blog 积累 follower
- [ ] 找独特 niche: "唯一支持 agent 通过标准 KV 协议 inspect 任意运行中变量的 VM"

---

## 5. kvlang 核心优势 vs Zerolang

| 维度 | Zerolang | kvlang 优势 |
|------|----------|------------|
| Agent API | 自定义协议 | 标准 KV (get/set) |
| 分布式 | 单机 | Redis 多进程 |
| 并发 | 编译时单线程 | 128 worker 运行时 |
| 人类可读 | .0 需学习 | KV path 即语义 |
| 源码规模 | 215 文件 | ~40 文件 |

---

## 8. Issue 讨论深度分析 — kvlang 是否有同类问题？

### 8.1 #68: 类型系统用 `strcmp` 实现

一个**CS 本科生**读了 zerolang 的 28,000 行 C 代码后发 issue，指出核心问题：

> "You brilliantly simplified everything by defining types as `char *`! Generic instantiation is just finding `< >` brackets and doing string concatenations."

具体 bug: `shape T { value: i32 }` 和 `fun confuse<T>` 同时存在时，`T` 既是具体 shape 名又是泛型参数名。`zero check` 通过但 `zero build` 失败 — checker 和 codegen 对名字解析不一致。

另一评论者 @chuigda 直接比作 **李森科主义**（Lysenkoism）：用意识形态（"AI 能搞定一切"）代替客观规律（PLT 几十年积累的类型理论）。

**kvlang 是否有同类问题？** 有，但程度轻得多：

kvlang 的类型系统是 `Value{kind string, raw []byte}` — kind 也是字符串。不同之处在于 kvlang 没有泛型、没有类型推断、没有 subtyping。类型冲突的场景少得多。但存在潜在风险：

- `Value{kind: "int", raw: ...}` — 如果 kind 字符串拼错，不会编译报错，只会运行时返回零值
- 类型检查全在访问器层（`v.Int()` 里检查 `v.kind != "int"`），分散而非集中

**建议**: 将 kind 常量化（已有 `KindInt = "int"` 常量），增加 `Value.Validate()` 在构造时校验 kind 是否已注册。

### 8.2 #348: 多 agent 协作需要稳定节点 ID

zerolang 的核心 issue: graph 节点 ID 原来基于内容 hash。改一行代码，所有下游节点 ID 全变。多 agent 无法协作。

解决方案：**结构派生 ID** — 基于 "模块 + owner-edge 链 + edge kind + 兄弟顺序" 生成，与内容解耦。

**kvlang 是否有同类问题？** **没有。这是 kvlang 的天然优势。**

kvlang 的 path 天生稳定：
- `/vthread/1/[5,-1]` — 改 `[5,0]` 的值不影响 `[5,-1]` 的路径
- `/src/func/add/[3,0]` — 改函数 body 不影响其他指令的 path

zerolang 花了 RFC + 多轮 PR (#355) 才解决的事情，kvlang 不需要任何额外设计。

### 8.3 #290: 语法不稳定

0.1.4 版本一夜之间大幅改语法，用户抱怨。有人直接说 "agent-first" 和 "agent-only" 不是一回事。

**kvlang 是否有同类问题？** 目前没有，因为还没对外发布。但这是个警告：语法一旦公开，就不能大改。

### 8.4 #181: 代码质量差

"C 写的，不知道指针是 sink/inout/let/set。大量基于字符串的相等检查。为什么 IR 要处理字符串？"

**kvlang 是否有同类问题？** 相比之下 kvlang 代码质量好得多：

| | Zerolang | kvlang |
|--|----------|--------|
| 语言 | C (原始指针) | Go (GC + 类型安全) |
| 类型检查 | `strcmp` 字符串比较 | 结构化 `Value.kind` + `vtype.VType` 注册表 |
| IR | 字符串 IR | 无 IR 层（直接 KV path 就是 IR） |
| 代码量 | 28,000 行 C / 1 人 | ~40 文件 Go |
| 测试 | conformance 分片 | 175 集成测试 + 4 单元测试文件 |

**但这反而是 kvlang 的宣传点** — "Zerolang: 28K lines of C with string-based type checking. kvlang: 40 Go files with 2 dependencies."

### 8.5 #327: MCP 集成讨论

社区讨论如何让 LLM 通过 MCP (Model Context Protocol) 调用 Zero。核心需求：token-efficient encoding、typed tool calls、transactional patch。

**kvlang 是否有同类问题？** kvlang 的 KV 接口天然就是 MCP-ready：

- `GET /vthread/1/[5,0]` → 返回 opcode（MCP resource）
- `SET /vthread/1/[5,0] "add"` → 修改指令（MCP tool）
- kvspace 的原子性 = transactional patch

zerolang 需要讨论怎么包装 MCP server，kvlang 只需要 expose kvspace over HTTP。

### #178: 名字让人困惑

"Zero" 这个名字太抽象。有人建议改成 Copilot。

**对 kvlang 的启示**: "kvlang" 虽然不够性感，但至少准确（KV language）。问题不是名字，是没人知道。
