> 本文档，遵守父级文档 `doc/kvlang/设计/kvlang语言深度理解.md`，是其子模块的丰富。

# kvspace 类型化 Value 设计 (vtype 集成版)

## 问题

当前 `Get` 返回 `string`，所有值被 Redis 强制字符串化，类型信息丢失，消费者被迫 `strconv.Atoi` 猜测类型。

## 核心思路：Value 的 Kind 即 vtype Name

kvlang 已有 `internal/vtype` 类型系统（`str`、`tensor` 命名空间）。kvspace 的 `Value` 不应独立设计一套类型标签，而应与 vtype 统一：**Value 的 Kind 就是 vtype 的 Name**。

```
kvspace.Value              vtype.VType
─────────────              ───────────
kind: "int"       ←──→    (内建标量类型)
kind: "float"     ←──→    (内建标量类型)
kind: "bool"      ←──→    (内建标量类型)
kind: "str"       ←──→    strVType
kind: "tensor"    ←──→    tensorVType
kind: "bytes"     ←──→    (内建标量类型)
```

语义：**Value 是有类型的通用数据载体；VType 是该类型的算子命名空间。** 同一类型名同时出现在两个层面——存储层（Value kind）和计算层（VType dispatch）。

## 编码格式

```
[1 byte kind_len][N bytes kind_name][M bytes raw_value]
```

| kind | kind_len | raw_value |
|------|----------|-----------|
| `"int"` (3B) | `0x03` | 8B int64 LE |
| `"float"` (5B) | `0x05` | 8B float64 IEEE 754 LE |
| `"bool"` (4B) | `0x04` | 1B: 0x00/0x01 |
| `"str"` (3B) | `0x03` | UTF-8 bytes |
| `"tensor"` (6B) | `0x06` | JSON metadata |
| `"bytes"` (5B) | `0x05` | raw bytes |

kind_name 字符串化而非数字枚举——人类可读、Redis 中 `GET /key` 直接看到 `\x03int\x2a\x00...` 而非 `\x01\x2a\x00...`，调试友好。vtype 的 `Register()` 注册新类型时，kind_name 随之可用。

## Go 侧 Value 类型

```go
// internal/kvspace/value.go

// Value 是 kvspace 中存储的 vtype-typed 值。零值表示 nil。
type Value struct {
    kind string   // vtype name: "int", "float", "bool", "str", "tensor", "bytes"
    raw  []byte   // 原始值字节
}

// ── 构造（与 vtype Name 对齐） ──

func Int(v int64) Value     { return Value{kind: "int", raw: encodeInt64LE(v)} }
func Float(v float64) Value { return Value{kind: "float", raw: encodeFloat64LE(v)} }
func Bool(v bool) Value     { b := byte(0); if v { b = 1 }; return Value{kind: "bool", raw: []byte{b}} }
func Str(v string) Value    { return Value{kind: "str", raw: []byte(v)} }
func Bytes(v []byte) Value  { return Value{kind: "bytes", raw: v} }

// TensorValue 由 tensor vtype 构造
func Tensor(meta []byte) Value { return Value{kind: "tensor", raw: meta} }

// ── 解码 ──

func (v Value) IsNil() bool    { return v.kind == "" }
func (v Value) Kind() string   { return v.kind }  // vtype name
func (v Value) Int() int64     { return decodeInt64LE(v.raw) }
func (v Value) Float() float64 { return decodeFloat64LE(v.raw) }
func (v Value) Bool() bool     { return len(v.raw) > 0 && v.raw[0] != 0 }
func (v Value) String() string { return string(v.raw) }
func (v Value) Bytes() []byte  { return v.raw }

// ── 调度：Value 自行路由到对应 VType ──

// Dispatch 将 Value 路由到对应 VType 的算子执行。
// 标量类型（int/float/bool/str/bytes）由 builtin 处理；
// 复合类型（tensor）由对应 VType.Exec 处理。
func (v Value) Dispatch(opcode string) Executor { ... }
```

## 接口变更

```go
type KVSpace interface {
    Get(key string) (Value, error)
    Gets(keys ...string) ([]Value, error)
    Set(key string, value Value) error   // 不再用 any，强制类型化
    Sets(kvs map[string]Value) error
    // ... 其余不变
}
```

`Set(key, any)` 不再接受裸 `any`。调用方必须显式选择 `kvspace.Int(42)` 或 `kvspace.Str("running")`。**类型在 Set 时确定，Get 时不猜。**

## 与 vtype 的统一

