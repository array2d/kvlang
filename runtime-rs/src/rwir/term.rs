//! rwir `print` / `println` / `cerr` / `printf` / `input`。它们不是 kvlang runtime 的 builtin，
//! 由本 runtime 就地实现：print* 拼行输出；`printf` C 风格格式化串（不自动换行，换行靠 `\n`）；
//! `input` 读一行 stdin 回填写槽。
//! TTY 走 rustyline（方向键移动光标 / 上下翻历史 / 按字符退格）；管道走 read_line（脚本/测试）。

use std::cell::RefCell;
use std::io::Write;

use crate::engine::Engine;
use crate::ffi::*;

pub fn print_line(eng: &Engine, pc: &str) {
    let params = take(unsafe { kvlangRwirextParams(eng.kv, cs(pc).as_ptr()) });
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
        line.push_str(&eng.read_at(pc, i as i32));
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

/// printf(fmt, args...)：C 风格格式化串就地输出（不自动换行，换行靠 fmt 里的 `\n`）。
/// 支持转换 `d i u o x X f F e E g G c s %`、标志 `- 0 + 空格 #`、宽度、`.精度`；
/// 实参经 read_at 取显示串，数值型转换按需 parse 回 i64/f64。
pub fn printf(eng: &Engine, pc: &str) {
    let params = take(unsafe { kvlangRwirextParams(eng.kv, cs(pc).as_ptr()) });
    let nslots = params.split('\n').count().saturating_sub(1);
    if nslots == 0 {
        return;
    }
    let fmt = eng.read_at(pc, 0);
    let args: Vec<String> = (1..nslots as i32).map(|i| eng.read_at(pc, i)).collect();
    print!("{}", format_c(&fmt, &args));
    std::io::stdout().flush().ok();
}

struct Spec {
    left: bool,
    zero: bool,
    plus: bool,
    space: bool,
    alt: bool,
    width: Option<usize>,
    prec: Option<usize>,
    conv: char,
}

fn format_c(fmt: &str, args: &[String]) -> String {
    let c: Vec<char> = fmt.chars().collect();
    let (mut out, mut ai, mut i) = (String::new(), 0usize, 0usize);
    while i < c.len() {
        if c[i] != '%' {
            out.push(c[i]);
            i += 1;
            continue;
        }
        i += 1;
        if i < c.len() && c[i] == '%' {
            out.push('%');
            i += 1;
            continue;
        }
        let (mut left, mut zero, mut plus, mut space, mut alt) = (false, false, false, false, false);
        while i < c.len() {
            match c[i] {
                '-' => left = true,
                '0' => zero = true,
                '+' => plus = true,
                ' ' => space = true,
                '#' => alt = true,
                _ => break,
            }
            i += 1;
        }
        let mut width = None;
        while i < c.len() && c[i].is_ascii_digit() {
            width = Some(width.unwrap_or(0) * 10 + (c[i] as usize - '0' as usize));
            i += 1;
        }
        let mut prec = None;
        if i < c.len() && c[i] == '.' {
            i += 1;
            let mut p = 0usize;
            while i < c.len() && c[i].is_ascii_digit() {
                p = p * 10 + (c[i] as usize - '0' as usize);
                i += 1;
            }
            prec = Some(p);
        }
        while i < c.len() && matches!(c[i], 'l' | 'h' | 'z' | 'j' | 't' | 'L') {
            i += 1;
        }
        if i >= c.len() {
            out.push('%');
            break;
        }
        let spec = Spec { left, zero, plus, space, alt, width, prec, conv: c[i] };
        i += 1;
        let arg = args.get(ai).map(String::as_str).unwrap_or("");
        ai += 1;
        out.push_str(&render(&spec, arg));
    }
    out
}

/// 数值符号：负号恒先；正数按 `+` / 空格标志补前缀。
fn sign_of(neg: bool, plus: bool, space: bool) -> &'static str {
    if neg {
        "-"
    } else if plus {
        "+"
    } else if space {
        " "
    } else {
        ""
    }
}

/// 宽度填充：prefix（符号 / `0x`）恒留左侧，零填充嵌在 prefix 与数字之间。
fn pad(prefix: &str, body: &str, s: &Spec, zeroable: bool) -> String {
    let len = prefix.chars().count() + body.chars().count();
    match s.width {
        Some(w) if w > len => {
            let fill = w - len;
            if s.left {
                format!("{prefix}{body}{}", " ".repeat(fill))
            } else if s.zero && zeroable {
                format!("{prefix}{}{body}", "0".repeat(fill))
            } else {
                format!("{}{prefix}{body}", " ".repeat(fill))
            }
        }
        _ => format!("{prefix}{body}"),
    }
}

fn as_i64(arg: &str) -> i64 {
    let t = arg.trim();
    t.parse::<i64>()
        .or_else(|_| t.parse::<f64>().map(|f| f as i64))
        .unwrap_or(0)
}
fn as_f64(arg: &str) -> f64 {
    arg.trim().parse::<f64>().unwrap_or(0.0)
}

