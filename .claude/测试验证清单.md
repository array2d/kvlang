# kvlang 测试验证清单

> **执行准则**：每次对 `cmd/kvlang/`、`internal/` 的改动，提交前必须通过本清单全部项目。  
> 自动化脚本：`.claude/test/run.sh`

---

## 前置条件

| 条件 | 检查命令 | 期望结果 |
|------|---------|---------|
| Redis 在线 | `redis-cli ping` | `PONG` |
| 二进制已构建 | `./kvlang help` | 打印 usage |
| 示例文件存在 | `ls tutorial/04-func/main.kv` | 文件存在 |

---

## 一、构建与静态检查

| # | 命令 | 期望 |
|---|------|------|
| 1.1 | `go build ./...` | 退出 0 |
| 1.2 | `go vet ./...` | 退出 0，无输出 |
| 1.3 | `go test ./...` | 所有包 ok 或 `[no test files]` |
| 1.4 | `make build` | 退出 0 |
| 1.5 | `.claude/hooks/check-keytree.sh` | 退出 0，无违规输出 |
| 1.6 | `grep -rn '"redis"' --include="*.go" . \| grep -v internal/kvspace` | 零行输出（无 redis 泄漏）|

---

## 二、kvlang help

| # | 命令 | 期望输出关键词 |
|---|------|--------------|
| 2.1 | `./kvlang help` | `usage:`, `load`, `serve`, `kvspace` |
| 2.2 | `./kvlang -h` | 同 2.1 |
| 2.3 | `./kvlang --help` | 同 2.1 |

---

## 三、kvlang load

| # | 命令 | 期望 |
|---|------|------|
| 3.1 | `./kvlang kvspace clear && ./kvlang load tutorial/04-func/main.kv` | 含 `loaded 1 file(s)` |
| 3.2 | 接上：`./kvlang kvspace get /func/main` | 含 `"entry":"pre_main"` |
| 3.3 | 接上：`./kvlang kvspace list /vthread` | **空**（未创建 vthread）|
| 3.4 | `./kvlang load --addr 127.0.0.1:6379 tutorial/04-func/main.kv` | 含 `loaded` |
| 3.5 | `./kvlang load tutorial/` （目录加载）| 含 `loaded N file(s)` |
| 3.6 | `./kvlang load` （缺路径）| stderr 含 `usage:`, 退出 2 |
| 3.7 | `./kvlang load --unknown` （未知 flag）| stderr 含 `flag provided but not defined`, 退出 2 |
| 3.8 | `./kvlang load --help` | 打印 `--addr` 说明, 退出 0 |

---

## 四、kvlang run（默认子命令）

### 4a. 文件模式

| # | 命令 | 期望 stdout |
|---|------|------------|
| 4.1 | `./kvlang kvspace clear && ./kvlang tutorial/04-func/main.kv` | `add(10,20) = 30` |
| 4.2 | `./kvlang --addr 127.0.0.1:6379 tutorial/04-func/main.kv` | `add(10,20) = 30` |
| 4.3 | `./kvlang tutorial/04-func/main.kv` | `A = 2`, `B = 3`, `C = 5` |

### 4b. 内联 `-c` 模式

| # | 命令 | 期望 stdout |
|---|------|------------|
| 4.4 | 见下方代码块 | `sum = 42` |

```bash
./kvlang kvspace clear
./kvlang -c "$(cat <<'KV'
def add2(A:int, B:int) -> (C:int) {
    './C' <- A + B
}
str.set("kvlangrun") -> './term'
add2(10, 32) -> './sum'
print("sum =", './sum')
KV
)"
```

### 4c. 管道模式

| # | 命令 | 期望 stdout |
|---|------|------------|
| 4.5 | `./kvlang kvspace clear && cat tutorial/04-func/main.kv \| ./kvlang` | `add(10,20) = 30` |

### 4d. 无参数 → serve 模式

| # | 命令 | 期望 |
|---|------|------|
| 4.6 | `timeout 2 ./kvlang` | stderr 含 `starting with`, 退出（超时）|

---

## 五、kvlang serve

| # | 命令 | 期望 |
|---|------|------|
| 5.1 | `timeout 2 ./kvlang serve` | stderr 含 `starting`, `registered`, `workers started` |
| 5.2 | `timeout 2 ./kvlang serve --addr 127.0.0.1:6379` | 同 5.1 且日志含 `kv=127.0.0.1:6379` |
| 5.3 | `./kvlang serve --help` | 打印 `--addr` 说明, 退出 0 |
| 5.4 | `./kvlang serve --badopt` | stderr 含 `flag provided but not defined`, 退出 2 |

### 5.5 load → serve 集成

```bash
./kvlang kvspace clear
./kvlang load tutorial/04-func/main.kv
timeout 6 ./kvlang serve
```

期望：
- stdout 含 `add(10,20) = 30`
- stderr 含 `entry=pre_main`，`vthread 1 created`

---

## 六、kvlang vet

| # | 命令 | 期望 |
|---|------|------|
| 6.1 | `./kvlang vet tutorial/04-func/main.kv` | `print_int.kv: OK` |
| 6.2 | `./kvlang vet --dump tutorial/04-func/main.kv` | 含 `File`, `Func`, `Instruction` |
| 6.3 | `./kvlang vet --lower tutorial/04-func/main.kv` | `print_int.kv: OK` |
| 6.4 | `./kvlang vet --dump --lower example/kvlang/controlflow/if_else.kv` | 含 `BlockStmt` 或 `_block_` |
| 6.5 | `cat tutorial/04-func/main.kv \| ./kvlang vet` | `stdin: OK` |
| 6.6 | `./kvlang vet --help` | 含 `--dump`, `--lower`, `-c` |
| 6.7 | `./kvlang vet` （无参数，无管道）| stderr 含 `usage:`, 退出 1 |

