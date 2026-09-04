# kvlang benchmark

kvlang 与 Python / Rust / C 的跨语言性能基准。每个 case 是同一算法的**四份逻辑等价实现**，
规模阶梯冻结、跨版本可比，用于量化 kvlang 解释执行模型（每操作一次 KV 往返）相对原生/脚本语言的
常数因子，并**跨版本追踪 kvlang 自身的性能演进**。

当前 v0.2.5 的常数因子仍大，但这是**现阶段的现状、不是 kvlang 的固有属性**：后续版本会持续优化、
逐步逼近 Python 的水平。这套版本化快照的核心用途，正是把每一版的耗时钉在同一规模点上，
**逐版本量出 kvlang 的加速曲线**——今天与 Python 的差距，是用来被后续版本收窄的基线。

kvlang 是被测对象，**分别在三个 kvspace 后端上各跑一遍，占三列**；Python/Rust/C 与后端无关，
作原生/脚本基线各一列。三后端量的是「同一份 kvlang 程序在不同存储介质下每操作往返的真实代价」：

| 列 | 后端 | DSN | 量什么 |
|----|------|-----|--------|
| `kvlang_shm` | kvspace-c 共享内存 | `shm://<path>` | 纯内存态地板性能（无 I/O） |
| `kvlang_fs` | kvspace-durable 文件 | `fs://<dir>` | 每 KV 往返落文件系统的代价 |
| `kvlang_redis` | kvspace-durable redis | `redis://<host:port>` | 每 KV 往返走 TCP 的代价 |

每次采样前对应后端都会清空（shm 删文件 / fs 删目录 / redis flushall），杜绝残留污染。

> **冻结契约（可比性的前提）**：case 一经纳入，其四语言实现代码与**规模阶梯**即**永久冻结**，
> 后续任何 kvlang 版本迭代都**不得改动** `cases/**/*.{kv,py,rs,c}` 的逻辑，也不得改动 `run.py`
> 里该 case 的 `SWEEP` 规模点——唯有代码不变、规模点不变，`results/` 里跨时间同一 `(case,input)`
> 的耗时序列才真正可比。要压新维度就**加新 case**、要加规模点只**往阶梯尾部追加**（勿动已有点），
> 绝不改旧值。（修 bug 导致输出变化视同新基线，须在提交信息里显式声明并从该版本起断代对比。）

## 输入规模阶梯（sweep）

每个 case 不再只跑单一规模，而是沿一串规模点（`run.py` 的 `SWEEP`）依次放大，**逐点落一行**
（`input` 列记该点规模，如 `N=64`、`depth=6`、`rep=5`），从而看清耗时随输入的增长曲线。
规模经两条通道注入同一份逻辑等价代码，**四语言在每个规模点仍逐字节一致**：

- **kvlang**：源码里规模写作占位符 `__SCALE__`，`run.py` 按规模点文本替换后落临时文件再跑
  （kvlang 无「脚本读环境变量」的 builtin，`input` 只读 stdin，故走占位符）。
- **Python / Rust / C**：源码从环境变量 `BENCH_SCALE` 读规模（`os.environ` / `std::env::var` /
  `getenv`），`run.py` 每个规模点设一次环境变量、跑同一份未改写的源码（编译与规模无关）。

上限刻意放低，确保三后端全量约 **1 小时**内跑完（v0.2.5 每操作一次 KV 往返、常数因子仍大；
这是现阶段现状，后续版本会持续压低，规模点保持冻结才能逐版本量出加速）。

## case 集

八个经典算法各压一个语言层维度，另加两个 kvlang 架构地板参考。
每个 case 规模写死、跨版本可比，选让 kvlang 单次 ~2–3s（shm）的规模。

| case | 维度 | 关注 |
|------|------|------|
| `nqueens` | 递归 + 整数位运算 + 分支 | 位掩码回溯，`occ ^ all` 求可用列、`0-avail` 取最低位 |
| `fib` | 调用 / 帧寻址深度 | naive 递归，key 长度随深度增长（对齐 #116） |
| `quicksort` | 数组访问 + 递归 | 显式栈迭代 Lomuto 分区，LCG 造数 |
| `binary_search` | 有序表折半 | int 键映射作数组，逐次二分求和 |
| `binary_trees` | 内存分配 + 指针/引用 | L/R 子结点映射建满树，遍历栈跟随指针计数 |
| `hash_table` | 哈希表增删查 | Knuth 乘法散列，插入 + 查找求和 |
| `matmul` | 浮点运算 + 循环优化 | 稠密方阵乘三重循环，float64 校验和 ×1e6 精确对齐 |
| `k_nucleotide` | 字符串 + 哈希表 | 逐字符 `ord` 入哈希表统计碱基频次 |
| `iops` | 最小寻址单元往返地板价 | 单 key 读-改-写 `a<-a+1`，per-op 延迟（对齐 #204，参考基线） |
| `prime_sieve` | 计算 / 控制流密集 | 嵌套 `while` + 取模，O(n²) 内层迭代（参考基线） |

