
use std::env;
use std::os::raw::{c_char, c_int, c_void};
use std::process::exit;

const KVSPACE_XVALUE_HEAD_LEN: usize = 16;

const KVSPACE_MISSING: c_int = -2;

#[link(name = "kvspace")]
extern "C" {
    fn kvspaceConnect(dsn: *const c_char) -> *mut c_void; 
    fn kvspaceClose(h: *mut c_void); 

    fn kvspaceXvalueInProcessAddress(h: *mut c_void) -> *mut u8; 
    
    fn kvspaceResolveOffset(
        h: *mut c_void,
        key: *const c_char,
        parent_key_anchor: u64,
        out_value_offset: *mut u64,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    fn kvspaceFree(h: *mut c_void, key: *const c_char, err: *mut c_char, err_cap: u32) -> c_int; 
    
    fn kvspaceCp(
        h: *mut c_void,
        old_key: *const c_char,
        new_key: *const c_char,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    
    fn kvspaceMkindex(h: *mut c_void, key: *const c_char, err: *mut c_char, err_cap: u32) -> c_int; 
    fn kvspaceDeltree(h: *mut c_void, key: *const c_char, err: *mut c_char, err_cap: u32) -> c_int; 
    
    fn kvspaceCpdir(
        h: *mut c_void,
        source_key: *const c_char,
        destination_key: *const c_char,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    
    fn kvspaceListlen(
        h: *mut c_void,
        key: *const c_char,
        out_count: *mut i64,
        out_max_name_length: *mut u32,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    
    fn kvspaceList(
        h: *mut c_void,
        key: *const c_char,
        out_names: *mut c_char,
        out_capacity: u32,
        out_length: *mut u32,
        out_count: *mut u32,
        out_generation: *mut u64,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;

}

fn cs(s: &str) -> *const c_char {
    
    std::ffi::CString::new(s).unwrap().into_raw() as *const c_char
}

fn default_dsn() -> String {
    match env::var("KVSPACE") {
        Ok(value) if !value.is_empty() => value,
        _ => fatalf(
            "未设置 KVSPACE。本 CLI 只接受 shm://，例如：\n  \
             KVSPACE=shm:///tmp/kvspace.shm kvspace list /",
        ),
    }
}

fn fatalf(msg: &str) -> ! {
    eprintln!("{}", msg);
    exit(1)
}

fn fail_op(err: &[u8; 256], op: &str) -> ! {
    let n = err.iter().position(|&b| b == 0).unwrap_or(err.len());
    if n > 0 {
        fatalf(&String::from_utf8_lossy(&err[..n]));
    }
    fatalf(&format!("{}: failed（后端未填错误详情）", op));
}

fn data_base(kv: *mut c_void) -> *mut u8 {
    unsafe { kvspaceXvalueInProcessAddress(kv) }
}

fn resolve_view<'a>(kv: *mut c_void, key: &str) -> Option<&'a [u8]> {
    let base = data_base(kv);
    if base.is_null() {
        return None;
    }
    let mut offset: u64 = 0;
    let mut err = [0u8; 256];
    let rc = unsafe {
        kvspaceResolveOffset(
            kv,
            cs(key),
            0,
            &mut offset,
            err.as_mut_ptr() as *mut c_char,
            256,
        )
    };
    if rc != 0 {
        return None;
    }
    let head = unsafe { base.add(offset as usize) };
    
    let raw = unsafe { std::slice::from_raw_parts(head, KVSPACE_XVALUE_HEAD_LEN) };
    let parsed = raw_head(raw)?;
    let total = usize::try_from(parsed.total).ok()?;
    Some(unsafe { std::slice::from_raw_parts(head, total) })
}

struct RawHead {
    body_len: u64,
    attrs_len: u16,
    total: u64,
    padding_zero: bool,
}

fn raw_head(wire: &[u8]) -> Option<RawHead> {
    if wire.len() < KVSPACE_XVALUE_HEAD_LEN {
        return None;
    }
    let body_len = u64::from_le_bytes(wire[0..8].try_into().ok()?);
    let attrs_len = u16::from_le_bytes(wire[8..10].try_into().ok()?);
    let padding_zero = wire[10..16].iter().all(|b| *b == 0);
    let total = KVSPACE_XVALUE_HEAD_LEN as u64 + body_len + attrs_len as u64;
    Some(RawHead { body_len, attrs_len, total, padding_zero })
}

fn raw_attrs(wire: &[u8]) -> Option<Vec<(u8, Vec<u8>)>> {
    let head = raw_head(wire)?;
    let start = KVSPACE_XVALUE_HEAD_LEN + head.body_len as usize;
    let end = start + head.attrs_len as usize;
    if end > wire.len() {
        return None;
    }
    let mut records = Vec::new();
    let mut at = start;
    while at + 3 <= end {
        let tag = wire[at];
        let len = u16::from_le_bytes([wire[at + 1], wire[at + 2]]) as usize;
        if at + 3 + len > end {
            return None;
        }
        records.push((tag, wire[at + 3..at + 3 + len].to_vec()));
        at += 3 + len;
    }
    
    if at != end {
        return None;
    }
    Some(records)
}

fn hex(data: &[u8]) -> String {
    let mut out = String::with_capacity(data.len() * 2);
    for b in data {
        out.push_str(&format!("{:02x}", b));
    }
    out
}

fn get(kv: *mut c_void, key: &str) -> Vec<u8> {
    resolve_view(kv, key).map(|v| v.to_vec()).unwrap_or_default()
}

fn dir_form(prefix: &str) -> String {
    if prefix.ends_with('/') || prefix.ends_with('·') {
        prefix.to_string()
    } else {
        format!("{}/", prefix)
    }
}

fn list_names(kv: *mut c_void, prefix: &str) -> Vec<String> {
    let p = cs(&dir_form(prefix));
    let mut count: i64 = 0;
    let mut max_len: u32 = 0;
    let mut err = [0u8; 256];
    let rc = unsafe {
        kvspaceListlen(
            kv,
            p,
            &mut count,
            &mut max_len,
            err.as_mut_ptr() as *mut c_char,
            256,
        )
    };
    if rc != 0 || count <= 0 {
        return Vec::new();
    }
    
    let cap = (count as usize)
        .saturating_mul(max_len as usize + 1)
        .min(u32::MAX as usize) as u32;
    if cap == 0 {
        return Vec::new();
    }
    let mut buf = vec![0u8; cap as usize];
    let mut length: u32 = 0;
    let mut out_count: u32 = 0;
    let mut generation: u64 = 0;
    let rc = unsafe {
        kvspaceList(
            kv,
            p,
            buf.as_mut_ptr() as *mut c_char,
            cap,
            &mut length,
            &mut out_count,
            &mut generation,
            err.as_mut_ptr() as *mut c_char,
            256,
        )
    };
    if rc != 0 {
        fail_op(&err, "list");
    }
    buf.truncate(length as usize);
    buf.split(|b| *b == 0)
        .filter(|s| !s.is_empty())
        .map(|s| String::from_utf8_lossy(s).into_owned())
        .collect()
}

fn join_path(parent: &str, child: &str) -> String {
    if parent == "/" {
        return format!("/{}", child);
    }
    if parent.ends_with('/') || parent.ends_with('·') {
        return format!("{}{}", parent, child);
    }
    format!("{}/{}", parent, child)
}

fn parse_walk_args(tail: &[String]) -> String {
    let mut prefix = "/".to_string();
    for a in tail {
        if a.starts_with("--") {
            fatalf(&format!("unknown option: {}", a));
        }
        prefix = a.clone();
    }
    prefix
}

fn print_help() {
    println!("usage: kvspace <subcommand> [args]   (存储地址由 env KVSPACE 给出，形如 shm:///path)");
    println!();
    println!("存储子命令（只处理 key 与原始字节，不解释值的内容）:");
    println!("  head <key>...           打印原始 head：bodylen/attrslen/total/padding");
    println!("  attr <key>...           逐条打印 attr 记录：tag/len/data(hex)");
    println!("  del <key>...            删除键");
    println!("  deltree <prefix>        删除整棵子树（含根）");
    println!("  cp <src> <dst>          拷贝单键");
    println!("  cpdir <src> <dst>       递归拷贝子树");
    println!("  mkindex <path>          创建目录（index）");
    println!("  list|ls <prefix>        列子键（后端顺序，不排序）");
    println!("  tree <prefix>           树状遍历（后端顺序，不排序）");
    println!("  clear                   清空 kvspace（等价 deltree /，不可恢复）");
    println!("  help                    print this help");
    println!();
    println!("读写值（get/set 与 kind 解释）在 kvlang 的 CLI：`kvlang get|set|list --kind`。");
    println!("本 CLI 不解析 langtype——那是语言层的事，存储层不认识「类型」。");
    println!();
    println!("存储地址: 只接受 shm://。其它 scheme 在 connect 时就返回 NULL、CLI 立即退出——");
    println!("          不是「连上之后才发现不支持」。没有运行期 dispatch，后端由链接期决定。");
    println!("          没有默认值：未设 KVSPACE 直接报错退出，不会替你落一个存储文件。");
}

fn main() {
    let rest: Vec<String> = env::args().skip(1).collect();
    let dsn = default_dsn();
    if rest.is_empty() {
        eprintln!("usage: kvspace <subcommand> [args]   (存储地址由 env KVSPACE 给出，只接受 shm://)");
        eprintln!("       kvspace help  查看子命令清单");
        exit(1);
    }
    let sub = rest[0].clone();
    if matches!(sub.as_str(), "help" | "--help" | "-h") {
        print_help();
        return; 
    }

    let kv = unsafe { kvspaceConnect(cs(&dsn)) };
    if kv.is_null() {
        
        fatalf(&format!(
            "connect failed: {dsn}\n  \
             本 CLI 只接受 shm://（例如 shm:///tmp/kvspace.shm），其它 scheme 一律 NULL；\n  \
             若已是 shm://，检查该路径可写、父目录存在。"
        ));
    }

    let tail = &rest[1..];
    match sub.as_str() {
        "head" => {
            if tail.is_empty() {
                fatalf("usage: kvspace head <key>...");
            }
            for k in tail {
                let v = get(kv, k);
                if v.is_empty() {
                    println!("{}\t(nil)", k);
                    continue;
                }
                match raw_head(&v) {
                    Some(h) => println!(
                        "{}\tbodylen={}\tattrslen={}\ttotal={}\tpadding={}",
                        k,
                        h.body_len,
                        h.attrs_len,
                        h.total,
                        if h.padding_zero { "zero" } else { "NONZERO" }
                    ),
                    None => println!("{}\t(short: {} bytes, < 16)", k, v.len()),
                }
            }
        }
        "attr" => {
            if tail.is_empty() {
                fatalf("usage: kvspace attr <key>...");
            }
            for k in tail {
                let v = get(kv, k);
                if v.is_empty() {
                    println!("{}\t(nil)", k);
                    continue;
                }
                match raw_attrs(&v) {
                    Some(records) => {
                        if records.is_empty() {
                            println!("{}\t(no attr records)", k);
                        }
                        for (tag, data) in records {
                            println!("{}\ttag={}\tlen={}\tdata={}", k, tag, data.len(), hex(&data));
                        }
                    }
                    None => println!("{}\t(malformed: attr region does not fit)", k),
                }
            }
        }
        "del" => {
            if tail.is_empty() {
                fatalf("usage: kvspace del <key>...");
            }
            
            for k in tail {
                let mut err = [0u8; 256];
                let rc = unsafe { kvspaceFree(kv, cs(k), err.as_mut_ptr() as *mut c_char, 256) };
                if rc != 0 && rc != KVSPACE_MISSING {
                    fail_op(&err, &format!("del {}", k));
                }
            }
        }
        "deltree" => {
            let p = tail
                .first()
                .unwrap_or_else(|| fatalf("usage: kvspace deltree <prefix>"));
            let mut err = [0u8; 256];
            let rc = unsafe { kvspaceDeltree(kv, cs(p), err.as_mut_ptr() as *mut c_char, 256) };
            if rc != 0 {
                fail_op(&err, "deltree");
            }
        }
        "cp" => {
            if tail.len() < 2 {
                fatalf("usage: kvspace cp <src> <dst>");
            }
            let mut err = [0u8; 256];
            let rc = unsafe {
                kvspaceCp(
                    kv,
                    cs(&tail[0]),
                    cs(&tail[1]),
                    err.as_mut_ptr() as *mut c_char,
                    256,
                )
            };
            if rc != 0 {
                fail_op(&err, "cp");
            }
        }
        "cpdir" => {
            if tail.len() < 2 {
                fatalf("usage: kvspace cpdir <src> <dst>");
            }
            let mut err = [0u8; 256];
            let rc = unsafe {
                kvspaceCpdir(
                    kv,
                    cs(&tail[0]),
                    cs(&tail[1]),
                    err.as_mut_ptr() as *mut c_char,
                    256,
                )
            };
            if rc != 0 {
                fail_op(&err, "cpdir");
            }
        }
        "mkindex" => {
            let p = tail
                .first()
                .unwrap_or_else(|| fatalf("usage: kvspace mkindex <path>"));
            if tail.len() > 1 {
                fatalf("mkindex 不再接受 capacity（新 ABI 无该参数）");
            }
            
            let p = dir_form(p);
            let mut err = [0u8; 256];
            let rc = unsafe { kvspaceMkindex(kv, cs(&p), err.as_mut_ptr() as *mut c_char, 256) };
            if rc != 0 {
                fail_op(&err, "mkindex");
            }
        }
        "clear" => {
            
            eprintln!("warning: clear 删除整个 kvspace（等价 deltree /），不可恢复");
            let mut err = [0u8; 256];
            let rc = unsafe { kvspaceDeltree(kv, cs("/"), err.as_mut_ptr() as *mut c_char, 256) };
            if rc != 0 {
                fail_op(&err, "clear");
            }
        }
        "list" | "ls" => {
            let p = parse_walk_args(tail);
            fprint_list(kv, &p);
        }
        "tree" => {
            let p = parse_walk_args(tail);
            println!("{}", p);
            fprint_tree(kv, &p, "");
        }
        other => {
            eprintln!("unknown subcommand: {}", other);
            exit(1);
        }
    }

    unsafe {
        kvspaceClose(kv);
    }
}

fn fprint_list(kv: *mut c_void, prefix: &str) {
    
    for c in list_names(kv, prefix) {
        let full = join_path(prefix, &c);
        match raw_head(&get(kv, &full)) {
            Some(h) => println!("{}\tbodylen={}\tattrslen={}", c, h.body_len, h.attrs_len),
            None => println!("{}", c),
        }
    }
}

fn fprint_tree(kv: *mut c_void, prefix: &str, indent: &str) {
    
    let children = list_names(kv, prefix);
    let n = children.len();
    for (i, c) in children.iter().enumerate() {
        let full = join_path(prefix, c);
        let last = i == n - 1;
        let branch = if last { "└── " } else { "├── " };
        let next_indent = format!("{}{}", indent, if last { "    " } else { "│   " });
        if c.ends_with('/') {
            println!("{}{}{}", indent, branch, c);
            fprint_tree(kv, &full, &next_indent);
        } else {
            match raw_head(&get(kv, &full)) {
                Some(h) => println!(
                    "{}{}{}\tbodylen={}\tattrslen={}",
                    indent, branch, c, h.body_len, h.attrs_len
                ),
                None => println!("{}{}{}", indent, branch, c),
            }
        }
    }
}