fn render(s: &Spec, arg: &str) -> String {
    match s.conv {
        'd' | 'i' => {
            let v = as_i64(arg);
            let mag = (v as i128).unsigned_abs().to_string();
            let mag = zpad_prec(mag, s.prec);
            pad(sign_of(v < 0, s.plus, s.space), &mag, s, true)
        }
        'u' => pad("", &zpad_prec((as_i64(arg) as u64).to_string(), s.prec), s, true),
        'o' => {
            let d = format!("{:o}", as_i64(arg) as u64);
            let pfx = if s.alt && !d.starts_with('0') { "0" } else { "" };
            pad(pfx, &zpad_prec(d, s.prec), s, true)
        }
        'x' | 'X' => {
            let u = as_i64(arg) as u64;
            let d = if s.conv == 'x' { format!("{u:x}") } else { format!("{u:X}") };
            let pfx = match (s.alt && u != 0, s.conv == 'x') {
                (true, true) => "0x",
                (true, false) => "0X",
                _ => "",
            };
            pad(pfx, &zpad_prec(d, s.prec), s, true)
        }
        'f' | 'F' => {
            let v = as_f64(arg);
            let d = format!("{:.*}", s.prec.unwrap_or(6), v.abs());
            pad(sign_of(v.is_sign_negative(), s.plus, s.space), &d, s, true)
        }
        'e' | 'E' => {
            let v = as_f64(arg);
            let d = fmt_e(v.abs(), s.prec.unwrap_or(6), s.conv == 'E');
            pad(sign_of(v.is_sign_negative(), s.plus, s.space), &d, s, true)
        }
        'g' | 'G' => {
            let v = as_f64(arg);
            let d = fmt_g(v.abs(), s.prec.unwrap_or(6), s.conv == 'G', s.alt);
            pad(sign_of(v.is_sign_negative(), s.plus, s.space), &d, s, true)
        }
        'c' => {
            let ch: String = if arg.chars().count() == 1 {
                arg.to_string()
            } else {
                arg.trim()
                    .parse::<u32>()
                    .ok()
                    .and_then(char::from_u32)
                    .map(|c| c.to_string())
                    .unwrap_or_default()
            };
            pad("", &ch, s, false)
        }
        's' => {
            let body: String = match s.prec {
                Some(p) => arg.chars().take(p).collect(),
                None => arg.to_string(),
            };
            pad("", &body, s, false)
        }
        other => {
            let mut r = String::from('%');
            r.push(other);
            r
        }
    }
}

/// 整数精度：至少 prec 位，不足前补 0（区别于宽度的空格填充）。
fn zpad_prec(d: String, prec: Option<usize>) -> String {
    match prec {
        Some(p) if d.chars().count() < p => format!("{}{d}", "0".repeat(p - d.chars().count())),
        _ => d,
    }
}

/// C 风格 `%e`：尾数 . 精度 + `e±dd`（指数至少两位）。
fn fmt_e(mag: f64, prec: usize, upper: bool) -> String {
    let (mant, exp) = decompose(mag, prec);
    let e = if upper { 'E' } else { 'e' };
    let sign = if exp < 0 { '-' } else { '+' };
    format!("{mant}{e}{sign}{:02}", exp.unsigned_abs())
}

/// 把 mag 拆成 [尾数串(含 prec 位小数, ∈[1,10)), 十进制指数]；含进位归一。mag≥0。
fn decompose(mag: f64, prec: usize) -> (String, i32) {
    if mag == 0.0 {
        return (format!("{:.*}", prec, 0.0), 0);
    }
    let mut exp = mag.log10().floor() as i32;
    let mut mant = format!("{:.*}", prec, mag / 10f64.powi(exp));
    if mant.parse::<f64>().unwrap_or(0.0) >= 10.0 {
        exp += 1;
        mant = format!("{:.*}", prec, mag / 10f64.powi(exp));
    }
    (mant, exp)
}

/// C 风格 `%g`：按指数在 %e / %f 间取短者，精度=有效位数；除非 `#`，去尾零与尾点。
fn fmt_g(mag: f64, prec: usize, upper: bool, alt: bool) -> String {
    let p = prec.max(1);
    let exp = if mag == 0.0 { 0 } else { decompose(mag, p - 1).1 };
    let mut d = if exp < -4 || exp >= p as i32 {
        fmt_e(mag, p - 1, upper)
    } else {
        format!("{:.*}", (p as i32 - 1 - exp).max(0) as usize, mag)
    };
    if !alt && d.contains('.') {
        let (num, tail) = match d.find(['e', 'E']) {
            Some(k) => (d[..k].to_string(), d[k..].to_string()),
            None => (d.clone(), String::new()),
        };
        let num = num.trim_end_matches('0').trim_end_matches('.');
        d = format!("{num}{tail}");
    }
    d
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
