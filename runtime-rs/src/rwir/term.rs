//! rwir `print` / `println` / `cerr` / `input`。它们不是 kvlang runtime 的 builtin，
//! 由本 runtime 就地实现：print* 拼行输出；`input` 读一行 stdin 回填写槽。
//! TTY 走 rustyline（方向键移动光标 / 上下翻历史 / 按字符退格）；管道走 read_line（脚本/测试）。

use std::cell::RefCell;
use std::io::Write;

use crate::engine::Engine;
use crate::ffi::*;

pub fn print_line(eng: &Engine, pc: &str) {
    let params = take(unsafe { kvlang_rwirextParams(eng.kv, cs(pc).as_ptr()) });
    let mut it = params.split('\n');
    let opcode = it.next().unwrap_or("");
    let (sep, rawnl, cerr) = match opcode {
        "print" => ("", 1, 0),
        "println" => (" ", 0, 0),
        "cerr" => (" ", 0, 1),
        _ => return,
    };
    let mut line = String::new();
    for (i, _) in it.enumerate() {
        if i > 0 {
            line.push_str(sep);
        }
        let d = take(unsafe { kvlang_rwirextResolveRead(eng.kv, cs(pc).as_ptr(), i as i32) });
        line.push_str(&d);
    }
    if cerr != 0 {
        eprint!("{line}");
        if rawnl == 0 {
            eprintln!();
        }
        std::io::stderr().flush().ok();
    } else {
        print!("{line}");
        if rawnl == 0 {
            println!();
        }
        std::io::stdout().flush().ok();
    }
}

thread_local! {
    static EDITOR: RefCell<rustyline::DefaultEditor> =
        RefCell::new(rustyline::DefaultEditor::new().expect("初始化 line editor 失败"));
}

/// input(prompt) -> line：读一行 stdin → 回填写槽。
/// Ctrl-C / Ctrl-D 视作 "exit"，让主循环优雅退出。
pub fn input(eng: &Engine, pc: &str) {
    let prompt = eng.read0(pc);
    let line = if unsafe { libc::isatty(libc::STDIN_FILENO) } != 0 {
        rl_readline(&prompt)
    } else {
        pipe_readline(&prompt)
    };
    eng.set_kv(&eng.write0(pc), &line);
}

/// TTY：rustyline 行编辑（方向键 / 历史 / 多字节退格）。
fn rl_readline(prompt: &str) -> String {
    EDITOR.with(|e| {
        let mut ed = e.borrow_mut();
        match ed.readline(prompt) {
            Ok(line) => {
                if !line.trim().is_empty() {
                    let _ = ed.add_history_entry(line.as_str());
                }
                line
            }
            Err(rustyline::error::ReadlineError::Interrupted) => "exit".into(),
            Err(rustyline::error::ReadlineError::Eof) => "exit".into(),
            Err(err) => {
                eprintln!("输入错误: {err}");
                "exit".into()
            }
        }
    })
}

/// 管道：纯 read_line（脚本/测试，无 TTY 行编辑）。
fn pipe_readline(prompt: &str) -> String {
    print!("{prompt}");
    std::io::stdout().flush().ok();
    let mut line = String::new();
    let n = std::io::stdin().read_line(&mut line).unwrap_or(0);
    if n == 0 {
        "exit".to_string()
    } else {
        line.trim_end_matches(['\n', '\r']).to_string()
    }
}
