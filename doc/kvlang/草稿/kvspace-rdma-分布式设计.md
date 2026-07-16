# kvspace RDMA 分布式设计

## 1. 纠正：kvspace 不是单进程访问

之前 Pebble 内嵌方案有一个致命假设错误：

```
❌ 错误假设: kvspace 只有 kvlang VM Workers 在读写
            → 可以内嵌到 VM 进程中

✅ 实际情况: kvspace 是多组件共享的数据平面
```

**真实访问者**：

```
                        ┌──────────────────────┐
                        │     kvspace           │
                        │  (分布式 KV 数据平面)   │
                        └──────┬───────┬───────┘
                               │       │
        ┌──────────────────────┼───────┼──────────────────────┐
        │                      │       │                      │
        ▼                      ▼       ▼                      ▼
┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐
│ kvlang VM    │  │ op-plat      │  │ heap-plat    │  │ CLI / Tools  │
│ (Go workers) │  │ Triton/CUDA  │  │ GPU Memory   │  │ kvspace get  │
│              │  │ Python/C++   │  │ Allocator    │  │ kvlang vet   │
│ Node 0       │  │ Node 0-3     │  │ Node 0-3     │  │ 任意机器      │
└──────────────┘  └──────────────┘  └──────────────┘  └──────────────┘
```

- op-plat 是独立进程（Python/C++），不可能链接 Go 的 Pebble
- heap-plat 管理 GPU 显存，运行在 GPU 节点上
- 多个组件分布在不同物理机器上
- **kvspace 必须是独立可网络访问的服务**

## 2. 为什么是 RDMA？

### 2.1 kvlang 的延迟敏感路径

```
VM Worker 热路径 (每条指令):
  Decode  → Get(vthread slot)        ← 延迟敏感
  Execute → Get/Set(tensor metadata) ← 中等
  nextPC  → Set(vthread PC)          ← 延迟敏感
  Pick    → List(/vthread)           ← 延迟敏感

op-plat 路径:
  receive → Get(input tensor meta)   ← 延迟敏感
  compute → GPU kernel               ← 主导延迟
  done    → Notify(done:vtid)        ← 延迟敏感
```

每条 VM 指令 3-5 次 kvspace 操作。如果每次 100μs (TCP Redis)，则 0.5ms/指令 → **2000 指令/秒**。RDMA 降至 5μs → **40000 指令/秒**。

### 2.2 RDMA vs TCP 原理

```
TCP (Redis 当前):
  Client                    Server
    │                         │
    │── syscall write() ──►  │  CPU: 拷贝到内核
    │                         │  CPU: 协议栈处理
    │                         │  CPU: 拷贝到用户态
    │                         │  CPU: 处理命令
    │◄── syscall read() ────  │  CPU: 拷贝返回
    │                         │
  延迟: ~50-100μs (loopback)
        ~500μs (跨机)

RDMA (单边操作):
  Client                    Server (CPU 不参与!)
    │                         │
    │── RDMA READ ────────►  │  NIC: 直接读内存
    │◄── data ──────────────  │  NIC: DMA 返回
    │                         │
  延迟: ~1-5μs (同机架)
        ~10μs (跨交换机)
```

**关键差异**：RDMA 单边操作绕过远程 CPU，直接通过 NIC 访问远程内存。

## 3. 架构设计

### 3.1 整体拓扑

```
┌─────────────────────────────────────────────────────────────────┐
│                        RDMA Fabric                              │
│                   (InfiniBand / RoCE v2)                        │
│                                                                 │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐         │
│  │ kvspace Node │  │ kvspace Node │  │ kvspace Node │         │
│  │   (Master)   │  │   (Replica)  │  │   (Replica)  │         │
│  │              │  │              │  │              │         │
│  │ ┌──────────┐ │  │ ┌──────────┐ │  │ ┌──────────┐ │         │
│  │ │KV Memory │ │  │ │KV Memory │ │  │ │KV Memory │ │         │
│  │ │Region    │◄┼──┼─┤Region    │◄┼──┼─┤Region    │ │         │
│  │ │(RDMA注册)│ │  │ │(RDMA注册)│ │  │ │(RDMA注册)│ │         │
│  │ └──────────┘ │  │ └──────────┘ │  │ └──────────┘ │         │
│  │              │  │              │  │              │         │
│  │ ┌──────────┐ │  │ ┌──────────┐ │  │ ┌──────────┐ │         │
│  │ │Dir Index │ │  │ │Dir Index │ │  │ │Dir Index │ │         │
│  │ │(HashTbl) │ │  │ │(HashTbl) │ │  │ │(HashTbl) │ │         │
│  │ └──────────┘ │  │ └──────────┘ │  │ └──────────┘ │         │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘         │
│         │                 │                 │                   │
│         └─────────────────┼─────────────────┘                   │
│                           │                                     │
│  ┌────────────┐  ┌────────┴───┐  ┌────────────┐               │
│  │ kvlang VM  │  │ op-plat    │  │ heap-plat  │               │
│  │ (Go)       │  │ (Triton)   │  │ (GPU Mem)  │               │
│  │            │  │            │  │            │               │
│  │ RDMA READ  │  │ RDMA READ  │  │ RDMA WRITE │               │
│  │ 读 vthread │  │ 读 tensor  │  │ 写 metadata│               │
│  │ RDMA WRITE │  │ 元数据      │  │            │               │
│  │ 写结果     │  │            │  │            │               │
│  └────────────┘  └────────────┘  └────────────┘               │
│                                                                 │
│  所有组件通过 RDMA 单边操作直接访问 kvspace 内存                   │
└─────────────────────────────────────────────────────────────────┘
```

