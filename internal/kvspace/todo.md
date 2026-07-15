# kvspace TODO：向最高标准设计对齐

> 基准文档：`最高标准设计.md`
> 当前状态：`internal/kvspace/kvspace.go`、`internal/kvspace/redis.go`
> 生成时间：2026-07-15

每条 TODO 标注：**影响范围**、**实施步骤**、**验收标准**。

---

## P0：编译阻断问题

### [P0-1] 删除 `Value.Dispatch` / `Executor` 设计

**问题**：`kvspace-typed-value-设计.md` 中的 `Value.Dispatch(opcode string) Executor` 若实现，
`kvspace` 包必须导入 `vtype` 包，而 `vtype` 已导入 `kvspace`，产生循环导入，编译直接报错。

**影响文件**：`internal/kvspace/value.go`（待新建）

**实施**：
- [ ] 新建 `value.go` 时不实现 `Dispatch` 方法，不定义 `Executor` 类型
- [ ] 删除设计文档中的 "Dispatch" 小节，或标注"此方法不实现"

**验收**：`go build ./...` 无循环导入错误

---

## P1：接口签名变更（影响全局）

### [P1-1] `Get` 返回 `Value`（替代 `string`）

**问题**：`Get → string` 导致类型信息丢失，消费者被迫 `strconv.Atoi` 猜类型。

**依赖**：[P2-1]（需先有 Value 类型实现）

**影响文件**：
- `internal/kvspace/kvspace.go`
- `internal/kvspace/redis.go`（`redisImpl.Get` 改用 `.Bytes()` + `DecodeValue`）
- 约 90 个调用点

**实施**：
- [ ] 修改接口 `Get(key string) (Value, error)`
- [ ] `redisImpl.Get`：`r.rdb.Get(bg, ...).Bytes()` → `DecodeValue`
- [ ] 编译 → 逐调用点包裹或迁移

**验收**：`go build ./...` 通过；无裸 `strconv.Atoi(kv.Get(...))` 用法残留

---

### [P1-2] `Set` 接受 `Value`（替代 `any`）

**问题**：`Set(key, any)` 经 go-redis 内部走 `fmt.Sprint(any)`，二进制值被字符串化破坏。

**依赖**：[P2-1]

**影响文件**：同 [P1-1]

**实施**：
- [ ] 修改接口 `Set(key string, val Value) error`
- [ ] `redisImpl.Set`：`EncodeValue(val)` 以 `[]byte` 传入 go-redis
- [ ] 编译 → 逐调用点包裹

**验收**：`go build ./...` 通过；`redis.go` 中 `rdb.Set` 的 value 参数类型为 `[]byte`

---

### [P1-3] `GetMany`/`SetMany` 替代 `Gets`/`Sets`

**问题**：
- `Gets(keys ...string)` variadic 与 slice 参数混用不直观
- `Sets(map[string]any)` 无顺序保证且类型不安全

**实施**：
- [ ] 接口新增 `GetMany(keys []string) ([]Value, error)`，删除 `Gets`
- [ ] 接口新增 `SetMany(pairs []KVPair) error`，删除 `Sets`；定义 `type KVPair struct{ Key string; Val Value }`
- [ ] `redisImpl.GetMany`：MGet `.Result()` → 逐项 `[]byte(v.(string))` → `DecodeValue`
- [ ] `redisImpl.SetMany`：pairs 中 value 位置传 `[]byte(EncodeValue(pair.Val))`
- [ ] 编译 → 修复调用点

**验收**：`go build ./...` 通过；无 `kv.Gets`/`kv.Sets` 残留

---

### [P1-4] `Watch` 返回 `Value`；`Notify` 接受 `Value`（替代 `string`/`any`）

**问题**：
- `Watch(key, timeout) (string, error)` 返回 string，丢失类型
- `Notify(key, any)` 经 go-redis 走 `fmt.Sprint(any)`，二进制值被字符串化

**语义说明**：Watch/Notify 是单 key 值变化通知机制，不是通用队列。
名称保持不变（Watch/Notify），仅修正值类型。

**实施**：
- [ ] 接口改为 `Watch(key string, timeout time.Duration) (Value, error)`
- [ ] 接口改为 `Notify(key string, val Value) error`
- [ ] `redisImpl.Watch`：BLPop 结果经 `[]byte(vals[1])` → `DecodeValue`
- [ ] `redisImpl.Notify`：value 经 `EncodeValue` 后以 `[]byte` 传入 LPush
- [ ] 编译 → 修复调用点

**验收**：`go build ./...` 通过；Watch 返回类型正确（如 `Notify` 传 `kvspace.Str("running")`，Watch 收到 `kind=="str"`）

---

### [P1-5] `DelTree` 替代 `DelR`

