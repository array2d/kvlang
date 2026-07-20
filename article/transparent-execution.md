# kvspace 透明执行 vs 传统执行：Agent 视角的对比

> 核心问题：agent 需要在**运行时**读取、修改、替换代码和任务流。
> 谁能做到？做到的代价是什么？

---

## 一、四种执行模型的本质差异

| | 编译二进制 | Python | Shell | kvspace |
|---|---|---|---|---|
| 代码存在哪里 | ELF 文件 / 内存只读段 | `.py` 文件 + `.pyc` 字节码 | `.sh` 文本文件 | **KV 路径**（`/func/`） |
| 状态存在哪里 | 寄存器 + 栈 + 堆（不透明） | Python 对象图（不透明） | 环境变量（扁平） | **KV 路径**（`/vthread/`） |
| 外部可读性 | ❌ 需 gdb/ptrace | ⚠️ 需侵入 debug API | ✅ env 可读，但扁平 | ✅ **任意 key 直接 Get** |
| 外部可写性 | ❌ 极危险（ptrace write） | ⚠️ `exec()`/猴子补丁，副作用多 | ✅ 写文件再 source | ✅ **任意 key 直接 Set** |
| 更新代码后生效时机 | 重启进程 | 下次 `import`（需手动触发） | 下次 `source` | **下次调用该函数时立即生效** |
| 崩溃后状态 | 全部丢失 | 全部丢失 | 全部丢失 | **KV 持久，继续执行** |

---

## 二、Agent 实时更新代码

### 2.1 编译二进制

```
agent 想更新 policy() 函数：
  1. 修改 .cpp 源文件
  2. 重新编译（分钟级）
  3. 停止当前进程（状态丢失）
  4. 启动新进程（从零开始）
```

**结论**：不可行。编译时间 + 进程重启 = agent 无法做到"运行时进化"。

---

### 2.2 Python

```python
# agent 动态修改策略
import importlib
new_code = agent.generate_policy()
exec(new_code, globals())           # 注入新函数定义
importlib.reload(policy_module)     # 或重载模块

# 问题：
# - 正在执行中的 policy() 调用帧不受影响（旧代码继续跑完）
# - 全局状态可能被污染（gc 未回收旧对象）
# - 线程安全问题：多线程同时调用时 reload 有竞态
# - 无法回滚：覆盖后旧版本不可恢复（除非手动备份）
```

**优势**：不需要重编译，`exec()`/`reload()` 可以在运行时注入新代码。  
**劣势**：正在执行的帧用旧代码，新帧用新代码，**新旧代码并存**，调试噩梦；状态在对象图里，agent 无法从外部读取执行进度。

---

### 2.3 Shell

```bash
# agent 更新任务流
cat > /tmp/new_policy.sh << 'POLICY'
policy() { echo "new behavior"; }
POLICY
source /tmp/new_policy.sh           # 立即生效

# 问题：
# - 函数只能操作字符串和文件，无复杂数据结构
# - 并发极差（subshell 开销大，无共享内存）
# - 状态只有环境变量，不支持嵌套结构
```

**优势**：`source` 可以立即替换函数定义，更新简单直接。  
**劣势**：数据结构极其贫乏（字符串/数组），无法承载张量计算、复杂状态；并发能力差。

---

### 2.4 kvspace

```bash
# agent 更新策略函数：一条命令，立即生效
kvlang kvspace set /func/robot/policy "def policy(state) -> (action) { ... }"

# 切换入口（原子性）
kvlang kvspace set /func/main '{"entry":"policy_v2"}'

# 下一个 vthread 调度时，使用新代码
# 正在执行的 vthread 继续用旧代码（隔离，无竞态）
# 旧代码仍在 KV 中，随时可以回滚：
kvlang kvspace set /func/main '{"entry":"policy"}'
```

**优势**：
- 更新是一次 KV `Set`，无编译，无重启，毫秒生效
- 新旧版本天然隔离：运行中的 vthread 用旧路径，新 vthread 用新路径
- **版本化免费**：旧代码仍在 `/func/robot/policy`，新代码在 `/func/robot/policy_v2`，回滚就是改入口
- agent 可以先读旧代码、再写新代码，整个过程是普通 KV 操作

**劣势**：
- 依赖 Redis 基础设施，不能像 Python 脚本一样开箱即跑
- 写入 KV 的代码需经过 `kvlang layoutrwir` 编译为指令树，纯文本 Set 不会自动编译（待支持热编译）

---

## 三、Agent 实时更新任务流

### Python 的任务流更新

```python
# 传统 DAG 框架（Airflow/Prefect）
# 更新 DAG 需要：重新部署 → 等待调度器刷新 → 新 run 才生效
# 运行中的 run 不受影响，但也无法修改

# 动态生成任务：
tasks = agent.plan()
for t in tasks:
    t.run()
# 问题：agent 无法在 tasks 执行一半时插入新任务
```

### kvspace 的任务流更新

```
正在执行的 vthread PC 在 /vthread/1/pre-main/step3/[2,0]

agent 发现 step3 之后需要插入 step3b：
  1. kv set /func/main/pre-main/step3b  "def step3b(...){...}"
  2. kv set /func/main/pre-main/[3,0]   "step3b"    ← 修改 step3 的下一条指令
  3. 当前 vthread 执行完 step3 后自然跳入 step3b

无需重启，无需通知，vthread 下一步自然执行新任务。
```

这是 kvspace 独有的能力：**任务流是 KV 路径树，agent 直接修改路径树即修改任务流**，对正在运行的 vthread 也有效（下一条指令时生效）。

---

## 四、综合对比

### 优势总结

| 能力 | 二进制 | Python | Shell | kvspace |
|------|:------:|:------:|:-----:|:-------:|
| 运行时替换函数 | ❌ | ⚠️ | ✅ | ✅ |
| 替换后立即对新任务生效 | ❌ | ✅ | ✅ | ✅ |
| 不影响正在执行的旧任务 | — | ❌ 有竞态 | ❌ 有竞态 | ✅ vthread 隔离 |
| 版本回滚 | ❌ | ❌ 需手动备份 | ❌ | ✅ 改入口路径 |
| 外部读取执行状态 | ❌ | ❌ | ⚠️ env only | ✅ 任意路径 |
| 外部修改执行中任务流 | ❌ | ❌ | ❌ | ✅ |
| 崩溃恢复 | ❌ | ❌ | ❌ | ✅ |
| 承载张量计算 | ✅ | ✅ | ❌ | ✅ |
| 无基础设施开箱即用 | ✅ | ✅ | ✅ | ❌ 需 Redis |

### 劣势总结

| 劣势 | 说明 | 缓解方案 |
|------|------|---------|
| 需要 Redis | 不能像 py/sh 脚本直接运行 | 嵌入式 KV 后端（计划中） |
| 热编译尚未支持 | `kv set /func/...` 写文本不自动编译 | 支持 `kvlang layoutrwir --hot` |
| 路径设计需要规范 | 滥用路径会导致混乱 | keytree 包强制统一路径 |
| 调试工具不成熟 | 没有 pdb/gdb 等成熟调试器 | KV 透明本身即是最好的调试 |

---

## 五、一句话定位

> **Python 给了 agent 修改代码的能力，但状态不透明；**
> **Shell 给了 agent 透明的状态，但数据结构太贫乏；**
> **kvspace 让代码和状态都在同一棵透明的 KV 树上——agent 用同一套接口读代码、读状态、改代码、改任务流，无需任何特殊 API。**
