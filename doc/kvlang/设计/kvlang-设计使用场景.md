# kvlang 设计使用场景

> GitHub 定位：**Agent-Native 训推一体自迭代强人工智能计算架构**
> — KV 路径寻址 VM、Redis 驱动、控制流 / 张量计算 / 多后端 GPU 调度、Triton 集成

---

## 一、kvlang 是什么，不是什么

**是**：一个以 KV store 为执行内存的声明式 VM，程序状态全部透明存在路径树上，天然支持分布式、持久化、可观测。

**不是**：通用编程语言（不替代 Python/Go/Rust），不是传统深度学习框架（不替代 PyTorch/JAX），不是工作流引擎（不替代 Airflow/Temporal）。

kvlang 的定位是这些工具**之下的执行底座**——统一 AI 计算的控制平面。

---

## 二、核心使用场景

### 场景一：AI 推理服务（Inference）

**问题**：大模型推理需要多 GPU 调度、KV cache 管理、请求并发，传统框架（vLLM/TGI）把这些逻辑写死在 Python 进程里，难以热更新、难以分布式监控。

**kvlang 的做法**：
```kv
def forward(input_ids:tensor, kv_cache:tensor) -> (logits:tensor) {
    embedding(input_ids) -> hidden
    transformer_block(hidden, kv_cache) -> hidden
    lm_head(hidden) -> logits
}
```

- 每一步的中间张量 handle 写在 `/vthread/<vtid>/hidden`，随时可从外部读取
- `kv_cache` 存在 `/sys/heap/cuda/0` 下，多个请求（vthread）共享
- 推理进度（当前层）可从 KV 路径直接读出，无需侵入代码

**对比**：vLLM 的请求状态在 Python 进程内存里，崩溃即丢失；kvlang 的状态在 Redis，重启 VM 可从上次 PC 继续。

---

### 场景二：分布式训练控制流（Training）

**问题**：分布式训练的控制逻辑（数据并行、流水线并行、梯度同步）散落在 PyTorch/DeepSpeed 的 Python 层，调试困难，拓扑变更需要重写脚本。

**kvlang 的做法**：
```kv
def train_step(batch:tensor) -> (loss:tensor) {
    forward(batch) -> logits
    cross_entropy(logits, batch) -> loss
    backward(loss) -> grads
    optimizer_step(grads) -> ()
}
```

- `forward`/`backward` 实际调用 `op-cuda`/`op-triton` 后端，kvlang 只写控制流
- 多 GPU 的 AllReduce 是一个 `op` 调用，后端决定 NCCL/RDMA 实现
- 训练循环的当前 step、当前 loss 随时在 KV 路径上可查

**对比**：PyTorch DDP 把 AllReduce 隐藏在钩子里；kvlang 让每一步 IO 都显式出现在路径上，topo 变更只需换后端，不改控制代码。

---

### 场景三：强化学习循环（Reinforcement Learning）

**问题**：RL 的 actor-learner 架构需要多进程协调（actor 采样、learner 更新、replay buffer 管理），现有方案（RLlib、IMPALA）耦合严重，难以热替换组件。

**kvlang 的做法**：
```kv
def rl_loop(env_state:tensor) -> (reward:tensor) {
    actor(env_state) -> action
    env_step(action) -> (next_state, reward)
    replay_buffer_push(env_state, action, reward) -> ()
    learner_update() -> ()
}
```

- `actor`、`learner`、`replay_buffer` 各自是独立的 `op-plat` 后端
- 多个 actor vthread 并行，learner vthread 独立，通过 `/sys/heap/` 共享 replay buffer
- 任意 vthread 的执行进度、奖励曲线都可从 KV 路径实时读出

**对比**：RLlib 的 actor/learner 是 Ray actor，状态不透明；kvlang 的所有状态在 KV 路径上，可被任意 agent 读写。

---

### 场景四：Agent 任务流编排（Agentic Workflow）

**问题**：LLM agent 的工具调用链（plan → tool_call → observe → reflect → next_action）需要持久化（中途崩溃可恢复）、并发（多 agent 协作）、可监控（人类随时介入）。

**kvlang 的做法**：
```kv
def agent_step(goal:string, history:string) -> (action:string) {
    llm_plan(goal, history) -> plan
    tool_dispatch(plan) -> tool_result
    llm_reflect(plan, tool_result) -> action
}
```

