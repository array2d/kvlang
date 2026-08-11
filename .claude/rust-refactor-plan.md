# Rust Runtime 重构计划：像素级对齐 Go kvlang runtime

## 目标

将当前 `cmd/kvlang/main.rs`（238行，含全部逻辑）拆解为与 Go 一一对应的 Rust 模块。

## Go → Rust 模块映射

| Go 文件 | Rust 文件 | 内容 |
|---------|----------|------|
| `kvcpu/cpu.go` (27行) | `kvcpu/cpu.rs` | Cpu trait + KVCpu struct (kvspace FFI handle) |
| `kvcpu/execute.go` (241行) | `kvcpu/execute.rs` | Execute loop: PC→decode→dispatch→advance |
| `kvcpu/controlflow.go` (84行) | `kvcpu/controlflow.rs` | handleCall/handleReturn/handleBr/handleGoto |
| `rwir/rwir.go` (105行) | `rwir/rwir.rs` | Rwir struct, Decode(), ExtractAddr0(), NextPC() |
| `rwir/builtin/arith.go` (122行) | `rwir/builtin/arith.rs` | add/sub/mul/div/mod + evalBinaryArith |
| `rwir/builtin/cmp.go` (60行) | `rwir/builtin/cmp.rs` | eq/neq/lt/gt/le/ge |
| `rwir/builtin/io.go` (52行) | `rwir/builtin/io.rs` | print/println/display |
| `rwir/builtin/time.go` (152行) | `rwir/builtin/time.rs` | time.now/time.sub/time.duration.* |
| `rwir/builtin/mod.rs` | `rwir/builtin/mod.rs` | Op trait + dispatch table (registerWord) |
| `cmd/kvlang/main.rs` | `cmd/kvlang/main.rs` | thin: open SHM → kvlang_bootstrap → execute("main") |

## 不移植的部分（Go only，Rust runtime 不需要）

- `keytree/` — 路径生成（Go layout 已写入 kvspace，runtime 只需读取现有路径）
- `vthread/` — vthread 管理（Rust runtime 用简化版：直接用 slot-based 执行）
- `layout/`, `parser/`, `lower/`, `ast/`, `symbol/` — 编译器前端
- `rwir/builtin/call.go`, `cast.go`, `coerce.go`, `array.go` 等复杂 builtin — v2

## 执行模型（简化 vthread）

Go 的完整 vthread 模型需要 Bootstrap + ExtIndex + 完整帧管理。Rust runtime v1 用简化模型：

```
1. 打开 SHM，读 /lib/<funcname> 目录
2. 循环 slot=0,1,2...:
   a. 读 /lib/<func>/[slot, 0] → opcode
   b. 读 /lib/<func>/[slot, -i] → read params
   c. 读 /lib/<func>/[slot, i] → write slots  
   d. 本地 HashMap 存变量 (kind, raw bytes)
   e. dispatch(opcode, reads, writes, vars)
   f. slot++
3. 直到 [slot, 0] 不存在 → 结束
```

## 构建验证

```bash
cargo build --manifest-path kvlang/Cargo.toml  # 0 errors
cargo run — 对比 Go goheap:// 输出逐字节一致
```

## 当前 lib.rs 模块树

```
lib.rs
├── kvcpu (cpu.rs, execute.rs, controlflow.rs, sched.rs, debug.rs)
└── rwir (mod.rs, rwir.rs, builtin/mod.rs, builtin/io.rs)
```

需要添加: rwir/builtin/arith.rs, rwir/builtin/cmp.rs, rwir/builtin/time.rs

## 每个模块的接口契约

### kvcpu/cpu.rs
```rust
pub struct KVCpu { kv: *mut c_void, vm_id: String }
impl KVCpu {
    pub fn open(shm_path: &str) -> Option<*mut c_void>
    pub fn new(kv: *mut c_void, vm_id: &str) -> Self
    pub fn close(kv: *mut c_void)
    pub fn get(&self, key: &str) -> Option<(String, Vec<u8>)>  // (kind, raw)
    pub fn set(&self, key: &str, kind: &str, raw: &[u8])
}
```

### rwir/rwir.rs
```rust
pub struct Param { pub name: String, pub val: (String, Vec<u8>) } // (kind, raw)
pub struct Rwir { pub opcode: String, pub reads: Vec<Param>, pub writes: Vec<Param> }
pub fn decode(cpu: &KVCpu, func_base: &str, slot: i32) -> Option<Rwir>
pub fn next_pc(pc: &str) -> String
```

### rwir/builtin/mod.rs
```rust
pub trait BuiltinOp {
    fn call(&self, cpu: &KVCpu, reads: &[Param], writes: &[Param], vars: &mut HashMap<String, (String, Vec<u8>)>);
}
fn dispatch(opcode: &str) -> Option<&dyn BuiltinOp>
```

### kvcpu/execute.rs
```rust
pub fn execute(cpu: &KVCpu, func_name: &str) -> Result<(), String>
```

### cmd/kvlang/main.rs
```rust
fn main() {
    let kv = KV::open("/tmp/kv_shm_test").expect("open");
    // bootstrap vthread for "main"
    execute::execute(&kv, "main").expect("execute");
}
```

## 实现顺序

1. `kvcpu/cpu.rs` — KVCpu + FFI bindings (already mostly done)
2. `rwir/rwir.rs` — Rwir + Decode (from existing cmd/main.rs logic)
3. `rwir/builtin/mod.rs` + `io.rs` + `arith.rs` + `cmp.rs` + `time.rs`
4. `kvcpu/execute.rs` — dispatch loop
5. `cmd/kvlang/main.rs` — thin entry
6. Remove all logic from cmd/kvlang/main.rs
7. Build + test against hello.kv SHM
