
#![allow(non_snake_case, non_camel_case_types)]

use std::ffi::c_char;
use std::ptr::null_mut;

use byteseek::engine::Engine;
use byteseek::ffi::*;

fn dsn() -> String {
    match std::env::var("KVSPACE") {
        Ok(v) if !v.is_empty() => v,
        _ => {
            byteseek::elog!("缺少 KVSPACE：DSN 必须显式给出（无默认值）。");
            byteseek::elog!("例：KVSPACE=shm:///tmp/kv.shm byteseek -c 'print(\"ok\")'");
            std::process::exit(2);
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
enum LogLevel {
    Warn,
    Info,
    Debug,
}

static LOG_LEVEL: std::sync::OnceLock<LogLevel> = std::sync::OnceLock::new();

fn parse_log_level(args: &[String]) -> LogLevel {
    let mut level = LogLevel::Warn;
    for a in args {
        if let Some(v) = a.strip_prefix("--log-level=") {
            level = match v {
                "warn" => LogLevel::Warn,
                "info" => LogLevel::Info,
                "debug" => LogLevel::Debug,
                _ => {
                    byteseek::elog!("--log-level 只接受 warn|info|debug，收到 {v:?}");
                    std::process::exit(1);
                }
            };
        }
    }
    level
}

fn log_info(msg: &str) {
    if LOG_LEVEL.get().copied().unwrap_or(LogLevel::Warn) >= LogLevel::Info {
        byteseek::elog!("{msg}");
    }
}

fn read_file(path: &str) -> String {
    std::fs::read_to_string(path).unwrap_or_else(|e| {
        byteseek::elog!("读取 {path} 失败: {e}");
        std::process::exit(1);
    })
}

fn main() {
    
    #[cfg(feature = "embed")]
    byteseek::embed::init();

    let args: Vec<String> = std::env::args().skip(1).collect();
    let dsn = dsn();

    let no_stdlib = args.iter().any(|a| a == "--no-stdlib");
    
    let _ = LOG_LEVEL.set(parse_log_level(&args));
    let args: Vec<String> = args
        .into_iter()
        .filter(|a| a != "--no-stdlib" && !a.starts_with("--log-level="))
        .collect();

    match args.as_slice() {
        
        [cmd, file] if cmd == "layout" => {
            let mut entry = [0u8; 4096];
            let mut err = [0u8; 4096];
            let rc = unsafe {
                kvlangLayoutFile(
                    cs(file).as_ptr(),
                    cs(&dsn).as_ptr(),
                    entry.as_mut_ptr() as *mut c_char,
                    entry.len() as u32,
                    err.as_mut_ptr() as *mut c_char,
                    err.len() as u32,
                )
            };
            if rc != 0 {
                byteseek::elog!("layout 失败: {}", cbuf(&err));
                std::process::exit(1);
            }
            println!("ENTRY={}", cbuf(&entry));
        }
        [cmd, file] if cmd == "vet" => {
            let src = read_file(file);
            let mut err = [0u8; 4096];
            let rc = unsafe {
                kvlangLayoutVet(
                    cs(&src).as_ptr(),
                    err.as_mut_ptr() as *mut c_char,
                    err.len() as u32,
                )
            };
            if rc == 0 {
                println!("ok");
            } else {
                eprintln!("{}", cbuf(&err));
                std::process::exit(1);
            }
        }
        [cmd, file] if cmd == "format" => {
            let src = read_file(file);
            let mut out = vec![0u8; src.len() * 4 + 4096];
            let mut err = [0u8; 4096];
            let rc = unsafe {
                kvlangLayoutFormat(
                    cs(&src).as_ptr(),
                    out.as_mut_ptr() as *mut c_char,
                    out.len() as u32,
                    err.as_mut_ptr() as *mut c_char,
                    err.len() as u32,
                )
            };
            if rc != 0 {
                byteseek::elog!("format 失败: {}", cbuf(&err));
                std::process::exit(1);
            }
            print!("{}", cbuf(&out));
        }
        
        [cmd, file] if cmd == "printlib" => printlib_file(file, "/lib", &dsn),
        [cmd, file, prefix] if cmd == "printlib" => printlib_file(file, prefix, &dsn),

        [cmd, src] if cmd == "-c" => {
            let eng = boot(&dsn, no_stdlib);
            layout_code_or_die(src, &dsn);
            drive(&eng, "init");
        }

        [x] if x.ends_with(".kv") && std::path::Path::new(x).is_file() => {
            let eng = boot(&dsn, no_stdlib);
            layout_file_or_die(x, &dsn);
            drive(&eng, "test");
        }

        [cmd, act] if cmd == "isa" && act == "publish" => {
            let rt = unsafe { kvlangRuntimeConnect(cs(&dsn).as_ptr()) };
            if rt.is_null() {
                byteseek::elog!("kvlangRuntimeConnect 失败: {dsn}");
                std::process::exit(1);
            }
            let kv = unsafe { kvlangRuntimeKvspaceHandle(rt) };
            let eng = Engine {
                rt,
                kv,
                dsn: dsn.clone(),
                ext: None,
            };
            eng.register();
            unsafe { kvlangRuntimeDisconnect(rt) };
        }

        [cmd, funcname] if cmd == "create" => {
            let eng = boot(&dsn, no_stdlib);
            let vid = take(unsafe {
                kvlangRuntimeBootstrap(eng.rt, cs(funcname).as_ptr(), null_mut(), 0)
            });
            if vid.is_empty() {
                byteseek::elog!("create {funcname} 失败");
                std::process::exit(1);
            }
            println!("{vid}"); 
        }
        
        [cmd, vid] if cmd == "run" => drive_vid(&boot(&dsn, no_stdlib), vid),

        [x] => drive(&boot(&dsn, no_stdlib), x),
        
        [flag, lib] if flag == "--lib" => {
            if lib.is_empty() {
                byteseek::elog!("need entry");
                std::process::exit(1);
            }
            let eng = boot(&dsn, no_stdlib);
            let libs = collect_libs(lib);
            let inits = eng.layout_libs(&libs);
            eng.run_inits(&inits);
        }
        
        [] => {
            byteseek::elog!("need entry (or --lib p1:p2:…)");
            std::process::exit(1);
        }
        _ => {
            byteseek::elog!("参数不合法");
            std::process::exit(1);
        }
    }
}

fn printlib_file(path: &str, prefix: &str, dsn: &str) {
    layout_file_or_die(path, dsn);
    let src = read_file(path);
    let mut out = vec![0u8; src.len() * 16 + 65536];
    let mut err = [0u8; 4096];
    let rc = unsafe {
        kvlangLayoutPrintlib(
            cs(prefix).as_ptr(),
            cs(dsn).as_ptr(),
            out.as_mut_ptr() as *mut c_char,
            out.len() as u32,
            err.as_mut_ptr() as *mut c_char,
            err.len() as u32,
        )
    };
    if rc != 0 {
        byteseek::elog!("printlib 失败: {}", cbuf(&err));
        std::process::exit(1);
    }
    print!("{}", cbuf(&out));
}

fn layout_code_or_die(src: &str, dsn: &str) {
    let mut err = [0u8; 4096];
    let rc = unsafe {
        kvlangLayoutCode(
            cs(src).as_ptr(),
            cs(dsn).as_ptr(),
            null_mut(),
            0,
            err.as_mut_ptr() as *mut c_char,
            err.len() as u32,
        )
    };
    if rc != 0 {
        byteseek::elog!("layout 失败: {}", cbuf(&err));
        std::process::exit(1);
    }
}

fn collect_libs(spec: &str) -> Vec<(String, String)> {
    let mut out: Vec<(String, String)> = Vec::new();
    for path in spec.split(':').filter(|s| !s.is_empty()) {
        let p = std::path::Path::new(path);
        if p.is_dir() {
            walk_kv(p, p, &mut out);
        } else if path.ends_with(".kv") && p.is_file() {
            let name = p
                .file_name()
                .unwrap_or_default()
                .to_string_lossy()
                .into_owned();
            out.push((name, read_file(path)));
        }
    }
    out.sort_by(|a, b| a.0.cmp(&b.0));
    out
}

fn walk_kv(root: &std::path::Path, dir: &std::path::Path, out: &mut Vec<(String, String)>) {
    let Ok(entries) = std::fs::read_dir(dir) else {
        return;
    };
    for e in entries.filter_map(|e| e.ok()) {
        let path = e.path();
        if path.is_dir() {
            walk_kv(root, &path, out);
        } else if path.extension().map_or(false, |x| x == "kv") {
            let rel = path
                .strip_prefix(root)
                .unwrap_or(&path)
                .to_string_lossy()
                .replace('\\', "/");
            out.push((rel, std::fs::read_to_string(&path).unwrap_or_default()));
        }
    }
}

fn layout_file_or_die(path: &str, dsn: &str) {
    let mut err = [0u8; 4096];
    let rc = unsafe {
        kvlangLayoutFile(
            cs(path).as_ptr(),
            cs(dsn).as_ptr(),
            null_mut(),
            0,
            err.as_mut_ptr() as *mut c_char,
            err.len() as u32,
        )
    };
    if rc != 0 {
        byteseek::elog!("layout 失败: {}", cbuf(&err));
        std::process::exit(1);
    }
}

fn boot(dsn: &str, no_stdlib: bool) -> Engine {
    let rt = unsafe { kvlangRuntimeConnect(cs(dsn).as_ptr()) };
    if rt.is_null() {
        byteseek::elog!("kvlangRuntimeConnect 失败: {dsn}");
        std::process::exit(1);
    }
    
    let kv = unsafe { kvlangRuntimeKvspaceHandle(rt) };
    let eng = Engine {
        rt,
        kv,
        dsn: dsn.to_string(),
        ext: None,
    };
    eng.register();
    if !no_stdlib {
        let inits = eng.layout_stdlib();
        eng.run_inits(&inits);
    }
    eng
}

fn drive(eng: &Engine, funcname: &str) {
    let vid = take(unsafe { kvlangRuntimeBootstrap(eng.rt, cs(funcname).as_ptr(), null_mut(), 0) });
    if vid.is_empty() {
        byteseek::elog!("bootstrap {funcname} 失败");
        std::process::exit(1);
    }
    log_info(&format!("vthread {vid}")); 
    drive_vid(eng, &vid);
}

fn drive_vid(eng: &Engine, vid: &str) {
    if let Err(e) = eng.drive_vid(vid) {
        byteseek::elog!("{e}");
        std::process::exit(1);
    }
    
}