- 每个 agent 是一个 vthread，路径 `/vthread/<vtid>/plan`、`/vthread/<vtid>/tool_result` 随时可查
- 人类可以直接 `kv set /vthread/<vtid>/action "override"` 强制干预
- 崩溃后 VM 重启，从 KV 中读取上次的 `plan` 和 `tool_result`，继续执行

**对比**：LangChain/AutoGen 的 agent 状态在 Python 对象里，人类干预需要改代码；kvlang 的状态在 KV，干预就是 `kvlang kvspace set`。

---

### 场景五：AI 自迭代（Self-Improving AI）

这是 kvlang 的终极场景，也是 "agent-native" 的核心含义。

**目标**：让 AI 系统能够读取自身的执行状态（`/vthread/`）、代码（`/func/`）、历史（`/src/`），生成改进代码，写回 KV，下一轮执行改进后的版本。

```
AI 读取 /func/main/train_step   → 分析当前训练代码
AI 生成改进版本                 → 写入 /func/main/train_step_v2
AI 写入 /func/main              → {"entry":"train_step_v2"}
下一轮 VM 调度执行新版本
```

**关键**：代码（`/func/`）和状态（`/vthread/`）在同一路径系统，AI 用同一套接口读代码、读状态、写代码——无需特殊 API，无需 reflection，路径即接口。

---

## 三、使用场景矩阵

| 场景 | 核心价值 | 替代方案 | kvlang 的额外优势 |
|------|---------|---------|-----------------|
| AI 推理 | 可恢复、可监控 | vLLM、TGI | KV 透明状态，崩溃不丢请求 |
| 分布式训练 | 控制流与计算解耦 | DeepSpeed、Megatron | topo 变更只换 op 后端 |
| 强化学习 | 组件热替换 | RLlib、IMPALA | actor/learner 完全解耦 |
| Agent 编排 | 持久化 + 人类干预 | LangChain、AutoGen | KV 状态天然可干预 |
| AI 自迭代 | 代码即数据 | 无直接对应物 | `/func/` 可被 AI 读写 |

---

## 四、不适合的场景

| 场景 | 原因 |
|------|------|
| 高频交易 / 游戏引擎 | KV 路径寻址有延迟（µs 级），不适合 ns 级热循环 |
| 系统编程（驱动、OS） | 无裸内存访问，无中断处理 |
| 独立 CLI 工具 | 依赖 Redis 作为执行基础设施，轻量场景杀鸡用牛刀 |
| 前端 / Web 渲染 | 无 DOM 绑定，无事件循环抽象 |

---

## 五、目标用户

### 5.1 训推一体 AI 计算平台

将训练与推理统一在同一套执行底座上。传统方案训练用 PyTorch，推理用 TensorRT/vLLM，两套系统、两套状态管理、两套监控——kvlang 以统一的 KV 路径执行模型同时承载两者，切换只是换 `/func/` 下的函数实现，状态表示不变。

```
训练时：/vthread/<vtid>/grads   存梯度句柄
推理时：/vthread/<vtid>/logits  存推理输出
监控方：/vthread/<vtid>/        读任意中间状态
```

平台工程师的职责从"维护两套系统"变为"维护一套 KV 路径树"。

---

### 5.2 自主迭代进化的机器人脑核心

机器人需要在运行中持续学习、更新策略、进化神经网络结构——这要求执行引擎本身能被 AI 读写。kvlang 的代码（`/func/`）和状态（`/vthread/`）在同一路径系统，机器人脑可以：

- **读**当前策略：`kv get /func/robot/policy`
- **分析**执行历史：遍历 `/vthread/` 下的历史 vthread
- **写**进化版本：`kv set /func/robot/policy_v2 "def policy_v2(...) {...}"`
- **切换**入口：`kv set /func/main '{"entry":"policy_v2"}'`
- **下一帧**自动执行新策略，无需重启进程

进化不需要特殊 API，**路径即接口**，代码与数据同构。

---

### 5.3 Agent

kvlang 本身可以作为 agent 的执行内核。agent 的感知、规划、行动、反思每一步都是一个 vthread，每个 vthread 的状态透明可查，多 agent 协作通过共享 KV 路径实现，人类随时可以通过 `kvlang kvspace set` 干预任意 agent 的状态或决策。

```
/vthread/<agent-id>/plan          当前规划
/vthread/<agent-id>/tool_result   工具调用结果
/vthread/<agent-id>/reflect       反思内容
/vthread/<agent-id>/done          完成信号
```

多 agent 不需要消息总线——共享 KV 路径树即是总线。
