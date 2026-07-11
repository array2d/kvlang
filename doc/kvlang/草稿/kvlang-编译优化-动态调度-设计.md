# kvlang 编译优化层：动态调度与张量并行

## 1. 用户的判断是否合理？

**完全合理。** Triton 和 kvlang 工作在两个正交的优化层次上：

```
┌──────────────────────────────────────────────────────────────┐
│  kvlang 编译优化层              │  Triton 内核层               │
│                                │                              │
│  • 图分区 (graph partition)    │  • 单 kernel 代码生成         │
│  • 张量分片 (tensor shard)     │  • 共享内存优化               │
│  • 设备放置 (device placement) │  • 内存合并 (coalescing)      │
│  • 动态调度 (dynamic schedule) │  • 自动调优 (autotune)        │
│  • KV 空间路由                 │  • 寄存器分配                 │
│  • 跨节点通信编排              │  • warp 级并行                │
│                                │                              │
│  操作对象: 计算图 + 分布式 KV   │  操作对象: 单 GPU kernel      │
│  不重复 ✅                      │  不重复 ✅                    │
└──────────────────────────────────────────────────────────────┘
```

Triton 无法处理的问题，正是 kvlang 编译优化的核心价值：

| Triton 的边界 | kvlang 的职责 |
|--------------|--------------|
| 单 GPU，固定 shape kernel | 多 GPU，动态 shape → 重分片 |
| 编译时确定 launch config | 运行时根据实际 shape 调度 |
| 无分布式概念 | 张量并行 + 流水线并行编排 |
| 无 KV 空间感知 | 数据在 KV 空间的路由优化 |

## 2. 核心场景：动态 Shape 下的张量并行

### 2.1 问题描述

```kvlang
# 用户代码：看起来是简单的 elementwise
def transformer_layer(X: tensor, W: tensor) -> (O: tensor) {
    matmul('./X', './W') -> './T'
    relu('./T') -> './O'
}
```

实际执行时，`X` 的 shape 在编译期未知 — 可能是 `[batch=8, seq=512]` 也可能是 `[batch=64, seq=2048]`。这决定了：

- **是否分片**：小 batch 单卡跑，大 batch 切到 4 卡
- **如何分片**：按 batch 维度切（数据并行）还是按 hidden 维度切（张量并行）
- **通信开销**：分片后 all-reduce / all-gather 的开销是否值得

### 2.2 kvlang 编译器的决策流程

```
┌─────────────────────────────────────────────────────────────┐
│  kvlang Compiler Pipeline                                    │
│                                                              │
│  ① Parse AST                                                 │
│       │                                                      │
│       ▼                                                      │
│  ② Graph IR 构建                                             │
│     ┌──────────────────────────────────┐                    │
│     │  Node: matmul(X, W) → T          │                    │
│     │  Node: relu(T) → O               │                    │
│     │  Edge: T depends on X, W         │                    │
│     └──────────────────────────────────┘                    │
│       │                                                      │
│       ▼                                                      │
│  ③ Shape Inference + Annotation                              │
│     X: [?×?]  →  (需要运行时确定)                             │
│       │                                                      │
│       ▼                                                      │
│  ④ Device Topology                                           │
│     ┌─────────────────────────────────┐                     │
│     │ GPU 0: NVIDIA H100, 80GB, NVLink│                     │
│     │ GPU 1: NVIDIA H100, 80GB, NVLink│                     │
│     │ GPU 2: NVIDIA H100, 80GB, NVLink│                     │
│     │ GPU 3: NVIDIA H100, 80GB, NVLink│                     │
│     └─────────────────────────────────┘                     │
│       │                                                      │
│       ▼                                                      │
│  ⑤ 分片策略决策 (编译时生成分支)                               │
│                                                              │
│  if X.shape[1] > 2048:           ← 动态条件                   │
│      shard X 沿 dim=1 切 4 份    ← 张量并行                   │
│      shard W 沿 dim=0 切 4 份                                 │
│      → 4 路 matmul + all-reduce                               │
│  else:                                                       │
│      单 GPU 执行                 ← 避免通信开销                │
│      → Triton fused kernel                                    │
│       │                                                      │
│       ▼                                                      │
│  ⑥ 生成分片后的 IR                                            │
│                                                              │
│  // 单 GPU 路径                                               │
│  @triton("cuda:0")                                           │
│  matmul_fused_relu(X, W) → O                                 │
│                                                              │
│  // 4 GPU 路径                                                │
│  @shard(X, dim=1, parts=4)                                   │
│  @shard(W, dim=0, parts=4)                                   │
│  @parallel(4) {                                              │
│      @triton("cuda:{rank}")                                  │
│      matmul_fused_relu(X[shard], W[shard]) → O[shard]       │
│  }                                                           │
│  @all_reduce(O, op=sum)                                      │
│       │                                                      │
│       ▼                                                      │
│  ⑦ 运行时调度                                                 │
│     shape 确定 → 选择分支 → 分发到 op-plat                     │
└─────────────────────────────────────────────────────────────┘
```