```
                  ┌──────────────────────────────┐
                  │        kvspace.Value          │
                  │  kind: "tensor"               │
                  │  raw:  {dtype,shape,address}  │
                  └──────────────┬───────────────┘
                                 │
                                 │ vtype.Lookup("tensor") → tensorVType
                                 │
                  ┌──────────────▼───────────────┐
                  │        vtype.VType            │
                  │  Name() → "tensor"            │
                  │  Exec("tensor.add", kv, pc, inst)  │
                  └──────────────────────────────┘
```

新增 vtype 时，只需 `Register(myVType)`——其 `Name()` 自动成为合法的 Value.kind，无需修改 kvspace 包。

## 相比 v1 方案（固定 1-byte tag）的改进

| | v1 (固定 tag) | v2 (vtype 集成) |
|--|--------------|----------------|
| 扩展性 | 新增类型需改 tag 常量 | 新类型只需 `vtype.Register()` |
| 调试性 | `\x01` 不可读 | `\x03int` 人类可读 |
| 类型语义 | kvspace 自己维护类型表 | 与 vtype 命名空间统一 |
| Dispatch | Value 不感知 dispatch | `Value.Dispatch(op)` 直接路由到 vtype |

---

## Redis 二进制存储实现方案

### 问题根因

当前 `redisImpl` 类型丢失发生在两个层面：

| 层面 | 现象 | 导致 |
|------|------|------|
| **接口层** | `Get→string` / `Set(key, any)` | 类型标签在函数签名处丢失 |
| **go-redis 层** | `Set` 对 `any` 走 `fmt.Sprint`；`Get` 用 `.Result()` 返回 string | 即便上游传入二进制 `[]byte`，如果走了 `any` → `fmt.Sprint` 路径，原始字节也会被字符串化破坏 |

**结论：必须在 Value 自身中嵌入类型标签（自描述编码），并且 redisImpl 在 go-redis 边界上使用 `[]byte` 而非 `string`/`any` 透传。**

### go-redis 二进制安全边界

Redis 协议是二进制安全的（RESP bulk string 有显式长度字段，不依赖 `\0` 终止）。go-redis v9 的读写行为如下：

| 操作 | 当前用法 | 问题 | 改进 |
|------|---------|------|------|
| `rdb.Set` | `Set(ctx, key, value, 0)` — `value: any` | 非 `string`/`[]byte` 走 `fmt.Sprint`，破坏二进制 | 传入 `[]byte(encoded)`，go-redis 直接作为 bulk string 写入 |
| `rdb.Get` | `.Result()` → `(string, error)` | Go string 可存任意字节，但语义是 text | 改用 `.Bytes()` → `([]byte, error)`，语义明确 |
| `rdb.MGet` | `.Result()` → `([]interface{}, error)` | 元素是 `string`，需手动 `[]byte(v.(string))` 取回 | 保持 `.Result()`，因为 go-redis 的 `MGet` 不支持 `.Bytes()`；在 `Gets` 中执行 `string`→`[]byte` 转换后 decode |
| `rdb.MSet` | `MSet(ctx, pairs...)` — pairs 为 `[]any` | 同上 Set 问题 | pairs 中 value 位置传入 `[]byte(encoded)` |
| `rdb.BLPop` | `.Result()` → `([]string, error)` | 同上 Get 问题 | 改用 `.Bytes()` 返回 `([]string, []byte)` 的替代方案；或 `.Result()` 后 `[]byte(s)` |
| `rdb.LPush` | `LPush(ctx, key, value)` — `value: any` | 同上 Set 问题 | 传入 `[]byte(encoded)` |

**关键原则：在 go-redis 调用边界上，永远用 `[]byte` 作为 value 的载体** — 无论是 Set/MSet/LPush 的入参，还是 Get/MGet/BLPop 的出参。

### 编码与解码（Redis 层）

