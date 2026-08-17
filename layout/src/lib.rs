//! kvlang-layout — 编译期：parse → lower → layoutcode。
//! 只依赖 kvspace-durable 的 C ABI（见 [`ffi`]），不依赖其 Rust 类型。
//! 翻译自 kvlang 的 Go 源码：parser/ lower/ layout/ ast/ symbol/ keytree/。

pub mod ffi;
pub mod kvkind;
pub mod keytree;
pub mod symbol;
pub mod ast;
pub mod scanner;
pub mod parser;
pub mod builtin;
pub mod lower;
pub mod code;

pub use code::{compile, init_dirs, write_func, write_rwir_decl};
pub use ffi::Kv;
pub use scanner::Diagnostic;
