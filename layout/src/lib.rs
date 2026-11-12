#![allow(non_snake_case)]

pub mod ast;
pub mod builtin;
pub mod capi;
pub mod code;
pub mod crash_resume;
pub mod ffi;
pub mod fusion;
pub mod keytree;
pub mod kvkind;
pub mod langtype;
pub mod lower;
pub mod parser;
pub mod scanner;
pub mod symbol;

pub use code::{
    compile, format, init_dirs, printlib, printstack, vet, vet_with_store, write_func,
    write_rwir_decl,
};
pub use ffi::Kv;
pub use scanner::Diagnostic;