```go
// internal/kvspace/encode.go

// EncodeValue 将 Value 编码为 Redis 可存储的 []byte。
// 格式: [1 byte kind_len][N bytes kind_name][M bytes raw_value]
func EncodeValue(v Value) []byte {
    if v.IsNil() {
        return nil
    }
    kind := v.kind
    buf := make([]byte, 1+len(kind)+len(v.raw))
    buf[0] = byte(len(kind))
    copy(buf[1:], kind)
    copy(buf[1+len(kind):], v.raw)
    return buf
}

// DecodeValue 从 Redis 读回的 []byte 解码为 Value。
// 若 data 不以 kind_len 开头（如旧版纯字符串数据），返回 Value{kind: "str", raw: data}。
func DecodeValue(data []byte) Value {
    if len(data) == 0 {
        return Value{} // nil
    }
    kindLen := int(data[0])
    // 最小完整帧: 1 byte kind_len + kindLen + 0 bytes raw
    if len(data) < 1+kindLen {
        // 旧版数据或损坏数据 → 当 str 处理
        return Value{kind: "str", raw: data}
    }
    // 额外检查：kind_name 必须是可打印 ASCII，以防把旧版二进制数据误判为 typed value
    kind := string(data[1 : 1+kindLen])
    if !isValidKind(kind) {
        return Value{kind: "str", raw: data}
    }
    return Value{kind: kind, raw: data[1+kindLen:]}
}

// isValidKind 检查字符串是否为合法的 vtype name（字母/数字/下划线，不可为空）。
func isValidKind(s string) bool {
    if len(s) == 0 {
        return false
    }
    for _, c := range s {
        if !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
            return false
        }
    }
    return true
}
```

**向后兼容设计**：当读到不以有效 kind 开头的二进制数据时，自动 fallback 为 `Value{kind: "str", raw: data}` — 旧版纯字符串数据无需迁移即可读取。新写入的数据带类型前缀，旧版 reader 读到会看到前缀乱码（可接受，因为旧版本来就无类型概念）。

### 软链接 sentinel 的二进制兼容

当前 `checkLink` 通过 `strings.HasPrefix(val, "->")` 检测链接。编码后 value 为 `[kind_len][kind][raw]`，`kind_len` 取值范围 1~127（最大 kind 名 127 字节），而 `'->'` 的字节是 `0x2D 0x3E`（0x2D = 45）。

**不会有冲突**：编码后的 value 首字节是 `kind_len`（1~127 中 ≤ 44 是合法类型名长度；45~127 不可能出现，因为 kind 名不会到 45 字节），且链接存储本身不编码（`linkSentinel + target` 是纯字符串）。检测逻辑不变：

```go
// checkLink 逻辑无需修改：
// 1. 从 Redis GET 原始字节（用 .Bytes()）
// 2. 检查前 2 字节是否为 "->"（0x2D 0x3E）
// 3. 若是链接 → 返回 target；否则交给 DecodeValue 解析
func (r *redisImpl) checkLink(path string) string {
    // ...
    raw, _ := r.rdb.Get(bg, path).Bytes()
    if len(raw) >= 2 && raw[0] == '-' && raw[1] == '>' {
        t = string(raw[2:])
    }
    // ...
}
```

### redisImpl 改造要点

```go
// ── CRUD 改造 ──

func (r *redisImpl) Get(key string) (Value, error) {
    raw, err := r.rdb.Get(bg, resolveCore(key, r.checkLink)).Bytes()
    if err != nil {
        return Value{}, err
    }
    return DecodeValue(raw), nil
}

func (r *redisImpl) Gets(keys ...string) ([]Value, error) {
    resolved := make([]string, len(keys))
    for i, k := range keys {
        resolved[i] = resolveCore(k, r.checkLink)
    }
    raw, err := r.rdb.MGet(bg, resolved...).Result() // MGet 只有 .Result()
    if err != nil {
        return nil, err
    }
    result := make([]Value, len(raw))
    for i, v := range raw {
        if v != nil {
            // MGet 返回 interface{}，实际类型是 string
            result[i] = DecodeValue([]byte(v.(string)))
        }
    }
    return result, nil
}

func (r *redisImpl) Set(key string, value Value) error {
    resolved := resolveCore(key, r.checkLink)
    r.maintainIndex(resolved, true)
    return r.rdb.Set(bg, resolved, EncodeValue(value), 0).Err()
}

func (r *redisImpl) Sets(kvs map[string]Value) error {
    if len(kvs) == 0 {
        return nil
    }
    pairs := make([]any, 0, len(kvs)*2)
    for k, v := range kvs {
        resolved := resolveCore(k, r.checkLink)
        r.maintainIndex(resolved, true)
        pairs = append(pairs, resolved, EncodeValue(v))
    }
    return r.rdb.MSet(bg, pairs...).Err()
}

// Watch / Notify 同理：Watch 用 .Bytes() 读取后 DecodeValue；
// Notify 传入 EncodeValue(value) 的 []byte。
```

### 操作兼容矩阵

所有操作按 Redis 命令分为两类：

**A 类（存 value 的命令）** — 需要 Encode/Decode：

