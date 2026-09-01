//! kvlang —— 功能完整的 runtime 二进制。模式2（runtime 主导 + 就地 rwir stdlib）。
//! CLI（DSN 取环境变量 KVSPACE，缺省 redis://127.0.0.1:6379）。入口须显式给出（不再自动探测）：
//!   kvlang                 无入口 → 提示 need entry
//!   kvlang <entry>         运行已 layout 进 kvspace 的入口（pkg·func 或裸名，pkg 可空）
//!   kvlang <file.kv>       stdlib 先行 → layout 该文件 → 运行约定入口 test
//!   kvlang -c "<src>"      stdlib 先行 → layout 内存源码 → 运行散语句合成入口 init
//!   kvlang layout <file>   仅 layout，打印 ENTRY=<entry>
//!   kvlang vet <file>      仅校验（parse+lower），打印 ok 或错误
//!   kvlang format <file>   格式化输出到 stdout
//! 驱动循环：executeVthread 主导执行，遇 rwir 停下；就地 rwir（print/json/http/…）
//! 连续批处理 + nextPc；外部 rwir（如 numpy）handoff 给扩展进程；native/控制帧写回 pc。
#![allow(non_snake_case, non_camel_case_types)]

use std::ffi::c_char;
use std::ptr::null_mut;

use kvlang_rs::engine::Engine;
use kvlang_rs::ffi::*;
use kvlang_rs::rwir;

fn dsn() -> String {
    std::env::var("KVSPACE").unwrap_or_else(|_| "redis://127.0.0.1:6379".to_string())
}

fn read_file(path: &str) -> String {
    std::fs::read_to_string(path).unwrap_or_else(|e| {
        eprintln!("kvlang: 读取 {path} 失败: {e}");
        std::process::exit(1);
    })
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let dsn = dsn();

    match args.as_slice() {
        // ── 仅 layout / 校验 / 格式化（不需 runtime）───────────────────
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
                eprintln!("kvlang: layout 失败: {}", cbuf(&err));
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
                eprintln!("kvlang: format 失败: {}", cbuf(&err));
                std::process::exit(1);
            }
            print!("{}", cbuf(&out));
        }

        // ── stdlib 先 layout&run → 再 layout 内存源码 → 运行（散语句合成 init）──
        [cmd, src] if cmd == "-c" => {
            let eng = boot(&dsn);
            layout_code_or_die(src, &dsn);
            drive(&eng, "init");
        }

        // ── stdlib 先 layout&run → 再 layout .kv 文件 → 运行（约定入口 test）──
        [x] if x.ends_with(".kv") && std::path::Path::new(x).is_file() => {
            let eng = boot(&dsn);
            layout_file_or_die(x, &dsn);
            drive(&eng, "test");
        }

        // ── 运行已入库的显式入口（tutorial 测试主路径：pkg·func 或裸名，pkg 可空）──
        [x] => drive(&boot(&dsn), x),
        [] => {
            eprintln!("kvlang: need entry");
            std::process::exit(1);
        }
        _ => {
            eprintln!("kvlang: 参数不合法");
            std::process::exit(1);
        }
    }
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
        eprintln!("kvlang: layout 失败: {}", cbuf(&err));
        std::process::exit(1);
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
        eprintln!("kvlang: layout 失败: {}", cbuf(&err));
        std::process::exit(1);
    }
}

/// 连接 runtime+kvspace，注册就地 rwir，layout stdlib 并 run 其 init（一次性引导）。
/// 先于用户代码 layout —— stdlib 常量（如 /lib/math·Pi）在此落值，用户代码随后可读。
fn boot(dsn: &str) -> Engine {
    let rt = unsafe { kvlangRuntimeConnect(cs(dsn).as_ptr()) };
    if rt.is_null() {
        eprintln!("kvlang: kvlangRuntimeConnect 失败: {dsn}");
        std::process::exit(1);
    }
    let kv = unsafe { kvspaceConnect(cs(dsn).as_ptr()) };
    if kv.is_null() {
        eprintln!("kvlang: kvspaceConnect 失败: {dsn}");
        std::process::exit(1);
    }
    let eng = Engine {
        rt,
        kv,
        dsn: dsn.to_string(),
        ext: None,
    };
    eng.register();
    eng.layout_stdlib();
    eng.run_stdlib_init();
    eng
}

/// 主导驱动 funcname 的 vthread 到结束（就地批处理纯净 rwir，外部 rwir handoff）。
fn drive(eng: &Engine, funcname: &str) {
    let (rt, kv) = (eng.rt, eng.kv);
    let vid = take(unsafe { kvlangRuntimeBootstrap(rt, cs(funcname).as_ptr(), null_mut(), 0) });
    if vid.is_empty() {
        eprintln!("kvlang: bootstrap {funcname} 失败");
        std::process::exit(1);
    }
    let vpc = format!("/vthread/{vid}/\u{2025}pc");

    loop {
        let mut pc: *mut c_char = null_mut();
        let rc = unsafe { kvlangRuntimeExecuteVthread(rt, cs(&vid).as_ptr(), &mut pc) };
        if rc == 0 {
            break; // vthread done
        }
        if rc != 1 {
            let st = eng.get_kv(&format!("/vthread/{vid}/\u{2025}status"));
            let msg = eng.get_kv(&format!("/vthread/{vid}/\u{2025}error/msg"));
            let st = if st.is_empty() { "error".into() } else { st };
            eprintln!("kvlang: vthread {vid} {st}: {msg}");
            std::process::exit(1);
        }

        // 连续批处理就地 rwir（print/json/http/kvlayout/input），遇非就地指令停下。
        let mut c = take(pc);
        let stop_op = loop {
            let params = take(unsafe { kvlang_rwirextParams(kv, cs(&c).as_ptr()) });
            let op = params.split('\n').next().unwrap_or("").to_string();
            if !rwir::is_inproc(&op) {
                break op;
            }
            rwir::dispatch(eng, &op, &c);
            c = take(unsafe { kvlang_rwirextNextPc(cs(&c).as_ptr()) });
        };

        // 外部扩展 rwir（如 numpy）：handoff；native/控制帧/帧结束：写回 pc 让 runtime 继续。
        if !stop_op.is_empty() && rwir::is_others_rwir(&stop_op) {
            if unsafe { kvlang_rwirextHandoff(kv, cs(&vid).as_ptr(), cs(&c).as_ptr()) } != 0 {
                eprintln!("kvlang: handoff {stop_op} 失败 @ {c}");
                std::process::exit(1);
            }
        } else {
            eng.set_kv(&vpc, &c);
        }
    }

    unsafe { kvspaceClose(kv) };
}
