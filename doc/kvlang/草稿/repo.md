# kvlang GitHub 项目优化评估

> 按 7 维度逐一评估现状，给出可执行改进项。目标：提高 star 数。
>
> 每个维度 0-10 分，10 = 顶尖开源项目水准。

---

## 1. 文档/规范（当前 3/10 → 目标 7/10）

### 现状
| 有 | 缺 |
|----|-----|
| README.md（含快速开始、语言速览） | LICENSE 文件 |
| grammar.bnf（形式语法） | 英文 README（当前仅中文） |
| 设计文档 20+ 篇（doc/） | 架构图（组件关系、数据流） |
| | 语言教程（step-by-step） |
| | API 文档 |

### 改进项（按优先级）

**P0 — 必须有**:
- [ ] **添加 LICENSE**（MIT 或 Apache 2.0）。无 LICENSE 的开源项目 star 数天然打折。
- [ ] **README 加英文版**（`README.md` → `README_CN.md`，新建 `README.md` 英文）。GitHub 默认流量是英文。

**P1 — 显著提升**:
- [ ] **架构图**：一张 mermaid/svg 图展示 `kvlang → parser → layoutcode → lower → opcode → vthread → kvspace → Redis` 流水线
- [ ] **5 分钟快速入门**：README 中一个完整可跑的 hello world（含预期输出）

**P2 — 锦上添花**:
- [ ] **语言规范独立文件**（`doc/LANGUAGE_SPEC.md`）：从 grammar.bnf 扩展出语义说明
- [ ] **API 参考**：builtin 函数一览表

---

## 2. 测试覆盖 & CI（当前 2/10 → 目标 7/10）

### 现状
| 有 | 缺 |
|----|-----|
| 4 个 `*_test.go`（link_test, op, parser） | CI pipeline（GitHub Actions） |
| `example/run.py` 集成测试 175 用例 | `go test -cover` 覆盖率报告 |
| `make test` 命令 | CI badge（README 中显示绿色 passing） |
| | 单元测试覆盖核心逻辑（kvcpu, lower, vthread） |

### 改进项

**P0**:
- [ ] **GitHub Actions CI**：`.github/workflows/ci.yml`，包含：
  ```yaml
  - go build ./...
  - go vet ./...
  - go test ./... -coverprofile=coverage.out
  - python3 example/run.py  # 集成测试
  services: redis:7-alpine
  ```
- [ ] **CI badge** 放在 README 顶部

**P1**:
- [ ] **补核心包单元测试**：`internal/kvcpu/`, `internal/lower/`, `internal/vthread/`, `internal/parser/`
- [ ] **`make cover`** 生成覆盖率 HTML

---

## 3. 可复现构建（当前 6/10 → 目标 8/10）

### 现状
| 有 | 缺 |
|----|-----|
| `go.mod`（仅 2 直接依赖） | 版本号/tag（`git tag`） |
| `Makefile`（build/test/vet/clean） | 二进制 release（GitHub Releases） |
| `.gitignore` 完整 | 多平台构建（当前仅 Linux） |
| `GOPROXY` 国内镜像配置 | |

### 改进项

**P0**:
- [ ] **打版本 tag**：`git tag v0.1.0 && git push --tags`。有 tag 才有 release，有 release 才有下载量/star 转化。
- [ ] **`make build-all`**：`GOOS=linux/darwin GOARCH=amd64/arm64` 四平台交叉编译

**P1**:
- [ ] **GitHub Release CI**：tag push 自动构建四平台二进制并发布
- [ ] **`go install` 支持**：`go install github.com/array2d/kvlang/cmd/kvlang@latest`

---

## 4. 依赖复杂度（当前 7/10 → 保持 7/10）

### 现状
- 2 个直接依赖：`gorilla/websocket` + `redis/go-redis/v9`
- 间接依赖仅 2 个（xxhash, atomic）
- **优势：极度精简**，这是亮点

### 建议
- **README 中显式标出**："仅 2 个运行时依赖"作为卖点
- Redis 是唯一外部服务依赖。README 中注明如何 3 行 CI 配置 Redis
- 考虑未来可选：`go build -tags nomqtt` 去掉 websocket 依赖

---