### 3.2 数据布局

```
kvspace 节点内存区域 (预注册 RDMA MR):

┌────────────────────────────────────────────────────────────┐
│  Region 0: Hash Index (固定大小, ~1GB)                      │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  bucket[0]: {key_hash, offset, size}  │ ...          │  │
│  │  bucket[1]: {key_hash, offset, size}  │              │  │
│  │  ...                                  │              │  │
│  │  bucket[N]: {key_hash, offset, size}  │              │  │
│  └──────────────────────────────────────────────────────┘  │
│                                                            │
│  Region 1: Data Store (动态扩展)                             │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  [key_bytes][value_bytes][key_bytes][value_bytes]... │  │
│  └──────────────────────────────────────────────────────┘  │
│                                                            │
│  Region 2: Dir Index (B-Tree, 只读)                         │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  /vthread     → ["run", "test-123", ...]            │  │
│  │  /src/func    → ["add_fn", "init", ...]         │  │
│  │  /sys         → ["vm", "term", "op-plat", ...]      │  │
│  └──────────────────────────────────────────────────────┘  │
│                                                            │
│  Region 3: Notify Queues (环形缓冲)                          │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  queue[done:run]    → [msg1][msg2]...               │  │
│  │  queue[NotifyVM]    → [msg1][msg2]...               │  │
│  └──────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────┘
```

### 3.3 操作映射

| kvspace 操作 | RDMA 实现 | 延迟 |
|-------------|----------|------|
| `Get(key)` | ① Hash index → offset<br>② RDMA READ data region | ~2μs |
| `Gets(keys)` | 批量 RDMA READ (doorbell batching) | ~5μs |
| `Set(key, val)` | ① RDMA WRITE data + index<br>② 原子 CAS 更新 hash bucket | ~5μs |
| `Sets(kvs)` | 批量 RDMA WRITE + 原子提交 | ~10μs |
| `List(prefix)` | RDMA READ dir index region | ~2μs |
| `Del(key)` | 原子 CAS 标记 tombstone | ~3μs |
| `DelR(prefix)` | 原子操作标记 + 异步 GC | ~5μs |
| `Watch(key)` | 轮询 RDMA READ notify queue | ~2μs |
| `Notify(key, val)`| RDMA WRITE notify queue | ~3μs |

### 3.4 一致性协议

RDMA 单边写入的一致性保证：

```
写入流程:
  Client                              kvspace Master
    │                                      │
    │── ① RDMA WRITE data to log ────────►│ NIC 写入内存
    │── ② RDMA WRITE atomic CAS index ───►│ NIC 原子更新 hash
    │                                      │
    │  CAS 成功 → 写入可见                   │
    │  CAS 失败 → 重试 (其他 client 竞争)     │

读取流程:
  Client                              kvspace (任一节点)
    │                                      │
    │── ① RDMA READ hash bucket ─────────►│ NIC 返回 {offset, ver}
    │── ② RDMA READ data at offset ──────►│ NIC 返回 {key, val, ver}
    │── ③ 校验 ver 一致 → 读取成功          │
    │── ③ 校验 ver 不一致 → 重试            │
```

**无锁读取**：版本号 (version) 保证读到一致的数据，不需要锁。

**原子写入**：CAS 保证只有一个 writer 成功。冲突时重试，概率极低（不同 key 无冲突）。

### 3.5 复制策略

