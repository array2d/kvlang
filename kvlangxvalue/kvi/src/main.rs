mod store;
mod tui;

use crate::store::{Store, HEAD_LENGTH, MISSING, OK};
use std::env;
use std::process::exit;

const EXIT_OK: i32 = 0;
const EXIT_FAILURE: i32 = 1;
const EXIT_USAGE: i32 = 2;
const EXIT_MISSING: i32 = 3;

fn main() {
    let arguments: Vec<String> = env::args().skip(1).collect();
    if arguments.is_empty() {
        run_tui();
    }
    match arguments[0].as_str() {
        "help" | "--help" | "-h" => {
            print_help();
            exit(EXIT_OK);
        }
        _ => {}
    }
    let data_source_name = match env::var("KVSPACE") {
        Ok(value) if !value.is_empty() => value,
        _ => {
            eprintln!(
                "未设置 KVSPACE。本工具只接受 shm://，例如：\n  KVSPACE=shm:///tmp/kvspace.shm kvi list /"
            );
            exit(EXIT_USAGE);
        }
    };
    let store = match Store::connect(&data_source_name) {
        Some(store) => store,
        None => {
            eprintln!(
                "connect failed: {data_source_name}\n  只接受 shm://（例如 shm:///tmp/kvspace.shm）"
            );
            exit(EXIT_USAGE);
        }
    };
    let status = dispatch(&store, &arguments);
    if status == EXIT_USAGE {
        print_help();
    }
    exit(status);
}

fn run_tui() -> ! {
    let data_source_name = match env::var("KVSPACE") {
        Ok(value) if !value.is_empty() => value,
        _ => {
            eprintln!(
                "未设置 KVSPACE。本工具只接受 shm://，例如：\n  KVSPACE=shm:///tmp/kvspace.shm kvi"
            );
            exit(EXIT_USAGE);
        }
    };
    let store = match Store::connect(&data_source_name) {
        Some(store) => store,
        None => {
            eprintln!("connect failed: {data_source_name}");
            exit(EXIT_USAGE);
        }
    };
    exit(tui::run(&store, &data_source_name));
}

fn dispatch(store: &Store, arguments: &[String]) -> i32 {
    let command = arguments[0].as_str();
    let rest = &arguments[1..];
    match command {
        "newT" => command_new_typed(store, rest),
        "writeT" => command_write_typed(store, rest),
        "readT" => command_read_typed(store, rest),
        "print" => command_print(store, rest),
        "head" => command_head(store, rest),
        "attr" => command_attr(store, rest),
        "list" => command_list(store, rest),
        "del" => command_delete(store, rest),
        "cp" => command_copy(store, rest),
        _ => EXIT_USAGE,
    }
}

fn command_new_typed(store: &Store, arguments: &[String]) -> i32 {
    if arguments.len() != 3 {
        return EXIT_USAGE;
    }
    match store.write_text(&arguments[0], &arguments[1], &arguments[2]) {
        Ok(()) => EXIT_OK,
        Err(message) => {
            eprintln!("{message}");
            EXIT_FAILURE
        }
    }
}

fn command_write_typed(store: &Store, arguments: &[String]) -> i32 {
    if arguments.len() != 2 {
        return EXIT_USAGE;
    }
    let key = &arguments[0];
    let wire = match store.resolve(key) {
        Ok(Some(wire)) => wire,
        Ok(None) => {
            eprintln!("key 不存在，没有类型可依");
            return EXIT_FAILURE;
        }
        Err(status) => {
            eprintln!("resolve failed: {status}");
            return EXIT_FAILURE;
        }
    };
    let langtype = match store.langtype(&wire) {
        Some(langtype) => String::from_utf8_lossy(&langtype).into_owned(),
        None => {
            eprintln!("key 没有 langtype 记录，没有类型可依");
            return EXIT_FAILURE;
        }
    };
    match store.write_text(key, &langtype, &arguments[1]) {
        Ok(()) => EXIT_OK,
        Err(message) => {
            eprintln!("{message}");
            EXIT_FAILURE
        }
    }
}

fn command_read_typed(store: &Store, arguments: &[String]) -> i32 {
    if arguments.len() != 1 {
        return EXIT_USAGE;
    }
    match store.read_text(&arguments[0]) {
        Ok(Some((langtype, text))) => {
            println!("{langtype}\t{text}");
            EXIT_OK
        }
        Ok(None) => EXIT_MISSING,
        Err(message) => {
            eprintln!("{message}");
            EXIT_FAILURE
        }
    }
}

