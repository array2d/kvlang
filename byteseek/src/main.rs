//! byteseek —— 功能完整的 runtime 二进制。模式2（runtime 主导 + 就地 rwir stdlib）。
//! CLI（DSN 取环境变量 KVSPACE，缺省 redis://127.0.0.1:6379）。入口须显式给出（不再自动探测）：
//!   byteseek                 无入口 → 提示 need entry
//!   byteseek <entry>         运行已 layout 进 kvspace 的入口（pkg·func 或裸名，pkg 可空）
//!   byteseek <file.kv>       stdlib 先行 → layout 该文件 → 运行约定入口 test
//!   byteseek -c "<src>"      stdlib 先行 → layout 内存源码 → 运行散语句合成入口 init
//!   byteseek layout <file>   仅 layout，打印 ENTRY=<entry>
//!   byteseek vet <file>      仅校验（parse+lower），打印 ok 或错误
//!   byteseek format <file>   格式化输出到 stdout
//!   byteseek printlib <file> [prefix]  layout 该文件后，把 /lib（或 prefix）子树重建为可运行 kvlang + 槽位注释（不读 .src）
//!   byteseek --lib p1:p2:…   layout 各路径下所有 .kv → run 各 lib 的 init（pkg 取自源码 `lib` 声明，同 stdlib）
//!   byteseek <arg...> --no-stdlib  任意位置：跳过 stdlib 的 layout&run 引导（benchmark 隔离用），
//!                                  只 layout 用户文件后直接 run 入口，不加载/运行内置库
//! 驱动循环：executeVthread 主导执行，遇 rwir 停下；就地 rwir（print/json/http/…）
//! 连续批处理 + nextPc；外部 rwir（如 numpy）handoff 给扩展进程；native/控制帧写回 pc。
#![allow(non_snake_case, non_camel_case_types)]

use std::ffi::c_char;
use std::ptr::null_mut;

use byteseek::engine::Engine;
use byteseek::ffi::*;
use byteseek::rwir;

fn dsn() -> String {
    std::env::var("KVSPACE").unwrap_or_else(|_| "redis://127.0.0.1:6379".to_string())
}

/// info 级日志（对齐 C logx.c 的 LOG_LEVEL 门控）：默认 warn 静默，仅 LOG_LEVEL=info|debug 时以 `<exe>: ` 前缀输出到 stderr。
fn log_info(msg: &str) {
    match std::env::var("LOG_LEVEL").as_deref() {
        Ok("info") | Ok("debug") => byteseek::elog!("{msg}"),
        _ => {}
    }
}

fn read_file(path: &str) -> String {
    std::fs::read_to_string(path).unwrap_or_else(|e| {
        byteseek::elog!("读取 {path} 失败: {e}");
        std::process::exit(1);
    })
}