```
┌──────────────┐                    ┌──────────────┐
│   Master     │                    │   Replica    │
│              │                    │              │
│  ┌────────┐  │   RDMA WRITE       │  ┌────────┐  │
│  │ WAL    │──┼───────────────────►│  │ WAL    │  │
│  │ Buffer │  │   批量复制          │  │ Buffer │  │
│  └────────┘  │   (every 100μs)    │  └────┬───┘  │
│              │                    │       │       │
│  ┌────────┐  │                    │  ┌────▼───┐  │
│  │ Data   │  │                    │  │ Data   │  │
│  │ Region │  │                    │  │ Region │  │
│  └────────┘  │                    │  └────────┘  │
└──────────────┘                    └──────────────┘

复制策略:
  - WAL 批量 RDMA WRITE 到所有 Replica (每 100μs 一批)
  - Replica 异步 apply WAL 到 Data Region
  - 读取可以从任意 Replica (允许亚毫秒级滞后)
  - 写入只走 Master (保证顺序)
```

## 4. 接口层：Go 侧 RDMA 封装

```go
// internal/kvspace/rdma/kvspace.go

type RDMASpace struct {
    dev      *rdma.Device          // RDMA 设备 (mlx5)
    pd       *rdma.ProtectionDomain
    mr       *rdma.MemoryRegion    // 本地注册内存
    qp       *rdma.QueuePair       // 连接到 kvspace Master
    cq       *rdma.CompletionQueue

    // Remote kvspace 内存布局 (发现时获取)
    remote    RemoteMemoryLayout
}

type RemoteMemoryLayout struct {
    HashRegion   rdma.RemoteMR   // Region 0: hash index
    DataRegion   rdma.RemoteMR   // Region 1: data store
    DirRegion    rdma.RemoteMR   // Region 2: dir index
    NotifyRegion rdma.RemoteMR   // Region 3: notify queues
}

func (r *RDMASpace) Get(key string) (string, error) {
    hash := fnv64(key)
    bucket := hash % r.remote.HashRegion.NumBuckets

    // ① RDMA READ: 读 hash bucket → 获取 {offset, size, version}
    bucketData := r.rdmaRead(r.remote.HashRegion, bucket*32, 32)
    offset, size, ver := parseBucket(bucketData)

    // ② RDMA READ: 读 data → 获取 {key, value, version}
    data := r.rdmaRead(r.remote.DataRegion, offset, size)
    if data.version != ver {
        return r.Get(key) // 重试: 并发写入导致版本变更
    }
    return data.value, nil
}

func (r *RDMASpace) Set(key string, value any) error {
    hash := fnv64(key)
    data := encode(key, value)

    // ① RDMA WRITE: 追加 data 到 log tail
    tail := atomic.Add(&r.remote.DataRegion.Tail, len(data))
    r.rdmaWrite(r.remote.DataRegion, tail, data)

    // ② RDMA ATOMIC CAS: 更新 hash bucket 指向新 offset
    bucket := hash % r.remote.HashRegion.NumBuckets
    oldBucket := r.rdmaRead(r.remote.HashRegion, bucket*32, 32)
    newBucket := encodeBucket(tail, len(data), oldBucket.version+1)
    swapped := r.rdmaCAS(r.remote.HashRegion, bucket*32, oldBucket, newBucket)

    if !swapped {
        return r.Set(key, value) // CAS 失败, 重试
    }
    return nil
}

func (r *RDMASpace) List(prefix string) ([]string, error) {
    // Dir Index 存在 Region 2: B-Tree 结构, 只读访问
    // ① RDMA READ dir root → 找到 prefix 对应的 B-Tree node offset
    // ② RDMA READ B-Tree node → 获取子节点列表
    nodeOff := r.lookupDir(prefix)
    node := r.rdmaRead(r.remote.DirRegion, nodeOff, pageSize)
    return parseChildren(node), nil
}

func (r *RDMASpace) Watch(key string, timeout time.Duration) (string, error) {
    // Notify 队列存 Region 3: 环形缓冲
    // 轮询 RDMA READ 检查新消息
    deadline := time.Now().Add(timeout)
    readPos := r.notifyReadPos[key]
    for time.Now().Before(deadline) {
        msg := r.rdmaRead(r.remote.NotifyRegion, queueOff+readPos, msgSize)
        if msg.seq > readPos {
            return msg.value, nil
        }
        time.Sleep(1 * time.Microsecond)
    }
    return "", ErrTimeout
}

func (r *RDMASpace) Notify(key string, value any) error {
    msg := encodeMessage(seq, value)
    // RDMA WRITE 到 notify queue
    off := atomic.Add(&r.remote.NotifyRegion.Tail[key], msgSize)
    r.rdmaWrite(r.remote.NotifyRegion, off, msg)
    return nil
}
```

## 5. 多语言客户端

kvspace RDMA 数据布局是**语言无关的内存格式**：

