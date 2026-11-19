//! v9：把 layout 产出的明文 rwir 编译成 binrwir（8 字节格）。
//!
//! 当前里程碑：**读**——把 `/lib/<pkg>·<func>/` 的指令槽与操作数槽读出来，
//! 按 `runtimec_v9_binrwir_opids.yml` 查 opid，打印清单。写 `.bin` 在下一步。
//!
//! 用法：`v9 <dsn> <function-key> [opids.yml]`
//!   v9 shm:///tmp/x.shm /lib/probe·iops

mod compile;
mod printer;

use std::collections::HashMap;
use std::ffi::CString;
use std::os::raw::{c_char, c_int, c_void};

type Handle = *mut c_void;

extern "C" {
    fn kvspaceConnect(dsn: *const c_char) -> Handle;
    fn kvspaceClose(h: Handle);
    fn kvspaceResolveOffset(
        h: Handle,
        key: *const c_char,
        parent_key_anchor: i64,
        out_value_offset: *mut u64,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    fn kvspaceRealloc(
        h: Handle,
        key: *const c_char,
        body_length: u64,
        attr_length: u64,
        out_value_offset: *mut u64,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    fn kvspaceXvalueInProcessAddress(h: Handle) -> *mut u8;
    fn kvspaceListlen(
        h: Handle,
        key: *const c_char,
        out_count: *mut i64,
        out_max_name_length: *mut u32,
        err: *mut c_char,
        err_cap: u32,
    ) -> c_int;
    fn kvspaceList(
        h: Handle,
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

/// 一个 XValue 的读出结果：langtype（attr tag 1）+ body 原始字节。
struct Value {
    langtype: String,
    body: Vec<u8>,
}

struct Kv {
    h: Handle,
}

impl Kv {
    fn connect(dsn: &str) -> Kv {
        let c = CString::new(dsn).expect("dsn 不能含 NUL");
        let h = unsafe { kvspaceConnect(c.as_ptr()) };
        if h.is_null() {
            panic!("kvspace connect failed: {dsn}");
        }
        Kv { h }
    }

    fn read(&self, key: &str) -> Option<Value> {
        let c = CString::new(key).ok()?;
        let mut offset: u64 = 0;
        let mut err = [0 as c_char; 256];
        let ret = unsafe {
            kvspaceResolveOffset(self.h, c.as_ptr(), 0, &mut offset, err.as_mut_ptr(), 256)
        };
        if ret != 0 {
            return None;
        }
        let base = unsafe { kvspaceXvalueInProcessAddress(self.h) };
        if base.is_null() {
            return None;
        }
        unsafe {
            let head = std::slice::from_raw_parts(base.add(offset as usize), 16);
            let body_len = u64::from_le_bytes(head[0..8].try_into().unwrap()) as usize;
            let attrs_len = u16::from_le_bytes([head[8], head[9]]) as usize;
            let total = 16 + body_len + attrs_len;
            let wire = std::slice::from_raw_parts(base.add(offset as usize), total);
            let body = wire[16..16 + body_len].to_vec();
            let mut langtype = String::new();
            let mut at = 16 + body_len;
            while at + 3 <= total {
                let tag = wire[at];
                let len = u16::from_le_bytes([wire[at + 1], wire[at + 2]]) as usize;
                let start = at + 3;
                if start + len > total {
                    break;
                }
                if tag == 1 {
                    langtype = String::from_utf8_lossy(&wire[start..start + len]).into_owned();
                }
                at = start + len;
            }
            Some(Value { langtype, body })
        }
    }

    /// 读 `.bin`：返回 (langtype, bodylen, body)，并校验 head 自洽。
    ///
    /// 与 `read` 的区别：`read` 宽松（读不到就 None），这里**严格**——
    /// 打印工具是给人核对产物的，head 不自洽必须报错而不是打一份假的。
    fn read_head(&self, key: &str) -> Result<(String, u64, Vec<u8>), String> {
        let c = CString::new(key).map_err(|_| "key 含 NUL".to_string())?;
        let mut offset: u64 = 0;
        let mut err = [0 as c_char; 256];
        let ret = unsafe {
            kvspaceResolveOffset(self.h, c.as_ptr(), 0, &mut offset, err.as_mut_ptr(), 256)
        };
        if ret != 0 {
            return Err(format!("resolve 失败：{key}"));
        }
        let base = unsafe { kvspaceXvalueInProcessAddress(self.h) };
        if base.is_null() {
            return Err("无进程内值区".into());
        }
        unsafe {
            let head = std::slice::from_raw_parts(base.add(offset as usize), 16);
            if head[10..16].iter().any(|b| *b != 0) {
                return Err(format!("padding 非零：{:02X?}", &head[10..16]));
            }
            let bodylen = u64::from_le_bytes(head[0..8].try_into().unwrap());
            let attrs = u16::from_le_bytes([head[8], head[9]]) as usize;
            let total = 16usize
                .checked_add(usize::try_from(bodylen).map_err(|_| "bodylen 过大")?)
                .and_then(|t| t.checked_add(attrs))
                .ok_or("总长溢出")?;
            let wire = std::slice::from_raw_parts(base.add(offset as usize), total);
            let body = wire[16..16 + bodylen as usize].to_vec();
            let mut langtype = String::new();
            let mut at = 16 + bodylen as usize;
            while at + 3 <= total {
                let len = u16::from_le_bytes([wire[at + 1], wire[at + 2]]) as usize;
                let start = at + 3;
                if start + len > total {
                    return Err("attr 记录截断".into());
                }
                if wire[at] == 1 {
                    langtype = String::from_utf8_lossy(&wire[start..start + len]).into_owned();
                }
                at = start + len;
            }
            Ok((langtype, bodylen, body))
        }
    }

    /// key 的**值区偏移**（不读内容）。供解析表引用 layout 槽。
    fn offset_of(&self, key: &str) -> u64 {
        let Ok(c) = CString::new(key) else { return 0 };
        let mut offset: u64 = 0;
        let mut err = [0 as c_char; 256];
        let ret = unsafe {
            kvspaceResolveOffset(self.h, c.as_ptr(), 0, &mut offset, err.as_mut_ptr(), 256)
        };
        if ret != 0 {
            return 0;
        }
        offset
    }

    fn list(&self, prefix: &str) -> Vec<String> {
        let c = CString::new(prefix).expect("prefix 不能含 NUL");
        let mut count: i64 = 0;
        let mut max_name: u32 = 0;
        let mut err = [0 as c_char; 256];
        let ret = unsafe {
            kvspaceListlen(self.h, c.as_ptr(), &mut count, &mut max_name, err.as_mut_ptr(), 256)
        };
        if ret != 0 || count <= 0 {
            return Vec::new();
        }
        let cap = (count as usize) * (max_name as usize + 1);
        let mut buf = vec![0u8; cap];
        let mut length: u32 = 0;
        let mut listed: u32 = 0;
        let mut generation: u64 = 0;
        let ret = unsafe {
            kvspaceList(
                self.h,
                c.as_ptr(),
                buf.as_mut_ptr() as *mut c_char,
                cap as u32,
                &mut length,
                &mut listed,
                &mut generation,
                err.as_mut_ptr(),
                256,
            )
        };
        if ret != 0 {
            return Vec::new();
        }
        buf[..length as usize]
            .split(|b| *b == 0)
            .filter(|s| !s.is_empty())
            .map(|s| String::from_utf8_lossy(s).into_owned())
            .collect()
    }
}

impl Kv {
    /// 写 `/lib/<pkg>·<func>.bin`：body = 8×N，langtype `[8N]uint8`（spec §一）。
    ///
    /// head 由 `kvspaceRealloc` 写，本函数只填 body 与 attr。
    fn write(&self, key: &str, cells: &[[u8; compile::CELL]]) {
        let body: Vec<u8> = cells.iter().flat_map(|c| c.iter().copied()).collect();
        let langtype = format!("[{}]uint8", body.len());
        let mut attr = Vec::with_capacity(3 + langtype.len());
        attr.push(1u8);
        attr.extend_from_slice(&(langtype.len() as u16).to_le_bytes());
        attr.extend_from_slice(langtype.as_bytes());
        let c = CString::new(key).expect("key 不能含 NUL");
        let mut offset: u64 = 0;
        let mut err = [0 as c_char; 256];
        let ret = unsafe {
            kvspaceRealloc(
                self.h,
                c.as_ptr(),
                body.len() as u64,
                attr.len() as u64,
                &mut offset,
                err.as_mut_ptr(),
                256,
            )
        };
        if ret != 0 {
            panic!("realloc 失败 {key}: {}", String::from_utf8_lossy(&err.iter().map(|c| *c as u8).take_while(|b| *b != 0).collect::<Vec<_>>()));
        }
        let base = unsafe { kvspaceXvalueInProcessAddress(self.h) };
        if base.is_null() {
            panic!("无进程内值区");
        }
        unsafe {
            std::ptr::copy_nonoverlapping(body.as_ptr(), base.add(offset as usize + 16), body.len());
            std::ptr::copy_nonoverlapping(
                attr.as_ptr(),
                base.add(offset as usize + 16 + body.len()),
                attr.len(),
            );
        }
    }
}

impl Drop for Kv {
    fn drop(&mut self) {
        unsafe { kvspaceClose(self.h) };
    }
}

/// `{opid: 12, name: ..., ...}` → name 到 opid。
fn load_opids(path: &str) -> HashMap<String, u16> {
    let text = std::fs::read_to_string(path)
        .unwrap_or_else(|e| panic!("读不了 opid 表 {path}: {e}"));
    let mut map = HashMap::new();
    for line in text.lines() {
        let line = line.trim();
        let Some(rest) = line.strip_prefix("- {") else {
            continue;
        };
        let Some(opid) = field(rest, "opid: ") else {
            continue;
        };
        let Some(name) = field(rest, "name: ") else {
            continue;
        };
        map.insert(name.to_string(), opid.parse().expect("opid 不是数字"));
    }
    map
}

fn field<'a>(text: &'a str, label: &str) -> Option<&'a str> {
    let start = text.find(label)? + label.len();
    let rest = &text[start..];
    let end = rest.find(',').unwrap_or(rest.len());
    Some(rest[..end].trim())
}

fn slot(name: &str) -> Option<(i32, i32)> {
    let inner = name.strip_prefix('[')?.strip_suffix(']')?;
    let mut parts = inner.split(',');
    let s0 = parts.next()?.trim().parse().ok()?;
    let s1 = parts.next()?.trim().parse().ok()?;
    Some((s0, s1))
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    /* `v9 print <dsn> <bin-key> [opids.yml]` 是只读模式；其余形态照旧编译。 */
    if args.get(1).map(String::as_str) == Some("print") {
        let rest = &args[2..];
        if rest.len() < 2 {
            eprintln!("usage: v9 print <dsn> <bin-key> [opids.yml]");
            std::process::exit(2);
        }
        let opid_path = rest
            .get(2)
            .map(String::as_str)
            .unwrap_or("runtimec/binrwir/runtimec_v9_binrwir_opids.yml");
        if let Err(message) = print_bin(&rest[0], &rest[1], opid_path) {
            eprintln!("v9 print: {message}");
            std::process::exit(1);
        }
        return;
    }
    if args.len() < 3 {
        eprintln!("usage: v9 <dsn> <function-key> [opids.yml]");
        eprintln!("       v9 print <dsn> <bin-key> [opids.yml]");
        std::process::exit(2);
    }
    let dsn = &args[1];
    let function = args[2].trim_end_matches('/');
    let opid_path = args
        .get(3)
        .map(String::as_str)
        .unwrap_or("runtimec/binrwir/runtimec_v9_binrwir_opids.yml");
    let opids = load_opids(opid_path);

    let kv = Kv::connect(dsn);
    let mut slots: Vec<(i32, i32)> = kv
        .list(&format!("{function}/"))
        .iter()
        .filter_map(|name| slot(name))
        .collect();
    slots.sort();

    println!("{function}: {} 个槽", slots.len());
    let mut missing = Vec::new();
    let mut s0s: Vec<i32> = slots.iter().map(|(s0, _)| *s0).collect();
    s0s.dedup();
    for s0 in s0s {
        let key = format!("{function}/[{s0},0]");
        let Some(value) = kv.read(&key) else {
            continue;
        };
        if std::env::var_os("V9_DUMP").is_some() {
            let hex: String = value
                .body
                .iter()
                .map(|b| format!("{b:02X} "))
                .collect();
            println!("[{s0},0] body({}): {hex}", value.body.len());
        }
        // rwir 指令体：`[读参个数][00][写参个数][00][00]` + 算子文本。
        let arity = if value.body.len() >= 5 {
            format!("nr={},nw={}", value.body[0], value.body[2])
        } else {
            String::new()
        };
        let name = if value.body.len() > 5 {
            String::from_utf8_lossy(&value.body[5..]).into_owned()
        } else {
            String::new()
        };
        let opid = opids.get(&name);
        let describe = |b: i32| {
            let key = format!("{function}/[{s0},{b}]");
            match kv.read(&key) {
                None => format!("[{s0},{b}] <空>"),
                Some(v) => {
                    if std::env::var_os("V9_DUMP").is_some() {
                        let hex: String =
                            v.body.iter().map(|x| format!("{x:02X} ")).collect();
                        format!("[{s0},{b}] body({}): {hex}", v.body.len())
                    } else {
                        let text = String::from_utf8_lossy(&v.body).replace('\0', "");
                        format!("[{s0},{b}] {} [{}]", text.trim(), v.langtype)
                    }
                }
            }
        };
        let reads: Vec<String> = slots
            .iter()
            .filter(|(a, b)| *a == s0 && *b < 0)
            .map(|(_, b)| describe(*b))
            .collect();
        let writes: Vec<String> = slots
            .iter()
            .filter(|(a, b)| *a == s0 && *b > 0)
            .map(|(_, b)| describe(*b))
            .collect();
        if opid.is_none() {
            missing.push(name.clone());
        }
        println!(
            "s0={s0:<3} {:<6} {arity:<9} opid={} | 读: {} | 写: {}",
            if name.is_empty() { "<签名>" } else { &name },
            opid.map(|o| o.to_string()).unwrap_or_else(|| "-".into()),
            reads.join(" ; "),
            writes.join(" ; ")
        );
    }
    if !missing.is_empty() {
        println!("\n（以下名字需按操作数类型组元组查，见编译阶段）");
    }
    if std::env::var_os("V9_WRITE").is_none() {
        return;
    }
    let (cells, op_cells) = compile_function(&kv, &function, &opids);
    println!("\n格数 N = {}", cells.len());
    println!("解析表: {} 项", u64::from_le_bytes(cells[1]));
    for (i, cell) in cells.iter().enumerate() {
        let word = u64::from_le_bytes(*cell);
        /* 前 3 格是 goto + 表头 + 表项，其後是各段；op 字要按位段读，
         * 不能和操作数格用同一套解码（那会把 opcode 当 kind 读出来）。 */
        let text = if i == 0 || op_cells.contains(&i) {
            describe_op(word)
        } else {
            describe_cell(word)
        };
        println!("[{i:>3}] {word:016X}  {text}");
    }
    let out_key = format!("{function}.bin");
    kv.write(&out_key, &cells);
    println!("\n写入 {out_key}（{} 字节）", cells.len() * compile::CELL);
}

/// 编译一个函数：读槽 → 排格 → 收敛成 `Compiled`。
fn compile_function(kv: &Kv, function: &str, opids: &HashMap<String, u16>) -> (Vec<[u8; compile::CELL]>, Vec<usize>) {
    /* 1) 收集指令：每个 s0 一条，s1=0 是 op 字，s1<0 读、s1>0 写。 */
    let mut slots: Vec<(i32, i32)> = kv
        .list(&format!("{function}/"))
        .iter()
        .filter_map(|name| slot(name))
        .collect();
    slots.sort();
    let mut s0s: Vec<i32> = slots.iter().map(|(a, _)| *a).collect();
    s0s.dedup();

    struct Instr {
        s0: i32,
        opcode: u16,
        reads: Vec<compile::Operand>,
        writes: Vec<compile::Operand>,
    }
    let mut instrs: Vec<Instr> = Vec::new();
    for s0 in &s0s {
        let Some(head) = kv.read(&format!("{function}/[{s0},0]")) else {
            continue;
        };
        let name = String::from_utf8_lossy(&head.body[5.min(head.body.len())..]).into_owned();
        let name = name.trim().to_string();
        /* s0=0 是签名槽（`rwfunc:(nr,nw)`，无算子名），不是指令。 */
        if name.is_empty() {
            continue;
        }
        let bin_name = match name.as_str() {
            "=" => "C·memcpy",
            "br" => "branch",
            other => other,
        };
        let collect = |want_read: bool| -> Vec<(compile::Operand, String)> {
            let mut list: Vec<(i32, (compile::Operand, String))> = slots
                .iter()
                .filter(|(a, b)| a == s0 && (if want_read { *b < 0 } else { *b > 0 }))
                .filter_map(|(_, b)| {
                    let key = format!("{function}/[{s0},{b}]");
                    let v = kv.read(&key)?;
                    let op = compile::Operand::from_slot(&v.langtype, &v.body)?;
                    if std::env::var_os("V9_DEBUG").is_some() {
                        eprintln!("    op {key} lt={} body={} -> ok", v.langtype, v.body.len());
                    }
                    /* 操作数自身的类型：`rwir` 槽的 body 是 `名字 langtype`，取后半段。 */
                    /* `rwir` 槽的 body = `[5 字节头][名字]0[langtype]`；
                     * 类型是 **NUL 之后**那一段（不是空格分，见 V9_DUMP 实测）。 */
                    let own = if v.langtype == "rwir" {
                        let text = String::from_utf8_lossy(&v.body[5.min(v.body.len())..]);
                        text.split('\0').nth(1).unwrap_or("int64").trim().to_string()
                    } else {
                        v.langtype.clone()
                    };
                    Some((*b, (op, own)))
                })
                .collect();
            list.sort_by_key(|(b, _)| b.abs());
            list.into_iter().map(|(_, o)| o).collect()
        };
        let mut reads: Vec<(compile::Operand, String)> = collect(true);
        let writes: Vec<(compile::Operand, String)> = collect(false);
        /* 查 opid：先按原名（goto/return/C·memcpy 这类无类型参数的），
         * 再按 `<op>_<读1>_<读2>_<写>` 元组（add/le/mul… 这些按操作数类型特化）。 */
        let tuple_name = |r: &[(compile::Operand, String)], w: &[(compile::Operand, String)]| {
            let mut parts = vec![bin_name.to_string()];
            parts.extend(r.iter().map(|(_, t)| bin_type(t).to_string()));
            parts.extend(w.iter().map(|(_, t)| bin_type(t).to_string()));
            parts.join("_")
        };
        let opcode = opids.get(bin_name).copied().or_else(|| {
            if bin_name == "C·memcpy" || bin_name == "goto" || bin_name == "branch" || bin_name == "return" {
                None
            } else {
                opids.get(&tuple_name(&reads, &writes)).copied()
            }
        });
        let Some(opcode) = opcode else {
            panic!("没有 opid：{bin_name} 或 {}", tuple_name(&reads, &writes));
        };
        /* goto/branch 的目标是 s0 指令序号（spec §九），不是普通标量：
         * layout 已把它解析成 `int64:<序号>`，这里换回 `kind = 3`。 */
        if name == "goto" || name == "br" {
            for (operand, _) in reads.iter_mut().skip(if name == "br" { 1 } else { 0 }) {
                if let compile::Operand::Constant(bytes) = operand {
                    let s0_target = u64::from_le_bytes(bytes[..8].try_into().unwrap()) as i64;
                    *operand = compile::Operand::Target(s0_target);
                }
            }
        }
        /* `C·memcpy` 签名是 2 读（src, byte_count）、1 写（dest）。
         * byte_count 的宽度来自**源操作数的 langtype**；变宽类型按 spec §十二 报错不猜。 */
        if bin_name == "C·memcpy" {
            let width = reads
                .first()
                .and_then(|(_, t)| compile::scalar_width(t))
                .unwrap_or_else(|| panic!("变宽类型 `=` 未实现（spec §十二）：s0={s0}"));
            let mut cell = [0u8; compile::CELL];
            cell[..8].copy_from_slice(&width.to_le_bytes());
            reads.push((compile::Operand::Constant(cell.to_vec()), "uint64".to_string()));
        }
        instrs.push(Instr {
            s0: *s0,
            opcode,
            reads: reads.into_iter().map(|(o, _)| o).collect(),
            writes: writes.into_iter().map(|(o, _)| o).collect(),
        });
    }

    /* 2) 字面量区：常量按出现顺序各占一格，记「格号」。 */
    let mut literal_of: HashMap<Vec<u8>, usize> = HashMap::new();
    let mut literals: Vec<[u8; compile::CELL]> = Vec::new();
    /* 帧局部 → addrbuf 索引（按 s0 顺序、同名去重，spec §五之二）。 */
    let mut frame_index: HashMap<String, u64> = HashMap::new();
    let mut frame_order: Vec<String> = Vec::new();

    /* 2.5) 先扫出全部帧局部（编码时要用它算绝对格号的基址）。 */
    for ins in &instrs {
        for operand in ins.reads.iter().chain(ins.writes.iter()) {
            if let compile::Operand::FrameLocal { name, .. } = operand {
                if !frame_index.contains_key(name) {
                    let next = frame_index.len() as u64;
                    frame_index.insert(name.clone(), next);
                    frame_order.push(name.clone());
                }
            }
        }
    }
    /* 字面量格的绝对格号基址：goto + 表头 + 表项。 */
    let literal_base = 1 + 1 + frame_order.len();

    /* 3) 指令流：为每条指令分配格号（goto 要用它换算距离）。 */
    let mut code: Vec<[u8; compile::CELL]> = Vec::new();
    let mut cell_of_s0: HashMap<i32, usize> = HashMap::new();
    for ins in &instrs {
        cell_of_s0.insert(ins.s0, code.len());
        for _ in 0..ins.reads.len() {
            code.push([0u8; compile::CELL]);
        }
        code.push([0u8; compile::CELL]); /* op 字占位 */
        for _ in 0..ins.writes.len() {
            code.push([0u8; compile::CELL]);
        }
    }

    /* 4) 填格：操作数 + op 字（next_distance 到下一条 op 字）。 */
    for (index, ins) in instrs.iter().enumerate() {
        let at = cell_of_s0[&ins.s0];
        /* 本条 op 字所在格 = 起始格 + 读格数。 */
        let op_at = at + ins.reads.len();
        let next_op = instrs
            .get(index + 1)
            .map(|n| cell_of_s0[&n.s0] + n.reads.len())
            .unwrap_or(op_at + 1 + ins.writes.len());
        let distance = next_op - op_at;
        if distance > 255 {
            panic!("next_distance {distance} 超 255（指令 s0={}）：不产出", ins.s0);
        }
        let mut cursor = at;
        for operand in &ins.reads {
            code[cursor] = encode_operand(operand, &mut literals, &mut literal_of, &mut frame_index, &mut frame_order, literal_base);
            cursor += 1;
        }
        code[cursor] = compile::op_word(
            ins.opcode,
            ins.reads.len() as u8,
            ins.writes.len() as u8,
            distance as u8,
        );
        cursor += 1;
        for operand in &ins.writes {
            code[cursor] = encode_operand(operand, &mut literals, &mut literal_of, &mut frame_index, &mut frame_order, literal_base);
            cursor += 1;
        }
    }

    /* 5) 组装：cell0 goto + [解析表头 + 表项] + 字面量 + 指令流。
     *
     * 解析表在字面量区**开头**（spec §五之二）：cell 1 是表项数，其後 N 项是
     * 各访问点对应 layout 槽 body 的**值区偏移**。这里先用 s0 相对序号占位——
     * 真实偏移要 kvspace 解析，见下方 `resolve_slot_offset`。 */
    let table_len = 1 + frame_order.len();
    /* cell0 的 next 指**第一条指令的 op 字**，与 `next_distance` 同一语义
     * （「到下一个 op 字」）。指读格起点会让读方无法从入口定位 op——
     * 它得先知道 read_count，而 read_count 在 op 字里——循环依赖。 */
    let code_start = 1 + table_len + literals.len() + instrs.first().map_or(0, |i| i.reads.len());
    let mut all: Vec<[u8; compile::CELL]> = Vec::with_capacity(code_start + code.len());
    all.push(compile::op_word(
        opids["goto"],
        0,
        0,
        u8::try_from(code_start).expect("字面量区超 255 格"),
    ));
    all.push((frame_order.len() as u64).to_le_bytes());
    for name in &frame_order {
        let offset = resolve_slot_offset(kv, function, &s0s, name);
        all.push(offset.to_le_bytes());
    }
    all.extend(literals);
    let code_at = all.len();
    all.extend(code);
    /* op 字在指令流里的绝对格号，供描述时按位段读。 */
    let op_cells: Vec<usize> = instrs
        .iter()
        .map(|ins| code_at + cell_of_s0[&ins.s0] + ins.reads.len())
        .collect();
    (all, op_cells)
}

/// 操作数 → 格。常量先入字面量区取格号。
fn encode_operand(
    operand: &compile::Operand,
    literals: &mut Vec<[u8; compile::CELL]>,
    literal_of: &mut HashMap<Vec<u8>, usize>,
    frame_index: &mut HashMap<String, u64>,
    frame_order: &mut Vec<String>,
    literal_base: usize,
) -> [u8; compile::CELL] {
    match operand {
        compile::Operand::Constant(bytes) => {
            /* 格号是**绝对**格号：goto(1) + 表头(1) + 表项(N) + literals 内下标。
             * 表项数在编码时才知道，故先记下标、组装时再加基址（见下）。 */
            let index = *literal_of.entry(bytes.clone()).or_insert_with(|| {
                let mut cell = [0u8; compile::CELL];
                cell.copy_from_slice(bytes);
                literals.push(cell);
                literals.len() - 1
            });
            compile::operand_cell(compile::KIND_LITERAL, (literal_base + index) as u64, false)
        }
        compile::Operand::FrameLocal { name, deref } => {
            let index = match frame_index.get(name) {
                Some(existing) => *existing,
                None => {
                    let next = frame_index.len() as u64;
                    frame_index.insert(name.clone(), next);
                    frame_order.push(name.clone());
                    next
                }
            };
            compile::operand_cell(compile::KIND_ADDRBUF, index, *deref)
        }
        compile::Operand::Target(s0) => {
            compile::operand_cell(compile::KIND_TARGET, *s0 as u64, false)
        }
    }
}

/// 把一个格解成人能读的形态（自检用）。
fn describe_cell(word: u64) -> String {
    let deref = word >> 63 & 1 == 1;
    let kind = (word >> 62) & 3;
    let payload = word & ((1u64 << 62) - 1);
    let d = if deref { "*" } else { "" };
    match kind {
        0 => format!("{d}literal#{payload}"),
        1 => format!("{d}kvspace#{payload}"),
        3 => format!("{d}target s0={payload}"),
        _ => format!("{d}addrbuf#{payload}"),
    }
}

/// langtype → binrwir 元组里的类型名。
fn bin_type(langtype: &str) -> &str {
    match langtype {
        "bool" => "bool",
        "int8" => "int8",
        "uint8" => "uint8",
        "int16" => "int16",
        "uint16" => "uint16",
        "int32" => "int32",
        "uint32" => "uint32",
        "int64" => "int64",
        "uint64" => "uint64",
        "float32" => "float32",
        "float64" => "float64",
        other => other,
    }
}

/// 帧局部名字 → 它第一次出现的 layout 槽的**值区偏移**。
///
/// 解析表要的是「哪一格 layout 槽描述了这个名字」，不是名字的存储位置——
/// 帧局部的存储建帧期才有。所以这里返回的是**描述槽**的偏移。
fn resolve_slot_offset(kv: &Kv, function: &str, s0s: &[i32], name: &str) -> u64 {
    for s0 in s0s {
        let prefix = format!("{function}/[{s0},");
        for slot_name in kv.list(&format!("{function}/")) {
            let Some((a, b)) = slot(&slot_name) else { continue };
            if a != *s0 {
                continue;
            }
            let key = format!("{prefix}{b}]");
            let Some(v) = kv.read(&key) else { continue };
            if v.langtype != "rwir" {
                continue;
            }
            let text = String::from_utf8_lossy(&v.body[5.min(v.body.len())..]).into_owned();
            let found = text.split('\0').next().unwrap_or("").trim();
            let bare = found.strip_prefix('*').unwrap_or(found);
            if bare == name {
                return kv.offset_of(&key);
            }
        }
    }
    0
}

/// op 字按位段读：`opcode16 | read4 | write4 | next_distance8`。
fn describe_op(word: u64) -> String {
    let opcode = word & 0xFFFF;
    let reads = (word >> 16) & 0xF;
    let writes = (word >> 20) & 0xF;
    let next = (word >> 24) & 0xFF;
    format!("op#{opcode} r={reads} w={writes} next={next}")
}

/// `v9 print`：读 `.bin` → 校验 → 分三段打印。失败返回 Err（调用方非零退出）。
fn print_bin(dsn: &str, bin_key: &str, opid_path: &str) -> Result<(), String> {
    let opids = load_opids(opid_path);
    let lookup: HashMap<u16, &str> = opids.iter().map(|(name, id)| (*id, name.as_str())).collect();
    let kv = Kv::connect(dsn);
    let (langtype, bodylen, body) = kv.read_head(bin_key)?;
    let bin = printer::parse(&body, &langtype, bodylen)?;

    println!("{bin_key}");
    println!("  bodylen  {}  langtype  {langtype}", body.len());
    println!("  格数     {}  入口格    {}", bin.cells.len(), bin.entry);

    println!("\n解析表（{} 项）", bin.table_len);
    for index in 0..bin.table_len {
        let offset = u64::from_le_bytes(bin.cells[2 + index]);
        println!("  [{index}] 值区偏移 {offset}");
    }

    println!("\n字面量区（cell {}..{}）", 2 + bin.table_len, bin.entry);
    for cell in 2 + bin.table_len..bin.entry {
        let word = u64::from_le_bytes(bin.cells[cell]);
        println!("  [{cell}] {word}  (0x{word:016X})");
    }

    println!("\n指令流（入口 op 字 cell {}）", bin.entry);
    let mut at = bin.entry;
    let mut seen = 0usize;
    loop {
        if at >= bin.cells.len() {
            return Err(format!("取指越界：cell {at} >= {}", bin.cells.len()));
        }
        let (opcode, reads, writes, next) = printer::op_fields(u64::from_le_bytes(bin.cells[at]));
        if opcode == 0 {
            return Err(format!("cell {at} 的 opcode 为 0"));
        }
        let name = lookup.get(&opcode).copied().unwrap_or("-");
        let operands = |start: usize, count: usize| -> Result<Vec<String>, String> {
            (0..count)
                .map(|k| printer::operand_text(u64::from_le_bytes(bin.cells[start + k])))
                .collect()
        };
        let read_text = if at < reads {
            return Err(format!("cell {at} 的读格起点越界"));
        } else {
            operands(at - reads, reads)?
        };
        let write_text = operands(at + 1, writes)?;
        println!(
            "  [{at:>3}] op#{opcode} {name} r={reads} w={writes} next={next}  | 读: {} | 写: {}",
            read_text.join(" ; "),
            write_text.join(" ; ")
        );
        seen += 1;
        if seen > bin.cells.len() {
            return Err("指令流成环（步数超过格数）".into());
        }
        /* 本里程碑的执行器语义：落点是下一条的 op 字。 */
        if next == 0 {
            return Err(format!("cell {at} 的 next_distance 为 0（原地打转）"));
        }
        at += next;
        if at == bin.cells.len() {
            break;
        }
    }
    Ok(())
}
