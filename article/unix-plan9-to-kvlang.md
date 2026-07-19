# 从 Unix / Plan 9 到 kvlang

> 本文回答一个问题：**"用 KV 路径层次编码调用栈的执行机制，前人尝试过吗？"**

---

## 一、kvlang 执行模型的三个核心特征

在做比较之前，先明确被比较的对象：

1. **KV store 即执行内存** — 程序的全部运行状态存在 Redis 路径下，不在 CPU 寄存器或进程内存里。
2. **路径深度 = 调用栈深度** — `/vthread/1/pre-main/calc/add` 的路径层数就是当前调用栈的深度，无需额外的栈数据结构。
3. **代码与状态共用同一命名空间** — `/func/` 存静态代码，`/vthread/` 存动态状态，同一路径系统，同一寻址方式。

---

## 二、六条先驱路线

### 2.1 SECD 机器（Landin，1964）— 最早的函数式 VM

SECD = Stack, Environment, Control, Dump。四个寄存器分别指向操作数栈、环境（变量绑定）、控制序列（指令）、转储（调用栈保存）。这是历史上**第一台专为 lambda 演算求值设计的虚拟机**。

| | SECD | kvlang |
|---|---|---|
| 调用栈 | Dump 寄存器（链表，内存中） | **路径本身**（`/vthread/1/a/b/c`） |
| 环境 | E 寄存器（关联数组） | KV 槽位（`/vthread/<vtid>/x`） |
| 代码 | C 寄存器（指令列表） | `/func/<pkg>/<name>/[i,j]` |
| 持久化 | ❌ 纯内存，进程死即失 | ✅ Redis 持久，崩溃可继续 |

SECD 是最直接的祖先——结构化地对待执行状态。但它的调用栈是独立的 Dump 链表，不是路径本身。

---

### 2.2 Plan 9 / 9P 协议（Bell Labs，1987）— "万物皆路径"走到极致

Plan 9 将 Unix "万物皆文件" 延伸为 **9P 网络文件协议**：进程、网络连接、窗口、设备，全部表示为路径下的文件。

```
/proc/<pid>/ctl     ← 控制进程
/proc/<pid>/mem     ← 读写进程内存
/proc/<pid>/fd/     ← 进程的文件描述符
```

这与 kvlang 的路径设计直接对应：

```
/vthread/<vtid>/done   ← 完成通知
/vthread/<vtid>/[i,j]  ← 指令槽
/sys/vm/<id>/hb        ← 心跳
```

Plan 9 是路径哲学的最直接来源。**差异**：Plan 9 用路径命名进程资源，但进程本身仍在 CPU 上执行，执行语义不走文件系统。kvlang 把**执行本身**也放进了路径系统。

---

### 2.3 Linda / Tuplespace（David Gelernter，1985）— KV 作为协调介质

Linda 是并行计算的协调模型，提供三个原语：`out(tuple)`（写入），`in(template)`（取出），`rd(template)`（读取）。共享的 tuple space 是唯一的通信介质。JavaSpaces（1999）是 Java 实现，GigaSpaces 是商业版。

**相似**：共享 KV 存储作为计算介质。  
**差异**：Linda 是**扁平的 tuple 袋**，无路径层次，无调用栈语义——它是协调机制，不是执行引擎。

---

### 2.4 Blackboard 系统（1980s AI）— 共享知识库作为计算基底

Blackboard 架构（HEARSAY-II，CMU，1970s）：多个"知识源"（specialist）轮流读写共享的"黑板"，各自贡献局部解，迭代收敛到完整解。

**相似**：共享存储作为多智能体计算的基底，与 kvlang 的多 worker 并行调度 vthread 有相似之处。  
**差异**：Blackboard 没有调用栈语义，没有路径层次，是 AI 问题求解框架，不是通用语言 VM。

---

### 2.5 Erlang OTP（Ericsson，1986）— 最接近 vthread 概念

Erlang 的轻量进程 + mailbox 模型：每个进程有独立内存，通过消息通信，OTP 监督树管理生命周期。

**相似**：轻量并发执行单元（Erlang 进程 ≈ kvlang vthread），故障隔离，分布式。  
**差异**：Erlang 进程**不共享地址空间**，无法通过路径直接读写另一个进程的状态。kvlang 的 `/vthread/<vtid>/done` 可被任何有权限的 worker 直接读写。

---

### 2.6 Temporal.io / DBOS（2020s）— 最近的同类，但方向不同

