# kvlang Tensor 计算对接 Triton + GPU 加速器方案

> **注**：本文以 Triton/CUDA 为例说明集成模式。
> kvlang 后端是动态的，后端名（op-cuda / op-pytorch / op-jax / op-triton / op-tvm 等）
> 由外部 op-plat 进程自注册，kvlang 不硬编码任何后端名称。

## 1. 当前架构：异步消息分发

```
┌─────────────────────────────────────────────────────────────────┐
│  kvlang VM (Go)                                                 │
│                                                                 │
│  Execute() ─► Compute() ─► buildOpTask() ─► JSON ─► Notify()   │
│     ▲                                              │            │
│     │                                      Redis Queue          │
│     │                                    cmd:op-metal:0          │
│     │                                              │            │
│     │                                    ┌─────────▼──────────┐ │
│     │                                    │  op-plat 进程       │ │
│     │                                    │  (C++ / Python)     │ │
│     │                                    │  接收 OpTask JSON   │ │
│     └──────── WaitDone("done:vtid") ◄────│  执行算子           │ │
│                                          │  通知 done:vtid     │ │
│                                          └────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

**当前算子**（全部逐 op dispatch）：
```
add, sub, mul, div, relu, exp, log, sqrt, pow, abs, neg, sign,
min, max, zeros, matmul, save, load
```

## 2. Triton 内核：核心思路

Triton 在 Python 中写 GPU kernel，编译为 PTX (NVIDIA) 或 SPIR-V/MLIR (跨平台)：

```python
import triton
import triton.language as tl

@triton.jit
def add_kernel(x_ptr, y_ptr, out_ptr, N, BLOCK: tl.constexpr):
    pid = tl.program_id(0)
    offs = pid * BLOCK + tl.arange(0, BLOCK)
    mask = offs < N
    x = tl.load(x_ptr + offs, mask=mask)
    y = tl.load(y_ptr + offs, mask=mask)
    tl.store(out_ptr + offs, x + y, mask=mask)
```

Triton 自动处理了：
- GPU block/grid 调度
- 共享内存管理
- 向量化内存访问
- 自动调优 (autotune)

## 3. kvlang + Triton 集成架构

```
┌──────────────────────────────────────────────────────────────────────┐
│  kvlang VM (Go)                                                      │
│                                                                      │
│  ┌─────────────┐    ┌──────────────────┐    ┌────────────────────┐  │
│  │ 逐 op 模式   │    │ Fused 模式 (推荐) │    │ JIT Compile 模式   │  │
│  │ one-by-one  │    │ 多 op 合并 kernel │    │ 整函数编译为 kernel │  │
│  └──────┬──────┘    └────────┬─────────┘    └─────────┬──────────┘  │
│         │                    │                         │             │
│         ▼                    ▼                         ▼             │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │              OpTask JSON → Redis Queue                       │    │
│  │  { opcode, inputs[{key,dtype,shape,address}], outputs[...] } │    │
│  └──────────────────────────┬──────────────────────────────────┘    │
└─────────────────────────────┼───────────────────────────────────────┘
                              │
                    Redis: cmd:op-triton:0
                              │