kvlang 的性能瓶颈是「PC/帧/局部全落 KV 树、每步一次往返」的架构本质（见 kvlang#194 #204 #116），
不是某个热点函数；`iops`/`prime_sieve` 单独隔离出这条地板价，其余八例是跨语言等价算法对照。

## 运行

```bash
python3 benchmark/run.py                 # 全部 case × 三后端，min of 3，追加 results.csv
python3 benchmark/run.py -k iops         # 只跑名字含 iops 的 case
python3 benchmark/run.py --repeat 1      # 快跑（单次，不取 min）
python3 benchmark/run.py --backends shm,fs   # 只跑部分 kvlang 后端
python3 benchmark/run.py --redis redis://127.0.0.1:6379   # 换 redis 地址
python3 benchmark/run.py --no-write      # 只打印不落 csv
python3 benchmark/run.py --show          # 打印 results.csv 历史后退出
```

依赖：`/usr/bin/kvlang`（可用 `--kvlang-bin` 覆盖）、`python3`、`rustc`、`gcc`；
`redis` 列还需本机 redis 与 `redis-cli`（不可用时该列留空，其余照常）。
shm 走临时文件、fs 走临时目录，均每次采样前清空。

## 计时约定

每份实现在功能输出之后打印一行 `__bench_ns: <整数纳秒>`，计时区间包住核心计算调用；
kvlang 那份再打印一行 `__bench_input: <规模>`（算法输入，与语言无关，只此一份被采纳，落 csv 的 `input` 列）。
runner 用正则取这两行，其余 stdout 为功能输出，用于**校验四语言输出逐字节一致**
（`valid` 列）——不一致说明某实现逻辑跑偏，该行数据不可信。

- kvlang：`time·now` / `time·sub` / `time/duration·as_nanos`
- Python：`time.perf_counter_ns()`
- Rust：`std::time::Instant`
- C：`clock_gettime(CLOCK_MONOTONIC)`

**原生编译优化档**（落 csv 的 `native_opt` 列）：`gcc -O1` / `rustc -C opt-level=1`；python 解释执行无编译期优化。
刻意**避开 -O2/-O3**：它们会把无外部副作用的循环闭式折叠/消除——`iops` 的 `a+=1` 在 `gcc -O2` 下
2000 次循环塌成常量赋值（88ns，纯失真），`-O1` 则保留真实工作量（761ns）。这不是让基线"跑得慢"，
而是让原生耗时**真实反映算法工作量**、随输入规模单调增长，从而与 kvlang 同口径可比。

## results/ 快照（版本化，每次运行一个文件）

每次运行写一个独立快照 `results/results-<tag>-<UTC 时间戳>.csv`，文件名带
**当前 kvlang 版本**（`git describe --tags --abbrev=0`）与**运行时刻**，一目了然属于哪个版本何时跑的。
`--show` 汇总 `results/` 下全部快照。列：

| 列 | 含义 |
|----|------|
| `timestamp` | UTC ISO8601 |
| `version` | `git describe --tags --always --dirty`（含 commit/dirty，精确区分同 tag 内迭代） |
| `commit` | short HEAD |
| `cpu` | CPU 型号（取自 `/proc/cpuinfo`，跨机对比须同型号，硬件不同不可比） |
| `case` | case 名 |
| `input` | 算法输入 / 规模（如 `N=6`、`depth=6`、`base×5=325`），取自 kvlang 的 `__bench_input` |
| `samples` | 采样次数（取 min） |
| `kvlang_shm_ns` `kvlang_fs_ns` `kvlang_redis_ns` | kvlang 在三后端各自最优耗时（纳秒），缺失后端留空 |
| `python_ns` `rust_ns` `c_ns` | 原生/脚本基线最优耗时（纳秒），缺失实现留空 |
| `python_ver` `rust_ver` `c_ver` | 各基线的解释器/编译器版本（`3.12.7` / `1.97.1` / `13.3.0`），工具升级也会影响基线 |
| `valid` | 所有已跑列的功能输出是否逐字节一致 |

**跨版本对比**：`--show` 汇总所有快照，过滤同一 `cpu` + `case`，按 `version` 排序看某一 `kvlang_*_ns` 列趋势。
例如 #116 三维地址曾把 prime_sieve 从 33.37s 降到 27.32s——这类演进就沉淀在快照序列里。
三列并排还能看清「后端 I/O 代价」占 kvlang 单操作开销的比重随版本如何变化；`*_ver` 列则保证基线可追溯。

## 加新 case

1. 建 `cases/<name>/`，放 `<name>.{kv,py,rs,c}` 四份逻辑等价实现。
2. 规模写死在代码里（**勿随版本改动**，否则历史不可比），选让 kvlang 单次 ~2–3s（shm）的规模。
3. 每份实现末尾打印 `__bench_ns:`，kvlang 那份再打印 `__bench_input: <规模>`；功能输出保持四语言逐字节一致。
4. 缺某语言实现时该语言留空跳过，不影响其余（但 `valid` 需 ≥2 份可比）。