| 命令 | 当前读写方式 | 改造后 |
|------|------------|--------|
| `SET` | `Set(key, value)` → `Result()` | `Set(key, []byte(EncodeValue(v)))` → `.Bytes()` → `DecodeValue` |
| `GET` | `Get(key).Result()` → string | `Get(key).Bytes()` → DecodeValue |
| `MGET` | `MGet(keys...).Result()` → []interface{} | `.Result()` → `[]byte(v.(string))` → DecodeValue |
| `MSET` | `MSet(pairs...)` | pairs 中 value 传入 `[]byte(EncodeValue(v))` |
| `BLPOP` | `BLPop(key).Result()` → []string | `.Result()` → `[]byte(vals[i])` → DecodeValue |
| `LPUSH` | `LPush(key, value)` | `LPush(key, []byte(EncodeValue(v)))` |

**B 类（不存 value 的命令）** — 无需改动：

| 命令 | 用途 | 说明 |
|------|------|------|
| `DEL` | 删 key | 不涉及 value 读写 |
| `SADD/SREM/SMEMBERS` | 目录索引 | key 名和成员名是路径字符串，与 value 编码无关 |

### 迁移路径

```
阶段 1（当前）          阶段 2（并行）               阶段 3（收敛）
─────────────────  →  ─────────────────────  →  ─────────────────
Get → string          Get → string (deprecated)   Get → Value
Set(key, any)         GetTyped → Value            Set(key, Value)
                       Set(key, any) (deprecated)
                       SetTyped(key, Value)
```

**阶段 2 实现策略**：
1. `KVSpace` 接口新增 `GetTyped`/`SetTyped`（或直接重命名接口，让旧调用方编译报错来显式迁移）
2. `redisImpl` 同时实现新旧两套方法，`SetTyped` 写入编码后的 `[]byte`，`Set` 写入 string（旧行为）
3. `GetTyped` 读回 `[]byte` → `DecodeValue`，`Get` 读回 string（旧行为）
4. 逐步将 `cmd/kvlang/*.go`、`internal/vthread/*.go` 等调用方从 `Set(key, "...")` 迁移到 `SetTyped(key, kvspace.Str("..."))`
5. 全部迁移完成后，删除旧接口方法，`GetTyped`/`SetTyped` 重命名为 `Get`/`Set`

**Redis 数据兼容**：旧版纯字符串 key 被 `DecodeValue` 以 fallback 方式读取（当作 `kind: "str"`），无需数据迁移。新版 typed value 被旧版 reader 读到会看到 `\x03int\x2a...` 前缀乱码——旧版本来就不区分 int/str，读到乱码不影响破坏性（它本就不理解类型）。

### 类型常量与 vtype 注册的对齐

为避免 kind_name 字符串分散在代码各处，在 `internal/kvspace` 包中定义与 `vtype` 对齐的内建常量：

```go
// internal/kvspace/value.go

// 内建标量 kind — 与 vtype 命名空间对齐。
// 这些是 kvlang 编译器/运行时直接支持的标量类型。
const (
    KindInt    = "int"
    KindFloat  = "float"
    KindBool   = "bool"
    KindStr    = "str"
    KindBytes  = "bytes"
    KindTensor = "tensor"    // 复合类型，由 tensor vtype 注册
)

// 确保内建类型在 vtype 中注册（init 时由对应 vtype 包完成）。
// 第三方 vtype 通过 vtype.Register() 注册后，其 Name() 自动成为合法 Value.kind。
```

### 性能考量

| 操作 | 额外开销 | 说明 |
|------|---------|------|
| `EncodeValue` | 1 次 `make([]byte, 1+len(kind)+len(raw))` + 2 次 `copy` | O(kind+raw)，纳秒级 |
| `DecodeValue` | 1 次 `data[0]` 索引 + `isValidKind` 检查 | O(1)，kind 名 ≤ 10 字节 |
| `Set` (写路径) | EncodeValue + `[]byte` 传入 go-redis | 比原 `fmt.Sprint(any)` 更快（无反射/fmt 开销） |
| `Get` (读路径) | `.Bytes()` 替代 `.Result()` + DecodeValue | `.Bytes()` 少一次 `string([]byte)` 分配，整体更优 |

### 总结

核心改动只有三件事：

1. **Value 是自描述的** — `[kind_len][kind][raw]` 编码，类型信息嵌入 value 自身
2. **Redis 边界用 `[]byte`** — 不再依赖 go-redis 的 `any → fmt.Sprint` 隐式转换，显式传入/取出 `[]byte`
3. **接口签名为 `Value`** — `Get→Value` / `Set(key, Value)` 从签名层面强制类型化，不再接受裸 `any`/`string`