fn command_print(store: &Store, arguments: &[String]) -> i32 {
    if arguments.len() != 1 {
        return EXIT_USAGE;
    }
    let wire = match store.resolve(&arguments[0]) {
        Ok(Some(wire)) => wire,
        Ok(None) => return EXIT_MISSING,
        Err(status) => {
            eprintln!("resolve failed: {status}");
            return EXIT_FAILURE;
        }
    };
    match store.langtype(&wire) {
        Some(langtype) => println!("langtype={}", String::from_utf8_lossy(&langtype)),
        None => println!("langtype=(none)"),
    }
    let body = Store::body(&wire);
    println!("bodylen={}", body.len());
    if body_is_text(body) {
        print!("{}", String::from_utf8_lossy(body));
        if body.last() != Some(&b'\n') {
            println!();
        }
        return EXIT_OK;
    }
    for (offset, chunk) in body.chunks(16).enumerate() {
        print!("{:08x} ", offset * 16);
        for index in 0..16 {
            match chunk.get(index) {
                Some(byte) => print!("{byte:02x} "),
                None => print!("   "),
            }
        }
        print!(" |");
        for byte in chunk {
            print!(
                "{}",
                if (0x20..=0x7e).contains(byte) {
                    *byte as char
                } else {
                    '.'
                }
            );
        }
        println!("|");
    }
    EXIT_OK
}

fn command_head(store: &Store, arguments: &[String]) -> i32 {
    if arguments.len() != 1 {
        return EXIT_USAGE;
    }
    let wire = match store.resolve(&arguments[0]) {
        Ok(Some(wire)) => wire,
        Ok(None) => return EXIT_MISSING,
        Err(status) => {
            eprintln!("resolve failed: {status}");
            return EXIT_FAILURE;
        }
    };
    let (body_length, attribute_length) = Store::head_fields(&wire);
    let padding_is_zero = wire[10..16].iter().all(|byte| *byte == 0);
    println!(
        "bodylen={body_length}\tattrslen={attribute_length}\ttotal={}\tpadding={}",
        wire.len(),
        if padding_is_zero { "zero" } else { "NONZERO" }
    );
    EXIT_OK
}

fn command_attr(store: &Store, arguments: &[String]) -> i32 {
    if arguments.len() != 1 {
        return EXIT_USAGE;
    }
    let wire = match store.resolve(&arguments[0]) {
        Ok(Some(wire)) => wire,
        Ok(None) => return EXIT_MISSING,
        Err(status) => {
            eprintln!("resolve failed: {status}");
            return EXIT_FAILURE;
        }
    };
    let (body_length, attribute_length) = Store::head_fields(&wire);
    let mut cursor = HEAD_LENGTH + body_length as usize;
    let end = cursor + attribute_length as usize;
    if cursor >= end {
        println!("(no attr records)");
        return EXIT_OK;
    }
    while cursor + 3 <= end {
        let tag = wire[cursor];
        let length = u16::from_le_bytes([wire[cursor + 1], wire[cursor + 2]]) as usize;
        if cursor + 3 + length > end {
            eprintln!("attr 记录越界");
            return EXIT_FAILURE;
        }
        print!("tag={tag}\tlen={length}\tdata=");
        for byte in &wire[cursor + 3..cursor + 3 + length] {
            print!("{byte:02x}");
        }
        println!();
        cursor += 3 + length;
    }
    EXIT_OK
}

fn command_list(store: &Store, arguments: &[String]) -> i32 {
    if arguments.len() != 1 {
        return EXIT_USAGE;
    }
    match store.list(&arguments[0]) {
        Ok(names) => {
            for name in names {
                println!("{name}");
            }
            EXIT_OK
        }
        Err(status) => {
            eprintln!("list failed: {status}");
            EXIT_FAILURE
        }
    }
}

fn command_delete(store: &Store, arguments: &[String]) -> i32 {
    if arguments.len() != 1 {
        return EXIT_USAGE;
    }
    match store.delete(&arguments[0]) {
        MISSING => EXIT_MISSING,
        OK => EXIT_OK,
        status => {
            eprintln!("delete failed: {status}");
            EXIT_FAILURE
        }
    }
}

fn command_copy(store: &Store, arguments: &[String]) -> i32 {
    if arguments.len() != 2 {
        return EXIT_USAGE;
    }
    match store.copy(&arguments[0], &arguments[1]) {
        OK => EXIT_OK,
        status => {
            eprintln!("copy failed: {status}");
            EXIT_FAILURE
        }
    }
}

fn body_is_text(body: &[u8]) -> bool {
    body.iter()
        .all(|byte| *byte == b'\n' || *byte == b'\t' || (0x20..=0x7e).contains(byte))
}

fn print_help() {
    println!("usage: kvi <command> [args]   (存储地址由 env KVSPACE 给出；无参数进入交互界面)");
    println!();
    println!("  {:<34} {}", "newT <key> <langtype> <value>", "按 langtype 构造 XValue 并写入");
    println!("  {:<34} {}", "writeT <key> <value>", "按目标已有 langtype 写入（类型从 attr 取，不猜）");
    println!("  {:<34} {}", "readT <key>", "打印 <langtype>TAB<value>；key 不存在时退出码 3");
    println!("  {:<34} {}", "print <key>", "打印代码值：langtype + body（文本原样，二进制十六进制）");
    println!("  {:<34} {}", "head <key>", "打印原始 head 字段");
    println!("  {:<34} {}", "attr <key>", "逐条打印 attr 记录（tag/len/data）");
    println!("  {:<34} {}", "list <prefix>", "列直接子项（坐标序）");
    println!("  {:<34} {}", "del <key>", "删除键");
    println!("  {:<34} {}", "cp <source> <target>", "复制单键");
    println!();
    println!("exit codes: 0 ok, 1 failure, 2 usage, 3 key not found");
}