## 3. kvlang IR 设计：分片与调度原语

### 3.1 Graph IR 新增节点

```go
// internal/ir/graph.go (新包)

type GraphNode interface {
    OpType() string
}

// ── 计算节点 ──
type ComputeOp struct {
    Opcode string              // "matmul", "add", "relu" ...
    Inputs []TensorRef
    Output TensorRef
    Backend string             // "triton-cuda:0"
}

// ── 分片节点 ──
type ShardOp struct {
    Input  TensorRef
    Outputs []TensorRef        // 分片后的子 tensor
    Dim    int                 // 切分维度
    Parts  int                 // 份数
    Strategy string            // "even" | "dynamic"
}

// ── 集合通信节点 ──
type CollectOp struct {
    Inputs []TensorRef         // 各分片的子 tensor
    Output TensorRef           // 合并后的完整 tensor
    Op     string              // "all_reduce" | "all_gather" | "reduce_scatter"
}

// ── 条件分支节点 (动态调度关键) ──
type BranchOp struct {
    Condition ShapePredicate   // 运行时 shape 条件
    ThenGraph *SubGraph        // 满足条件时的子图
    ElseGraph *SubGraph        // 不满足时的子图
}

type ShapePredicate struct {
    Tensor string             // 依赖的 tensor 名
    Dim    int                 // 检查的维度
    Op     string              // ">" | "<" | "=="
    Value  int                 // 阈值
}
```

### 3.2 编译 Pass 流水线

```
Source (.kv)
    │
    ▼
┌──────────────┐
│ Parse        │ → AST
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ Lower        │ → Linear IR (现有的指令序列)
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ BuildGraph   │ → Graph IR (DAG)
│              │    新增: 分析 def-use chain
│              │    识别可融合的 op 序列
│              │    识别可并行的独立分支
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ ShapeInfer   │ → 标注 shape constraint
│              │    static: 编译期已知
│              │    dynamic: 依赖上游运行时值
│              │    symbolic: dim0 = batch, dim1 = seq_len
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ Partition    │ → 分片决策
│              │    分析每个 op 的计算量 (FLOPS)
│              │    分析通信开销 (bytes to transfer)
│              │    决策: shard / replicate / single
│              │    插入 BranchOp 处理动态 shape
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ Schedule     │ → 生成执行计划
│              │    每个子图 → op-plat 队列
│              │    依赖关系 → wait/notify 编排
│              │    通信节点 → NCCL/RCCL 调用
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ Codegen      │ → 最终 IR
│              │    线性 op 序列 + 调度指令
│              │    写入 /src/func/<name>/
└──────────────┘
```

## 4. 动态调度运行时

### 4.1 调度指令

kvlang 新增调度级指令（与现有 `OpCall/OpReturn` 同级）：

```go
// internal/op/control.go 扩展
const (
    OpShard     = "shard"      // 分片 tensor
    OpGather    = "gather"     // 收集分片结果
    OpBarrier   = "barrier"    // 等待所有分片完成
    OpBranch    = "branch"     // 动态分支选择
    OpPrefetch  = "prefetch"   // 预取下一批数据
)
```

### 4.2 动态分支执行

```kvlang
# 编译器生成的调度代码 (lowering 后)
def transformer_layer__scheduled() -> ('/data/O') {
    entry: {
        # 读取 X 的实际 shape
        shape('/data/X') -> ('./batch', './hidden')
        # 动态分支
        './hidden' > 2048 -> './large'
        br('./large', shard4, single_gpu)
    }

    single_gpu: {
        # 单 GPU: 融合 kernel，直接丢给 Triton
        @triton("cuda:0")
        matmul_fused_relu('/data/X', '/data/W') -> '/data/O'
        return
    }

    shard4: {
        # 4 GPU 张量并行
        shard('/data/X', dim=1, parts=4) -> ('./X0','./X1','./X2','./X3')
        shard('/data/W', dim=0, parts=4) -> ('./W0','./W1','./W2','./W3')
        
        # 并行 dispatch 到 4 个 GPU
        parallel {
            @triton("cuda:0") matmul_fused_relu('./X0','./W0') -> './O0'
            @triton("cuda:1") matmul_fused_relu('./X1','./W1') -> './O1'
            @triton("cuda:2") matmul_fused_relu('./X2','./W2') -> './O2'
            @triton("cuda:3") matmul_fused_relu('./X3','./W3') -> './O3'
        }
        
        barrier
        all_reduce('./O0','./O1','./O2','./O3') -> '/data/O'
        return
    }
}
```

### 4.3 调度时序

```
Time ──────────────────────────────────────────────────────────►

GPU0: [shape detect] ── [br check] ── [shard X] ── [matmul X0*W0] ── [all_reduce]
GPU1:                                  [shard W] ── [matmul X1*W1] ── [all_reduce]
GPU2:                                               [matmul X2*W2] ── [all_reduce]
GPU3:                                               [matmul X3*W3] ── [all_reduce]
                         │                                        │
                         └── 动态决策点 ──────────────────────────┘
                         根据实际 hidden dim 选择分支
```