## 5. 示例/入门（当前 4/10 → 目标 8/10）

### 现状
| 有 | 缺 |
|----|-----|
| 179 个 `.kv` 示例文件 | 结构化教程（入门→进阶） |
| 覆盖 arith/cast/compare/logic/call/controlflow/algo | 每个示例的说明文档 |
| P0-P3 分级测试 | 与应用场景关联的 demo（如 HTTP API） |

### 改进项

**P0**:
- [ ] **创建一个独立 tutorial 目录**：
  ```
  tutorial/
    01-hello-world/    # print("hello")
    02-variables/      # 赋值与读取
    03-arithmetic/     # 加减乘除
    04-functions/      # def / call
    05-control-flow/   # if / loop
    06-recursion/      # fibonacci
  ```
  每个目录含 `main.kv` + `README.md`（中英双语）

**P1**:
- [ ] **README 中嵌入可运行的 demo gif/asciicast**
- [ ] **Playground 链接**（若有在线环境）

---

## 6. 安全/沙箱友好（当前 ?/10 → 目标 6/10）

### 分析
kvlang 的 KV 路径寻址天然适合沙箱：
- 所有 I/O 走 `kvspace`（可限制路径前缀）
- 无直接文件系统/网络访问（除非通过 `term_ws.go` 等 device 抽象）
- 指令集有限，无任意代码执行

### 改进项

- [ ] **文档化安全模型**：在 README/SECURITY.md 中说明沙箱边界
- [ ] **`--sandbox` flag**：限制访问路径前缀（如只允许 `/vt/<id>` 下操作）
- [ ] **资源限制**：最大 vthread 数、最大递归深度（已有 TCO，但显式说明）

---

## 7. 维护活跃度（当前 3/10 → 目标 6/10）

### 现状
| 有 | 缺 |
|----|-----|
| 频繁 commit（日更） | 版本 tag / release |
| 设计文档持续更新 | CHANGELOG |
| | CONTRIBUTING.md |
| | Issue/PR 模板 |
| | 项目 roadmap |

### 改进项

**P0**:
- [ ] **打第一个 release tag**（v0.1.0）
- [ ] **CHANGELOG.md**：按版本记录变更

**P1**:
- [ ] **CONTRIBUTING.md**：如何贡献代码、运行测试、提交 PR
- [ ] **Issue 模板**：`.github/ISSUE_TEMPLATE/bug_report.md`
- [ ] **ROADMAP.md**：未来 3 个月的计划

---

## 优先执行顺序

按 **star 转化率 × 实施成本** 排序：

| 优先级 | 项目 | 预计耗时 | 影响 |
|--------|------|---------|------|
| **1** | 添加 LICENSE (MIT) | 1 min | ⭐⭐⭐⭐⭐ |
| **2** | GitHub Actions CI + badge | 30 min | ⭐⭐⭐⭐⭐ |
| **3** | 英文 README + 架构图 | 1 h | ⭐⭐⭐⭐ |
| **4** | 打 tag v0.1.0 + CHANGELOG | 10 min | ⭐⭐⭐⭐ |
| **5** | 5 分钟入门教程（tutorial/） | 2 h | ⭐⭐⭐ |
| **6** | 补核心包单元测试 | 4 h | ⭐⭐⭐ |
| **7** | 多平台构建 + Release CI | 1 h | ⭐⭐ |
| **8** | CONTRIBUTING / Issue 模板 | 30 min | ⭐⭐ |

---

## 竞品对标

| | kvlang | Lua | Wren | Scheme (chibi) |
|--|--------|-----|------|----------------|
| 核心依赖 | 2 | 0 (纯 C) | 0 | 0 |
| 存储模型 | KV 路径树（Redis） | 栈+表 | 栈+对象 | 栈+链表 |
| 并发模型 | vthread 协程 | 单线程 | 单线程 | 单线程 |
| 定位 | 分布式原生 VM | 嵌入式脚本 | 嵌入式脚本 | 嵌入式脚本 |
| **差异化** | **数据和指令同一棵树，天然可持久化/可共享/可分布式** | | | |

kvlang 的核心卖点不是"又一个脚本语言"，而是 **"指令即数据，数据即指令"的 KV 树模型**。README 和文档应围绕这个差异化展开。