// kvlang Rust CLI — entry point for "run" command.
// This is the Rust counterpart to cmd/kvlang/main.go (Go toolchain CLI)
// and cmd/kvlang/main.cpp (C++ runtime CLI).

use std::env;

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        eprintln!("usage: kvlang run <file.kv>");
        std::process::exit(1);
    }

    match args[1].as_str() {
        "run" => {
            println!("[rust] run: not yet implemented");
        }
        cmd => {
            eprintln!("unknown command: {cmd}");
            std::process::exit(1);
        }
    }
}