## 5. kvlang vs Triton 分工边界

```
┌─────────────────────────────────────────────────────────────────┐
│                        kvlang 职责                               │
│                                                                 │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌────────────────┐  │
│  │ 图分析    │  │ 分片策略  │  │ 动态调度  │  │ KV 空间优化    │  │
│  │          │  │          │  │          │  │                │  │
│  │ FLOPS估算│  │ 张量并行  │  │ shape→分支│  │ 数据就近放置   │  │
│  │ 依赖分析 │  │ 流水线并行│  │ 负载均衡  │  │ 跨节点路由     │  │
│  │ 融合检测 │  │ 数据并行  │  │ 弹性扩缩  │  │ 缓存策略       │  │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └───────┬────────┘  │
│       │              │             │                 │           │
│       │     ┌────────┴─────────────┴─────────────────┘           │
│       │     │                                                    │
│       ▼     ▼                                                    │
│  ┌──────────────────────┐                                       │
│  │  sub-op 序列 + 调度指令│  ← kvlang IR 输出                     │
│  │  (每个 sub-op 落到    │                                       │
│  │   某个 GPU 的某个     │                                       │
│  │   Triton kernel)      │                                       │
│  └──────────┬───────────┘                                       │
└─────────────┼────────────────────────────────────────────────────┘
              │
              │  Redis Queue: cmd:op-triton-cuda:{gpu_id}
              │
┌─────────────▼────────────────────────────────────────────────────┐
│                        Triton 职责                                │
│                                                                  │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐   │
│  │ Kernel 生成   │  │ 内存优化     │  │ 自动调优              │   │
│  │              │  │              │  │                      │   │
│  │ 单 GPU PTX   │  │ shared memory│  │ BLOCK_SIZE 搜索      │   │
│  │ 向量化指令   │  │ coalescing   │  │ num_warps 搜索       │   │
│  │ 循环展开     │  │ 寄存器分配   │  │ 缓存最佳配置          │   │
│  └──────────────┘  └──────────────┘  └──────────────────────┘   │
│                                                                  │
│  ⚠️ 不关心: 分片策略、动态 shape 分支、跨 GPU 通信、KV 路由       │
└──────────────────────────────────────────────────────────────────┘
```

**关键原则**：kvlang 决定 **what to compute where**；Triton 决定 **how to compute on one GPU**。

## 6. 与现有架构的关系

```
现有架构                          扩展后
────────                          ──────
parser.ParseFile()                parser.ParseFile()
    │                                 │
lower.Func()                      lower.Func()
    │                                 │
layoutcode.WriteFunc()            ┌─ graph.BuildGraph()    ← 新增
    │                             │    graph.ShapeInfer()
    │                             │    graph.Partition()   ← 分片决策
    │                             │    graph.Schedule()    ← 动态调度
    │                             └─ layoutcode.WriteFunc()
    │                                 │
kvcpu.Execute()                   kvcpu.Execute()
    │                                 │
dispatch.Compute() ──► op-plat    dispatch.Compute() ──► Triton op-plat
                                    新增: Shard/Gather/Barrier/Branch 指令
```

**零侵入**：现有 `parser → lower → layoutcode → kvcpu → dispatch` 管线保留。新 Pass 在 `lower` 之后、`layoutcode` 之前插入，对上下游透明。

## 7. 为什么这个分工是正确的

| 如果 kvlang 不做 | 后果 |
|-----------------|------|
| 动态 shape 分支 | 小 batch 也走 4 GPU 分片，通信开销吃掉所有加速 |
| 张量并行编排 | 用户必须手写 shard/gather，kvlang 退化为汇编级 |
| KV 空间路由 | tensor 在错误的节点上，跨网络拷贝成为瓶颈 |
| 设备拓扑感知 | GPU0 和 GPU1 有 NVLink 但 GPU2 没有，分片策略不考虑 |

| 如果 kvlang 做 Triton 的事 | 后果 |
|--------------------------|------|
| 手写 PTX/SASS | 不可移植，维护成本爆炸 |
| 自研 kernel compiler | 重复造轮子，Triton 社区已有数年积累 |
| 手动 shared memory 调优 | autotune 自动完成，手写不如机器搜索 |

## 8. 实现优先级

| Phase | 内容 | 依赖 |
|-------|------|------|
| 1 | Graph IR + BuildGraph pass | 无 |
| 2 | ShapeInfer (static + dynamic annotation) | Graph IR |
| 3 | Partition: 基于计算量的静态分片 | ShapeInfer |
| 4 | Schedule: 插入 BranchOp 处理动态 shape | Partition |
| 5 | 调度指令运行时 (Shard/Gather/Barrier) | Schedule |
| 6 | 设备拓扑感知 + 通信代价模型 | 调度指令 |
| 7 | 自动分片策略搜索 (cost-model driven) | 拓扑感知 |
