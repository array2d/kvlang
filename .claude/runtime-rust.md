# kvlang Rust Runtime 开发原则

## 零、总体原则

**kvlang Rust runtime 是 Go kvlang runtime 的像素级 C ABI 移植。**

Go 负责 toolchain（parse → lower → layout → 写入 kvspace），Rust/C++ 负责 runtime（从 kvspace 读取指令 → kvcpu 执行）。

## 一、零拷贝原则

XValue 的 kind 解析后，body 字节直接 reinterpret_cast，不做任何拷贝或中间类型转换。

```
SHM sbo_data 指针 → read_tlv() 返回直接指针（无 malloc）
  ↓
Rust: *const u8 → *(ptr as *const i64).read_unaligned() → i64
C++:  const uint8_t* → *reinterpret_cast<const int64_t*>(ptr)
```

- 禁止 `to_vec()`, `memcpy`, `copy_from_slice`, `malloc` 在 hot path
- 禁止创建中间 Vec/u8 对象装载 body 字节
- `display()` 做格式化输出时从 raw pointer 直接读，不拷
- 只有 kind 字符串（<16 bytes）允许拷贝

## 二、先读 Go 代码原则

实现任何 runtime 功能前，必须先读 Go 对应文件理解逻辑：

| 功能 | Go 文件 |
|------|--------|
| 执行循环 | `kvcpu/execute.go` |
| 控制流 | `kvcpu/controlflow.go` |
| 算术 | `rwir/builtin/arith.go` |
| 比较 | `rwir/builtin/cmp.go` |
| IO | `rwir/builtin/io.go` |
| 时间 | `rwir/builtin/time.go` |
| 分发 | `rwir/builtin/ops.go` |
| 虚线程 | `vthread/vthread.go` |
| 路径生成 | `keytree/const.go` + `keytree/vthread.go` |

禁止不读 Go 代码直接凭想象写 Rust 实现。

## 三、禁止 hardcode 原则

所有路径常量、kind 字符串、opcode 字符串必须引用模块常量，禁止裸字符串。

- 路径 → `keytree/const.rs`（对齐 `keytree/const.go`）
- kind → 引用 XValue kind 常量
- opcode → `rwir/builtin/ops.rs` dispatch table 中定义

禁止在 execute.rs / controlflow.rs 等文件中出现 `"/lib/"`, `"/vthread/"`, `".pc"`, `"main"` 等裸字符串。

## 四、像素级对齐 Go 原则

Rust 项目的文件路径、文件名、函数名、模块名必须与 Go 源码一对一对应：

```
Go: kvlang/kvcpu/execute.go          → Rust: kvlang/kvcpu/execute.rs
Go: kvlang/kvcpu/controlflow.go      → Rust: kvlang/kvcpu/controlflow.rs
Go: kvlang/kvcpu/cpu.go              → Rust: kvlang/kvcpu/cpu.rs
Go: kvlang/rwir/rwir.go              → Rust: kvlang/rwir/rwir.rs
Go: kvlang/rwir/builtin/arith.go     → Rust: kvlang/rwir/builtin/arith.rs
Go: kvlang/rwir/builtin/cmp.go       → Rust: kvlang/rwir/builtin/cmp.rs
Go: kvlang/rwir/builtin/io.go        → Rust: kvlang/rwir/builtin/io.rs
Go: kvlang/rwir/builtin/time.go      → Rust: kvlang/rwir/builtin/time.rs
Go: kvlang/rwir/builtin/ops.go       → Rust: kvlang/rwir/builtin/ops.rs
Go: kvlang/rwir/builtin/logic.go     → Rust: kvlang/rwir/builtin/logic.rs
Go: kvlang/rwir/builtin/math.go      → Rust: kvlang/rwir/builtin/math.rs
Go: kvlang/rwir/builtin/string.go    → Rust: kvlang/rwir/builtin/string.rs
Go: kvlang/vthread/vthread.go        → Rust: kvlang/vthread/vthread.rs
Go: kvlang/keytree/const.go          → Rust: kvlang/keytree/const.rs
Go: kvlang/logx/logx.go              → Rust: kvlang/logx/logx.rs
```

Go 中的公开函数名在 Rust 中保持一致。例如 Go 的 `handle_goto` → Rust 的 `handle_goto`，Go 的 `exec_add` → Rust 的 `exec_add`。

## 五、模块结构

```rust
// lib.rs
pub mod kvcpu;
pub mod rwir;
pub mod keytree;
pub mod logx;
pub mod vthread;

// kvcpu/mod.rs
pub mod cpu;
pub mod execute;
pub mod controlflow;

// rwir/mod.rs
pub mod rwir;
pub mod builtin;

// rwir/builtin/mod.rs
pub mod ops; pub mod arith; pub mod cmp; pub mod io;
pub mod time; pub mod logic; pub mod math; pub mod bit;
pub mod cast; pub mod string; pub mod kvop;
```

## 六、编译与测试

```bash
# C 构建
make -C kvspace-c/build -j4

# Rust 构建
cargo build --manifest-path kvlang/Cargo.toml

# 单文件测试
KVSPACE_SHM=/tmp/t kvlang-rust main

# 全量测试
python3 kvlang/tutorial/test.py --runtime=rust
```