**Temporal**（前 Uber Cadence，2019）：Workflow Execution = 持久化的事件历史（Event History）。程序崩溃后通过**重放历史**恢复到最新状态。

**DBOS**（Stanford/MIT，2022）：把 Postgres 作为操作系统底座，进程状态、调度信息、执行上下文全部存在 DB 表中。"Lightweight Durable Execution Built on Postgres。"

| | Temporal/DBOS | kvlang |
|---|---|---|
| 状态持久化 | ✅ DB / 事件日志 | ✅ Redis KV |
| 崩溃恢复 | ✅ 重放事件历史 | ✅ 直接从当前 KV 状态继续 |
| 调用栈表示 | 事件日志（线性序列） | **路径层次**（深度 = 栈深度） |
| 并发 | Workflow 单线程 + Activity | vthread 多线程并行 |
| 目标 | 工作流编排 | 通用语言 VM |

这是工程上**最接近的现代实现**。关键差异：Temporal/DBOS 通过**重放**恢复状态（状态是日志的函数），kvlang **直接存储当前状态**（状态即 KV，无需重放）。

---

## 三、比较矩阵

| 特征 | Linda | Plan 9 | SECD | Erlang | Temporal/DBOS | **kvlang** |
|------|:-----:|:------:|:----:|:------:|:-------------:|:----------:|
| KV/路径作为执行介质 | ✅ | ✅ | ❌ | ❌ | ✅ | ✅ |
| 路径层次 = 调用栈深度 | ❌ | ❌ | ❌ | ❌ | ❌ | **✅** |
| 代码与状态同一命名空间 | ❌ | ✅ | ❌ | ❌ | ❌ | **✅** |
| 不依赖事件重放 | ✅ | ✅ | ✅ | ✅ | ❌ | **✅** |
| 通用语言 VM | ❌ | ❌ | ✅ | ✅ | ❌ | **✅** |
| 崩溃后可恢复 | ❌ | ❌ | ❌ | ✅ | ✅ | **✅** |

---

## 四、先驱谱系：谁最元老？

六个先驱按两个维度评估：

**按发布年份**：

| 系统 | 年份 | 创造者 |
|------|------|--------|
| SECD 机器 | **1964** | Peter Landin |
| Linda/Tuplespace | 1985 | David Gelernter（Yale） |
| Erlang | 1986 | Joe Armstrong（Ericsson） |
| Plan 9 | 1987 开发，1992 发布 | Bell Labs |
| Blackboard | 1970s～80s | CMU（HEARSAY-II） |
| Temporal/DBOS | 2019/2022 | 前 Uber 团队 / Stanford·MIT |

论年份，**SECD（1964）最古老**。

**按血统/传承**：

Plan 9 的创造者是 **Ken Thompson + Dennis Ritchie + Rob Pike**——Unix（1969）和 C（1972）的原班人马。Plan 9 是他们对 Unix 设计哲学的再次升华，直接继承了 1969 年以来的思想谱系。

Erlang 是 Ericsson 为电信交换机独立发展的，与 Unix 谱系无直接关联。

论血统，**Plan 9 最元老**——它出自计算机科学奠基人之手，是"万物皆文件"哲学的最终形态。而 kvlang 的路径系统正是这一哲学在 KV 执行引擎上的延伸：

```
Unix 1969（万物皆文件）
    ↓  Ken Thompson · Dennis Ritchie · Rob Pike
Plan 9 1987（万物皆路径，9P 网络协议）
    ↓  路径哲学 × KV 存储 × 结构化执行状态
kvlang 2024（路径深度 = 调用栈深度）
```

---

## 五、结论

kvlang 的执行模型是多条先驱路线的**交汇**，而非凭空发明：

```
Plan 9（路径即一切，Unix 正统血脉）
    +
SECD 机器（结构化执行状态，1964）
    +
Temporal/DBOS（持久化执行，KV/DB 作为底座）
    ↓
kvlang：路径深度 = 调用栈深度（新的交汇点）
```

**用路径的层次结构直接编码调用栈深度**——这一具体设计在已知文献和工程实现中没有发现先例。它使得：

- 调用栈在任意时刻都可从 KV 路径直接读出，无需反序列化
- 崩溃恢复不需要重放，直接从上次 KV 状态继续
- 监控、调试、热迁移天然免费——因为状态本来就是 KV

---

*搜索范围：Wikipedia、arXiv、Hacker News（2026-07-13）*