┌─────────────────────────────▼───────────────────────────────────────┐
│  Triton op-plat 服务 (Python)                                        │
│                                                                      │
│  ┌────────────────────────────────────────────────────────────┐     │
│  │  OpTask 解析器                                              │     │
│  │  ├─ 读取 input tensor metadata (dtype, shape, gpu_addr)    │     │
│  │  ├─ 映射到 GPU 显存地址                                      │     │
│  │  └─ 选择执行路径: 单 op / fused kernel / 整图编译            │     │
│  └──────────────────────┬─────────────────────────────────────┘     │
│                         │                                            │
│         ┌───────────────┼───────────────┐                           │
│         ▼               ▼               ▼                           │
│  ┌──────────┐  ┌──────────────┐  ┌──────────────┐                  │
│  │ 单 op    │  │ Fused Kernel │  │ Graph Compile│                  │
│  │ triton   │  │ (2-5 ops → 1 │  │ (整个函数 →  │                  │
│  │ kernel   │  │  kernel)     │  │  1 kernel)   │                  │
│  └────┬─────┘  └──────┬───────┘  └──────┬───────┘                  │
│       │               │                 │                           │
│       ▼               ▼                 ▼                           │
│  ┌─────────────────────────────────────────────────────┐           │
│  │           Triton Compiler + GPU Runtime              │           │
│  │                                                     │           │
│  │  NVIDIA:  triton → PTX → CUDA Driver                │           │
│  │  AMD:     triton → HIP (via triton-amd)             │           │
│  │  Intel:   triton → SPIR-V → Level Zero              │           │
│  │  Apple:   triton → MLIR → Metal (WIP)               │           │
│  │  Qualcomm: triton → MLIR → OpenCL (WIP)             │           │
│  └─────────────────────┬───────────────────────────────┘           │
│                        │                                            │
│                        ▼                                            │
│              GPU 显存 (tensor 原地读写)                               │
│              通知 Redis: done:vtid                                   │
└─────────────────────────────────────────────────────────────────────┘
```

## 4. 三种执行模式详解

### 4.1 逐 op 模式（兼容现有架构）

每个 kvlang op 对应一个 Triton kernel。与现有 dispatch 完全兼容：

```kvlang
# kvlang 源码
add('/data/a', '/data/b') -> '/data/c'   # Redis → OpTask → Triton add_kernel
mul('/data/c', '/data/d') -> '/data/e'   # Redis → OpTask → Triton mul_kernel
relu('/data/e') -> '/data/f'             # Redis → OpTask → Triton relu_kernel
```

每次 round-trip：VM → Redis → Triton → Redis → VM。**适合调试和兼容**。

### 4.2 Fused 模式（⚡ 性能关键）

kvlang 新增 `fuse_begin` / `fuse_end` 指令，将连续 op 合并为一个 kernel：

```kvlang
def mlp_forward() -> ('/data/output') {
    # ... alloc ...
    fuse_begin()                              # 开始记录
    add('/data/t1', '/data/B')  -> '/data/t2' # ─┐
    relu('/data/t2')            -> '/data/h'   #  │ 合并为 1 个
    mul('/data/h', '/data/W2')  -> '/data/t3'  #  │ Triton kernel
    add('/data/t3', '/data/B2') -> '/data/o'   # ─┘
    fuse_end()                                # 编译+执行
    # ... cleanup ...
}
```

VM 将 `fuse_begin..fuse_end` 间的 ops 收集为一个 `FusedOpTask`，Triton 后端自动生成融合 kernel：

```python
@triton.jit
def fused_mlp_kernel(t1_ptr, B_ptr, W2_ptr, B2_ptr, o_ptr, N, BLOCK: tl.constexpr):
    offs = tl.arange(0, BLOCK) + tl.program_id(0) * BLOCK
    mask = offs < N
    # 所有中间结果保持在寄存器中，零显存读写
    t2  = tl.load(t1_ptr + offs, mask=mask) + tl.load(B_ptr + offs, mask=mask)
    h   = tl.maximum(t2, 0)                    # relu
    t3  = h * tl.load(W2_ptr + offs, mask=mask)
    o   = t3 + tl.load(B2_ptr + offs, mask=mask)
    tl.store(o_ptr + offs, o, mask=mask)