**问题**：`DelR` 的 "R" 含义不明（Recursive? Root?）。`DisConn` 保持不变。

**实施**：
- [ ] 接口改为 `DelTree(prefix) error`
- [ ] 编译 → 修复调用点

**验收**：`go build ./...` 通过；项目内无 `DelR` 残留

---

### [P1-6] 增加有语义的错误 sentinel

**实施**：
- [ ] 新建 `internal/kvspace/errors.go`，定义 `ErrNotFound`、`ErrClosed`、`ErrLinkLoop`
- [ ] `redisImpl.Get`：将 `redis.Nil` 映射为 `ErrNotFound`
- [ ] `redisImpl.Watch`：超时的 `redis.Nil` 映射为 `ErrNotFound`
- [ ] 全局替换 `errors.Is(err, redis.Nil)` 为 `errors.Is(err, kvspace.ErrNotFound)`

**验收**：调用方无需 import `github.com/redis/go-redis/v9` 来判断 key-not-found

---

## P2：Value 实现质量

### [P2-1] 新建 `internal/kvspace/value.go` 和 `encode.go`

**实施**：
- [ ] `value.go`：实现 `Value`、构造函数、accessor、`String() Stringer`（格式 `"kind:repr"`）；`Str()` 替代 `String()` 作为字符串内容访问器
- [ ] `encode.go`：实现 `EncodeValue`、`DecodeValue`（内部 copy raw，见下）、`isValidKind`、`EncodedSize`
- [ ] 定义 `KindInt/Float/Bool/Str/Bytes/Tensor` 常量

---

### [P2-2] `DecodeValue` 必须 copy raw bytes

**问题**：当前草稿中 `return Value{kind: kind, raw: data[start:start+int(rawLen)]}` 是对输入 `data` 的切片引用。go-redis 当前场景安全，但文件/流场景（设计文档明确声称支持）会导致静默数据损坏。

**实施**：
- [ ] `DecodeValue` 中替换为：
  ```go
  raw := make([]byte, rawLen)
  copy(raw, data[start:start+int(rawLen)])
  return Value{kind: kind, raw: raw}
  ```
- [ ] `fallbackStr` 同样 copy

**验收**：单元测试：修改 `DecodeValue` 的入参 `data` 后，已解码的 `Value.RawBytes()` 内容不变

---

### [P2-3] accessor 添加边界保护（返回零值，不 panic）

**问题**：草稿中 `Int()` / `Float()` 若 `len(raw) < 8` 会 panic（`decodeInt64LE` 越界）。

**实施**：
- [ ] `Int()`：检查 `v.kind != "int" || len(v.raw) < 8`，返回 `0`
- [ ] `Float()`：检查 `v.kind != "float" || len(v.raw) < 8`，返回 `0`
- [ ] `Bool()`：检查 `v.kind != "bool" || len(v.raw) == 0`，返回 `false`

**验收**：单元测试：对错误 kind 调用 accessor 不 panic，返回零值

---

### [P2-4] `String()` 实现 Stringer 而非返回字符串内容

**问题**：草稿中 `func (v Value) String() string { return string(v.raw) }` 使得
`fmt.Println(kvspace.Int(42))` 输出 8 字节乱码，调试时极具迷惑性。

**实施**：
- [ ] `String()` 输出 `"kind:repr"` 格式（见设计文档 §三）
- [ ] 原"获取字符串内容"的访问器改名为 `Str()`
- [ ] 全局替换调用方的 `v.String()` 为 `v.Str()`（若存在）

**验收**：`fmt.Sprintf("%v", kvspace.Int(42))` 输出 `"int:42"`；
`kvspace.Str("hello").Str()` 返回 `"hello"`

---

## P3：builtin 层集成（strconv 消除）

### [P3-1] `resolveReadValue` 返回 `Value`

**问题**：当前签名 `resolveReadValue(...) string`，接口改为 `Get→Value` 后此函数是类型断层：
它调用 `kv.Get` 拿到 `Value`，再 `.Str()` 转 string，再传给 `parseNativeValue` 重新解析——
整个 strconv 往返在这里发生。

**影响文件**：`internal/op/builtin/resolve.go`、`internal/op/builtin/helper.go`

**实施**：
- [ ] `resolveReadValue` 签名改为 `→ kvspace.Value`
- [ ] 字面量分支（`'"'` 前缀、数字、bool）直接返回对应 `kvspace.Int/Float/Bool/Str`
- [ ] KV 路径分支直接返回 `kv.Get(key)` 的 `Value`（忽略 error 返回 `Value{}`）
- [ ] `ResolveReadValue`（导出版）同步修改
- [ ] `readInputs` 中将 `kvspace.Value` 转换为 `nativeValue`：
  ```go
  func valueToNative(v kvspace.Value) nativeValue {
      switch v.Kind() {
      case "int":   return nativeValue{kind: "int",   i: v.Int(),   raw: strconv.FormatInt(v.Int(), 10)}
      case "float": return nativeValue{kind: "float", f: v.Float(), raw: strconv.FormatFloat(v.Float(), 'f', -1, 64)}
      case "bool":  return nativeValue{kind: "bool",  b: v.Bool(),  raw: strconv.FormatBool(v.Bool())}
      default:      return parseNativeValue(v.Str())
      }
  }
  ```