---

## 七、kvlang format

| # | 命令 | 期望 |
|---|------|------|
| 7.1 | `./kvlang format tutorial/04-func/main.kv` | 输出格式化后的代码，退出 0 |
| 7.2 | `./kvlang fmt tutorial/04-func/main.kv` | 同 7.1（别名）|
| 7.3 | `cat tutorial/04-func/main.kv \| ./kvlang format` | 同 7.1 |
| 7.4 | `./kvlang format --help` | 含 `-c` |

---

## 八、kvlang kvspace

### 8a. 基础 CRUD

```bash
./kvlang kvspace clear
./kvlang kvspace set /test/x hello
./kvlang kvspace get /test/x          # → hello
./kvlang kvspace set /test/y world
./kvlang kvspace mget /test/x /test/y # → /test/x\thello, /test/y\tworld
./kvlang kvspace list /test            # → x, y
./kvlang kvspace del /test/x
./kvlang kvspace get /test/x          # → 退出 1，stderr: redis: nil
```

### 8b. tree / dump

```bash
./kvlang kvspace clear
./kvlang load tutorial/04-func/main.kv
./kvlang kvspace tree /src/func        # 树形结构，含 print_int, _block_1
./kvlang kvspace dump /src/func/print_int  # key=value 列表
```

### 8c. notify / watch

```bash
./kvlang kvspace watch --timeout 3s /test/q &
sleep 0.3
./kvlang kvspace notify /test/q "hello"
wait  # watch 应在收到消息后退出，stdout 含 /test/q 和 hello
```

| # | 场景 | 期望 |
|---|------|------|
| 8.1 | `watch --timeout 1s /nonexistent` | 超时后 stderr: `redis: nil`, 退出 1 |
| 8.2 | `watch --help` | 含 `--timeout duration` |
| 8.3 | `watch --timeout badval /key` | `invalid value "badval"`, 退出 2 |

### 8d. --addr 全局 flag

| # | 命令 | 期望 |
|---|------|------|
| 8.4 | `./kvlang kvspace --addr 127.0.0.1:6379 get /func/main` | 正常返回 |
| 8.5 | `./kvlang kvspace --addr 127.0.0.1:9999 get /any` | 连接失败错误 |

### 8e. clear

| # | 命令 | 期望 |
|---|------|------|
| 8.6 | `./kvlang load ... && ./kvlang kvspace clear && ./kvlang kvspace list /src/func` | 空输出 |

---

## 九、Flag 错误处理（通用）

| # | 命令 | stderr 含 | 退出码 |
|---|------|----------|--------|
| 9.1 | `./kvlang load --unknown` | `flag provided but not defined: -unknown` | 2 |
| 9.2 | `./kvlang serve --unknown` | `flag provided but not defined: -unknown` | 2 |
| 9.3 | `./kvlang vet --unknown` | `flag provided but not defined: -unknown` | 2 |
| 9.4 | `./kvlang kvspace watch --timeout notaduration /k` | `invalid value "notaduration"` | 2 |
| 9.5 | `./kvlang kvspace get`（缺 key）| `usage: kvlang kvspace get` | 1 |
| 9.6 | `./kvlang kvspace set /k`（缺 value）| `usage: kvlang kvspace set` | 1 |
| 9.7 | `./kvlang kvspace`（缺子命令）| `usage: kvlang kvspace` | 1 |

---

## 十、架构合规检查

| # | 检查 | 命令 | 期望 |
|---|------|------|------|
| 10.1 | 零 redis 泄漏 | `grep -rn "go-redis\|redis.New" --include="*.go" . \| grep -v internal/kvspace \| grep -v go.mod \| grep -v go.sum` | 零行 |
| 10.2 | 零硬编码路径 | `.claude/hooks/check-keytree.sh` | 退出 0 |
| 10.3 | 零硬编码 opcode | `grep -rn '"call"\|"return"\|"newtensor"\|"deltensor"' --include="*.go" . \| grep -v _test.go \| grep -v "const "` | 零行 |
| 10.4 | keytree 函数覆盖 | `grep -rn '"/vthread/\|/src/func/\|/sys/\|/op/' --include="*.go" . \| grep -v internal/keytree` | 零行 |

---

## 附录 A：快速回归（最常用）

```bash
# 最小回归：30 秒内完成
go build ./... && go vet ./... && go test ./...

# 关键功能验证：60 秒
./kvlang kvspace clear
./kvlang tutorial/04-func/main.kv      # run file
cat tutorial/04-func/main.kv | ./kvlang  # pipe
./kvlang load tutorial/04-func/main.kv && \
  timeout 6 ./kvlang serve                                # load+serve

# 完整测试
.claude/test/run.sh
```

---

## 附录 B：测试文件说明

测试脚本使用的固定 fixture（不创建临时文件，直接用仓库内示例）：

| 用途 | 文件 | 预期 stdout |
|------|------|------------|
| 基础函数 + print | `tutorial/04-func/main.kv` | `add(10,20) = 30` |
| 算术运算 | `tutorial/04-func/main.kv` | `A = 2`, `B = 3`, `C = 5` |
| 控制流（vet 专用）| `example/kvlang/controlflow/if_else.kv` | 不直接执行 |