```

**性能收益**：4 次显存往返 → 1 次。典型加速 2-4x。

### 4.3 JIT Compile 模式（🚀 极致性能）

整个 kvlang 函数编译为单个 Triton kernel：

```kvlang
@triton_compile                             # 编译注解
def mlp_forward() -> ('/data/output') { ... }
```

VM 将函数体 AST 发送给 Triton 后端，后端直接生成 kernel 并缓存编译结果。后续调用直接执行缓存 kernel，**零 dispatch 开销**。

## 5. GPU 加速器支持矩阵

| 厂商 | GPU 系列 | Triton 后端 | 状态 | kvlang 标识 |
|------|---------|------------|------|------------|
| NVIDIA | H100/A100/V100/RTX | triton → PTX | ✅ 生产就绪 | `op-triton-cuda` |
| AMD | MI300/MI250/7900XTX | triton-amd → HIP | ⚠️ 实验 | `op-triton-rocm` |
| Intel | PVC/Arc/Flex | triton → SPIR-V | ⚠️ 实验 | `op-triton-xpu` |
| Apple | M2/M3/M4 Ultra | triton → MLIR → Metal | 🔬 WIP | `op-triton-metal` |
| Qualcomm | Adreno/Snapdragon X | triton → MLIR → OpenCL | 🔬 WIP | `op-triton-qcom` |

### 5.1 op-plat 实例注册

每个 GPU 启动一个 Triton op-plat 实例，向 kvspace 注册：

```
/sys/op-plat/op-triton-cuda:0  → {"program":"op-triton-cuda","status":"running","load":0.3,"device":"NVIDIA H100","memory":"80GB"}
/sys/op-plat/op-triton-rocm:0  → {"program":"op-triton-rocm","status":"running","load":0.1,"device":"AMD MI300X","memory":"192GB"}
/sys/op-plat/op-triton-cuda:1  → {"program":"op-triton-cuda","status":"running","load":0.5,"device":"NVIDIA A100","memory":"40GB"}
```

每个实例在 `/op/op-triton-cuda/func/` 下注册支持的算子。

### 5.2 负载均衡与设备选择

`Select()` 函数根据 opcode 支持情况和实例负载自动选择：

```go
// 新增 device affinity: 优先选择 tensor 所在 GPU
func SelectWithAffinity(ctx, kv, opcode, preferredDevice string) (string, error) {
    // 1. 过滤: 支持该 opcode 且设备匹配
    // 2. 排序: device affinity > 低负载
    // 3. 返回: "triton-cuda:0"
}
```

## 6. Tensor 数据流

```
┌──────────────────────────────────────────────────────────────┐
│  heap-plat (生命周期管理)                                      │
│                                                              │
│  newtensor("f32","[1024]") → '/data/x'                       │
│    ├─ 在 GPU 显存分配 1024*4=4KB                              │
│    ├─ 写入 metadata: {dtype:"f32", shape:[1024],              │
│    │   address:{device:"cuda:0", ptr:0x7f...}}               │
│    └─ 设置 '/data/x' 的 kv 值为 JSON metadata                 │
│                                                              │
│  deltensor('/data/x')                                        │
│    ├─ 读取 metadata 获取 GPU 地址                              │
│    └─ cudaFree(ptr)                                          │
└──────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────┐
│  Tensor 在 Redis 中的存储                                      │
│                                                              │
│  Key: /data/x                                                │
│  Value: {"dtype":"f32","shape":[1024],                       │
│           "address":{"backend":"cuda","device":0,             │
│           "ptr":"7f8a4c000000","size":4096}}                 │
│                                                              │
│  ⚠️ 实际数据在 GPU 显存，Redis 只存元数据                       │
└──────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────┐
│  OpTask 传输                                                  │
│                                                              │
│  {                                                           │
│    "opcode": "add",                                          │
│    "inputs": [                                               │
│      {"key":"/data/a","dtype":"f32","shape":[1024],          │
│       "address":{"device":"cuda:0","ptr":"7f8a4c000000"}},   │
│      {"key":"/data/b","dtype":"f32","shape":[1024],          │
│       "address":{"device":"cuda:0","ptr":"7f8a4c001000"}}    │
│    ],                                                        │
│    "outputs": [                                              │
│      {"key":"/data/c","dtype":"f32","shape":[1024],          │
│       "address":{"device":"cuda:0","ptr":"7f8a4c002000"}}    │
│    ]                                                         │
│  }                                                           │
│                                                              │
│  Triton 后端直接通过 ptr 访问 GPU 显存，零拷贝                   │
└──────────────────────────────────────────────────────────────┘
```

## 7. 实现路线图

### Phase 1: 基础 op-plat (2-3 周)

```
├── 实现 triton_op_plat.py (Python 服务)
│   ├── Redis 连接 + cmd:op-triton-cuda:0 监听
│   ├── OpTask JSON 解析
│   ├── 单 op Triton kernel 库 (add/sub/mul/div/relu/exp/log/...)
│   └── 结果通知 done:vtid
├── 注册到 /op/op-triton-cuda/func/*
├── heap-plat 支持 CUDA 显存分配 (cudaMalloc/cudaFree)
└── 端到端: mlp_small.kv 在 GPU 上运行
```

### Phase 2: Fused Kernel (2-3 周)

```
├── kvlang 新增 fuse_begin/fuse_end 指令
├── Triton 后端: 解析 FusedOpTask → 生成融合 kernel
├── Autotune: 自动搜索最优 BLOCK_SIZE / num_warps
└── 缓存: 编译产物按 op 序列 hash 缓存到 /cache/
```

### Phase 3: 多 GPU + 图编译 (3-4 周)

```
├── triton-amd (ROCm) / triton-xpu (Intel) 后端
├── 多 GPU 负载均衡 + device affinity
├── @triton_compile 整函数 JIT
├── 跨 GPU tensor 迁移 (PCIe/NVLink)
└── 异步流水线: 多 op 并行 dispatch
```

### Phase 4: 生产优化 (持续)

```
├── FP8/INT8 量化 kernel
├── FlashAttention / custom attention
├── 与 PyTorch 互操作 (import/export tensor)
├── 自动微分 (grad 函数)
└── 分布式: 多机 NCCL/RCCL 集合通信
```

## 8. 关键技术决策

| 决策点 | 选择 | 理由 |
|--------|------|------|
| op-plat 语言 | Python | Triton 只有 Python DSL，kernel launch 也是 Python |
| 进程间通信 | Redis queue (不变) | 保持 kvlang 架构一致性 |
| GPU 内存管理 | 独立 heap-plat | 显存分配/释放独立于计算，支持内存池 |
| Tensor 数据 | GPU 显存 (非 Redis) | Redis 只存元数据，避免 GPU↔CPU 拷贝 |
| Kernel 缓存 | hash(op_seq) → /cache/ | 避免重复编译，首次编译 ~100ms，缓存命中 ~1μs |
| 算子粒度 | 默认逐 op，可 fused | 兼容现有逐 op dispatch，按需开启 fused 模式 |

## 9. 与现有架构的兼容性

**零破坏性变更**：
- `dispatch.Compute()` 不变 — 仍通过 `Select()` 选后端 + `Notify()` 发任务
- `OpTask` JSON 结构不变 — 只需 Triton 后端解析 `address.ptr` 字段
- kvlang 语法不变 — `add(A, B) -> C` 保持不变
- 新增 `fuse_begin/fuse_end` 和 `@triton_compile` 是可选的增强

**切换方式**：在 `/op/` 下注册 `op-triton-cuda` 算子 → `Select()` 自动发现并路由 tensor ops 到 Triton 后端。