**验收**：`go build ./...` 通过；arithmetic 测试通过

---

### [P3-2] `writeResult` 写入类型化 `Value`（不再 `result.String()`）

**问题**：`writeResult` 当前将 `nativeValue` 序列化为 string 再 `kv.Set`，
导致 int/float/bool 结果以字符串存储，后续读取需要 `parseNativeValue` 重新猜类型。

**影响文件**：`internal/op/builtin/helper.go`

**实施**：
- [ ] `writeResult` 按 `result.kind` 写对应类型的 `kvspace.Value`（见设计文档 §八）
- [ ] 同步修改所有调用 `vthread.Set` 的地方：status `"running"` 用 `kvspace.Str("running")`

**验收**：arithmetic 运算结果以 typed Value 存储；`redis-cli GET /vt/0/.../x` 看到 TLV 头而非纯数字字符串

---

### [P3-3] `ExecuteCopy` 经由 `Value` 传递（保留类型）

**问题**：`ExecuteCopy` 当前读 string、写 string，copy 操作会丢失类型信息（int 经过 copy 变成 str）。

**影响文件**：`internal/op/builtin/helper.go`

**实施**：
- [ ] `ExecuteCopy` 改为 `v, _ := kv.Get(srcKey); kv.Set(dstKey, v)`（直接传递 Value）

**验收**：将 int 变量 copy 到另一槽后，目标槽的类型仍为 `"int"`

---

## P4：Redis 实现细节

### [P4-1] `checkLink` 改用 `.Bytes()`

**问题**：当前 `checkLink` 用 `.Result()` 返回 string，无法正确处理将来可能含非 ASCII 的目标路径（虽然目前路径都是 ASCII，但接口语义上应该 binary-safe）。

**实施**：
- [ ] 改为 `raw, _ := r.rdb.Get(bg, path).Bytes()`
- [ ] 检查 `len(raw) >= 2 && raw[0] == '-' && raw[1] == '>'`

**验收**：链接解析行为不变；`link_test.go` 通过

---

### [P4-2] 链接 sentinel 写入改为原始字节

**问题**：`Link` 方法写 `linkSentinel+target`（string），go-redis 按 string 写入——当前正确。
但为与整体"value 以 `[]byte` 传入"策略一致，应显式传 `[]byte`。

**实施**：
- [ ] `r.rdb.Set(bg, linkpath, []byte(linkSentinel+target), 0)`

**验收**：`link_test.go` 通过

---

## P5：低延迟扩展（RDMA 准备）

### [P5-1] 定义 `Middleware` 类型和 `Chain` 函数

**实施**：
- [ ] 在 `internal/kvspace/middleware.go` 中定义 `Middleware` 类型和 `Chain` 函数（见设计文档 §六）
- [ ] 提供一个 `WithLogging(logger)` 示范实现

**验收**：`kvspace.Chain(rawKV, WithLogging(log))` 编译通过

---


---

## 实施顺序建议

```
P0-1（删 Dispatch 设计）
  ↓
P2-1（新建 value.go + encode.go）
P2-2（copy raw）
P2-3（accessor 保护）
P2-4（Stringer fix）
  ↓
P1-1（Get→Value）
P1-2（Set→Value）
P1-3（GetMany/SetMany）
P1-4（Watch/Notify 类型化）
P1-5（命名：DelTree）
P1-6（错误 sentinel）→ 编译 → 修 ~90 调用点
  ↓
  上述 P1 全部通过编译后继续：
  ↓
P3-1（resolveReadValue→Value）
P3-2（writeResult 类型化）
P3-3（ExecuteCopy 保类型）
  ↓
P4-1（checkLink .Bytes）
P4-2（Link []byte）
  ↓
P5-1（Middleware）
```

---

## 不在此 TODO 范围内

| 项目 | 原因 |
|------|------|
| RDMA 实现 | 独立工程，依赖硬件环境，见 `kvspace-rdma-分布式设计.md` |
| `nativeValue` 合并到 `kvspace.Value` | 两者职责不同（storage vs in-memory eval），维持分离 |
| KV 事务（Txn） | 当前 kvlang 无原子读改写需求；待有场景时再加 |
| Watch-for-change 流式订阅（etcd 语义） | 当前 Watch/Notify 满足单次阻塞通知需求；持续订阅流不在规划内 |