fn main() {
    // #493：先从内存加载嵌入的 .so 并绑定符号（必须在任何 connect 之前）。
    #[cfg(feature = "embed")]
    byteseek::embed::init();

    let args: Vec<String> = std::env::args().skip(1).collect();
    let dsn = dsn();

    // `--no-stdlib`（任意位置）：跳过 stdlib 的 layout&run 引导（benchmark 隔离用），
    // 仅把用户文件 layout 进 kvspace 后直接 run 入口，不加载/运行任何内置库。
    let no_stdlib = args.iter().any(|a| a == "--no-stdlib");
    let args: Vec<String> = args.into_iter().filter(|a| a != "--no-stdlib").collect();

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
        // ── layout 文件后 printlib /lib（或 prefix）子树：审查 layout 结果、调坐标、验 round-trip ──
        [cmd, file] if cmd == "printlib" => printlib_file(file, "/lib", &dsn),
        [cmd, file, prefix] if cmd == "printlib" => printlib_file(file, prefix, &dsn),

        // ── stdlib 先 layout&run → 再 layout 内存源码 → 运行（散语句合成 init）──
        [cmd, src] if cmd == "-c" => {
            let eng = boot(&dsn, no_stdlib);
            layout_code_or_die(src, &dsn);
            drive(&eng, "init");
        }

        // ── stdlib 先 layout&run → 再 layout .kv 文件 → 运行（约定入口 test）──
        [x] if x.ends_with(".kv") && std::path::Path::new(x).is_file() => {
            let eng = boot(&dsn, no_stdlib);
            layout_file_or_die(x, &dsn);
            drive(&eng, "test");
        }

        // ── isa publish：发布 core manifest（connect）并注册 byteseek 的 def rwir，
        //    使 layout CLI 无需完整 boot 即可独立工作。──
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

        // ── vthread 两步分离：create 只创建（bootstrap）并把 vid 打到 stdout，不运行 ──
        [cmd, funcname] if cmd == "create" => {
            let eng = boot(&dsn, no_stdlib);
            let vid = take(unsafe {
                kvlangRuntimeBootstrap(eng.rt, cs(funcname).as_ptr(), null_mut(), 0)
            });
            if vid.is_empty() {
                byteseek::elog!("create {funcname} 失败");
                std::process::exit(1);
            }
            println!("{vid}"); // vid 是命令结果 → stdout（区别于运行诊断日志）
        }
        // ── run <vid>：以 vid 为参数从持久化 pc 跑到结束（也即崩溃恢复：续跑已有 vthread）──
        [cmd, vid] if cmd == "run" => drive_vid(&boot(&dsn, no_stdlib), vid),

        // ── 运行已入库的显式入口（tutorial 测试主路径：pkg·func 或裸名，pkg 可空）──
        [x] => drive(&boot(&dsn, no_stdlib), x),
        // ── --lib p1:p2:… → layout 各路径下所有 .kv（相对名=pkg），复用 stdlib 的 run init 引导──
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
        // 无参：直接报错（没有任何“隐式库路径”环境变量）
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

// layout <file> 进 kvspace 后 printlib `prefix` 子树到 stdout（审查 layout 结果用，不读 .src）。
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

/// `--lib`（: 分隔路径）→ (名, 源码)：文件取 basename，目录递归、相对根、排序（确定性）。
/// 名仅作错误显示；pkg 与 init 由 layout 按源码 `lib` 声明产出，不从此名/路径推导。
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

/// 连接 runtime+kvspace，注册就地 rwir，layout stdlib 并 run 其 init（一次性引导）。
/// 先于用户代码 layout —— stdlib 常量（如 /lib/math·Pi）在此落值，用户代码随后可读。
fn boot(dsn: &str, no_stdlib: bool) -> Engine {
    let rt = unsafe { kvlangRuntimeConnect(cs(dsn).as_ptr()) };
    if rt.is_null() {
        byteseek::elog!("kvlangRuntimeConnect 失败: {dsn}");
        std::process::exit(1);
    }
    // 复用 rt 内部 kvspace 句柄，绝不另开连接——durable 惰性 flush 只在同句柄内相干，
    // 跨句柄写不可见（曾致 fs/redis 外部 rwir 后 pc 陈旧、整段重跑；shm 共享 mmap 掩盖）。
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

/// 主导驱动 funcname 的 vthread 到结束（就地批处理纯净 rwir，外部 rwir handoff）。
fn drive(eng: &Engine, funcname: &str) {
    let vid = take(unsafe { kvlangRuntimeBootstrap(eng.rt, cs(funcname).as_ptr(), null_mut(), 0) });
    if vid.is_empty() {
        byteseek::elog!("bootstrap {funcname} 失败");
        std::process::exit(1);
    }
    log_info(&format!("vthread {vid}")); // 供崩溃恢复：LOG_LEVEL=info 时暴露 vid，可 `byteseek run <vid>`
    drive_vid(eng, &vid);
}

/// 从 kvspace 里 vid 的持久化 pc 续跑到结束（崩溃恢复入口，与 drive 共用循环，不 bootstrap）。
fn drive_vid(eng: &Engine, vid: &str) {
    let (rt, kv) = (eng.rt, eng.kv);
    let mut guard_pc: Option<String> = None;
    let mut guard_st = String::new();
    let mut guard_n = 0u32;
    let mut guard_ran = false;

    loop {
        let mut pc: *mut c_char = null_mut();
        let rc = unsafe { kvlangRuntimeExecuteVthread(rt, cs(vid).as_ptr(), &mut pc) };
        if rc == 0 {
            break; // vthread done
        }
        if rc != 1 {
            let st = eng.get_kv(&format!("/vthread/{vid}/\u{2025}status"));
            let msg = eng.get_kv(&format!("/vthread/{vid}/\u{2025}msg"));
            let st = if st.is_empty() { "error".into() } else { st };
            byteseek::elog!("vthread {vid} rc={rc} {st}: {msg}");
            std::process::exit(1);
        }

        let st_before = eng.get_kv(&format!("/vthread/{vid}/\u{2025}status"));
        let c0 = take(pc);

        // 无进展闸门：runtime 回到同一 pc、期间没派发任何就地 op、状态也没变 → 空转。
        let same = guard_pc.as_deref() == Some(c0.as_str()) && !guard_ran
                   && guard_st == st_before;
        guard_n = if same { guard_n + 1 } else { 0 };
        guard_pc = Some(c0.clone());
        guard_st = st_before;
        guard_ran = false;
        if guard_n >= 3 {
            let op_at = take(unsafe { kvlangRwirextParams(kv, cs(&c0).as_ptr()) });
            let op_at = op_at.lines().next().unwrap_or("").to_string();
            let msg = eng.get_kv(&format!("/vthread/{vid}/\u{2025}msg"));
            byteseek::elog!(
                "op '{op_at}' not implemented by any runtime (Handoff): pc 连续 \
                 3 次未推进 @ {c0} status={guard_st} {msg}"
            );
            std::process::exit(1);
        }

        // 连续批处理就地 rwir（print/json/http/kvlayout/input），遇非就地指令停下。
        let mut c = c0.clone();
        let stop_op = loop {
            let params = take(unsafe { kvlangRwirextParams(kv, cs(&c).as_ptr()) });
            let op = params.split('\n').next().unwrap_or("").to_string();
            if !rwir::is_inproc(&op) {
                break op;
            }
            rwir::dispatch(eng, &op, &c);
            guard_ran = true;
            let nxt = take(unsafe { kvlangRwirextNextPc(cs(&c).as_ptr()) });
            // 就地派发可能跳到子 vthread：接下来停在的 pc 属谁，就推进谁的‥pc。
            let nsub = nxt.split('/').nth(2).unwrap_or(vid);
            eng.set_sys_text(&format!("/vthread/{nsub}/\u{2025}pc"), &nxt);
            c = nxt;
        };

        // pc 可能属子 vthread（native vthread·run 冒泡上来）：目标 vid 一律由 pc 导出，非固定主 vid。
        let sub = c.split('/').nth(2).unwrap_or(vid);

        // 路由：本 myrwircaps 或已登记扩展（/lib/<op>/vids）→ handoff；
        // 其余写回 pc 交给 runtime 自己执行（native/控制帧/帧结束都走这条）。
        // 无人认领的 op 由上面的"无进展"闸门在 3 次空转内硬错，不无限空转。
        let claimed = !stop_op.is_empty()
            && (rwir::in_myrwircaps(&stop_op)
                || eng.kv_exists(&format!("/lib/{stop_op}/vids")));
        if claimed {
            if unsafe { kvlangRwirextHandoff(kv, cs(sub).as_ptr(), cs(&c).as_ptr()) } != 0 {
                byteseek::elog!("handoff {stop_op} 失败 @ {c}");
                std::process::exit(1);
            }
        } else {
            eng.set_sys_text(&format!("/vthread/{sub}/\u{2025}pc"), &c);
        }
    }
    // kv 属 rt（kvlangRuntimeKvspaceHandle），进程退出时随 rt 回收，此处不 close。
}
