#![allow(non_snake_case)]
//! kvlang-layout — 编译期：parse → lower → layoutcode。
//! 只依赖 kvspace-durable 的 C ABI（见 [`ffi`]），不依赖其 Rust 类型。
//! 翻译自 kvlang 的 Go 源码：parser/ lower/ layout/ ast/ symbol/ keytree/。

pub mod ast;
pub mod builtin;
pub mod capi;
pub mod code;
pub mod ffi;
pub mod keytree;
pub mod kindexpr;
pub mod kvkind;
pub mod lower;
pub mod parser;
pub mod scanner;
pub mod symbol;

pub use code::{compile, dump, format, init_dirs, vet, write_func, write_rwir_decl};
pub use ffi::Kv;
pub use scanner::Diagnostic;