```
┌──────────────────────────────────────────────────────────┐
│  kvspace RDMA Memory Layout (C struct, ABI stable)        │
│                                                          │
│  HashBucket {                                            │
│      uint64_t key_hash;     // 8B                       │
│      uint64_t offset;       // 8B: data region 偏移     │
│      uint32_t size;         // 4B: value 大小            │
│      uint32_t version;      // 4B: 乐观锁版本号          │
│  }  // = 24B per bucket                                 │
│                                                          │
│  DirNode {                                               │
│      uint32_t num_children; // 4B                        │
│      char     names[][256]; // 变长: 子节点名             │
│      uint64_t offsets[];    // 变长: 子节点偏移           │
│  }                                                       │
│                                                          │
│  NotifyMsg {                                             │
│      uint64_t sequence;     // 8B: 单调递增              │
│      uint32_t size;         // 4B: payload 大小          │
│      char     payload[];    // 变长                       │
│  }                                                       │
└──────────────────────────────────────────────────────────┘
```

各语言客户端直接 mmap 或 RDMA read 这些结构：

| 语言 | RDMA 库 | 客户端 |
|------|---------|--------|
| Go | `go-rdma` 或 CGO+libibverbs | kvlang VM, kvspace CLI |
| Python | `pyverbs` (rdma-core) | Triton op-plat |
| C++ | `libibverbs` | heap-plat, CUDA op-plat |
| Rust | `rdma-sys` | 可选 |

## 6. 与传统方案的对比

| | Redis TCP | Pebble 内嵌 | RDMA kvspace |
|--|-----------|------------|-------------|
| 单机延迟 | 100μs | 0.2μs ✅ | 2μs |
| 跨机延迟 | 500μs | 不支持 ❌ | 5μs ✅ |
| 多语言支持 | ✅ (RESP) | ❌ (Go only) | ✅ (内存 ABI) |
| GPU 直接访问 | ❌ | ❌ | ✅ GPUDirect RDMA |
| 运维复杂度 | 低 | 低 | 中 (RDMA 网络) |
| 适用场景 | 开发/测试 | 单机部署 | **生产分布式** |

**关键优势：GPUDirect RDMA**

```
传统:
  GPU 显存 → cudaMemcpy → CPU 内存 → TCP send → 网络
                                           ↑
                                    两次拷贝

GPUDirect RDMA:
  GPU 显存 ──── RDMA WRITE ────► kvspace 内存
                    ↑
              GPU NIC 直接 DMA
              零 CPU 拷贝
```

op-plat 完成计算后，tensor 元数据可以直接从 GPU 通过 RDMA 写入 kvspace，无需经过 CPU。

## 7. 部署拓扑

```
┌──────────────────────────────────────────────────────────────┐
│  RDMA 网络 (InfiniBand HDR 200Gbps / RoCE v2 100Gbps)        │
│                                                              │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐         │
│  │ GPU Node 0  │  │ GPU Node 1  │  │ GPU Node 2  │         │
│  │             │  │             │  │             │         │
│  │ kvspace     │  │ kvspace     │  │ kvspace     │         │
│  │ Master      │  │ Replica     │  │ Replica     │         │
│  │             │  │             │  │             │         │
│  │ kvlang VM   │  │ kvlang VM   │  │ op-plat     │         │
│  │ (workers)   │  │ (workers)   │  │ Triton CUDA │         │
│  │             │  │             │  │             │         │
│  │ heap-plat   │  │ op-plat     │  │ heap-plat   │         │
│  │ GPU: H100x8 │  │ GPU: A100x8 │  │ GPU: MI300x8│         │
│  └─────────────┘  └─────────────┘  └─────────────┘         │
│                                                              │
│  ┌─────────────┐                                             │
│  │ Client Node │                                             │
│  │             │                                             │
│  │ kvspace CLI │── RDMA READ ──► 任意 kvspace 节点           │
│  │ kvlang vet  │                                             │
│  └─────────────┘                                             │
└──────────────────────────────────────────────────────────────┘
```

## 8. 实现路线

| Phase | 内容 | 时间 |
|-------|------|------|
| 1 | 内存数据结构定义 (C header, ABI stable) | 1 周 |
| 2 | kvspace Master: hash index + data store + dir index | 2 周 |
| 3 | Go RDMA 客户端 (libibverbs CGO) | 2 周 |
| 4 | 单边 Get/Set/List/Del 完整实现 | 1 周 |
| 5 | Notify/Watch via RDMA ring buffer | 1 周 |
| 6 | Replica + WAL 复制 | 2 周 |
| 7 | Python 客户端 (pyverbs) → op-plat 对接 | 1 周 |
| 8 | GPUDirect RDMA: tensor metadata 写入 | 1 周 |
| 9 | kvlang VM 切换到 RDMA kvspace | 1 周 |
