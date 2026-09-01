//! Engine —— kvlang runtime 核心原语：kvspace 读写、TLV 编解码、rwir 读写槽解析、结构操作。
//! 模式2 驱动循环在二进制 main.rs（含 print 批处理 + 外部 rwir handoff）；具体纯净 rwir 见 `crate::rwir`。

use std::ffi::{c_char, c_void};
use std::ptr::null_mut;

use crate::ffi::*;
use crate::rwir;

pub struct Engine {
    pub rt: *mut c_void, // kvlang runtime 句柄
    pub kv: *mut c_void, // kvspace 句柄（自持，同时传给 rwirext）
    pub dsn: String,     // kvspace DSN（layout rwir 需要）
    /// 外部 rwir 就地处理器（返回 true=已处理）。run_fn 遇非纯净 rwir 时委托；None 则报未知。
    pub ext: Option<fn(&Engine, op: &str, pc: &str) -> bool>,
}

impl Engine {
    // ── kvspace 读写（绝对路径，char/utf8 与 char/utf32 编解码）─────────
    pub fn set_kv(&self, key: &str, val: &str) {
        unsafe {
            let utf32: Vec<u32> = val.chars().map(|c| c as u32).collect();
            let raw: Vec<u8> = utf32.iter().flat_map(|v| v.to_le_bytes()).collect();
            let dims = [utf32.len() as i32];
            let (mut buf, mut len) = (null_mut(), 0u32);
            kvspaceTlvEncode(
                cs("char/utf32").as_ptr(),
                raw.as_ptr(),
                raw.len() as u32,
                dims.as_ptr(),
                1,
                &mut buf,
                &mut len,
            );
            let ck = cs(key);
            let keys = [ck.as_ptr()];
            let lens = [len];
            let mut err = [0u8; 256];
            kvspaceSet(
                self.kv,
                keys.as_ptr(),
                buf,
                lens.as_ptr(),
                1,
                err.as_mut_ptr() as *mut c_char,
                256,
            );
            kvspaceBytesFree(buf, len);
        }
    }
    pub fn get_kv(&self, key: &str) -> String {
        unsafe {
            let (mut out, mut olen) = (null_mut(), 0u32);
            kvspaceGet(self.kv, cs(key).as_ptr(), &mut out, &mut olen);
            if out.is_null() || olen == 0 {
                return String::new();
            }
            let mut head = KvspaceHead::default();
            kvspaceDecodeHead(out, olen, &mut head);
            let kx = String::from_utf8_lossy(&head.kindexpr)
                .trim_end_matches('\0')
                .to_string();
            let (_, _, kind) = parse_kindexpr(&kx);
            let (bo, bl) = (head.body_offset as usize, head.body_len.max(0) as usize);
            let s = if kind == "char/utf32" {
                std::slice::from_raw_parts(out.add(bo), bl)
                    .chunks_exact(4)
                    .map(|c| {
                        char::from_u32(u32::from_le_bytes([c[0], c[1], c[2], c[3]]))
                            .unwrap_or('\u{FFFD}')
                    })
                    .collect()
            } else {
                String::from_utf8_lossy(std::slice::from_raw_parts(out.add(bo), bl)).into_owned()
            };
            kvspaceBytesFree(out, olen);
            s
        }
    }

    // ── rwir 派发时按下标解析读/写槽（rwirext 宿主 ABI，传 kvspace 句柄）─
    pub fn read0(&self, pc: &str) -> String {
        take(unsafe { kvlang_rwirextResolveRead(self.kv, cs(pc).as_ptr(), 0) })
    }
    pub fn write0(&self, pc: &str) -> String {
        take(unsafe { kvlang_rwirextResolveWrite(self.kv, cs(pc).as_ptr(), 0) })
    }

    // ── kvspace 结构操作（json/http 扩展遍历子树用）────────────────────
    pub fn list_kv(&self, prefix: &str) -> Vec<String> {
        unsafe {
            let (mut out, mut olen) = (null_mut(), 0u32);
            kvspaceList(self.kv, cs(prefix).as_ptr(), 0, 0, &mut out, &mut olen);
            if out.is_null() || olen == 0 {
                return Vec::new();
            }
            let s = String::from_utf8_lossy(std::slice::from_raw_parts(out, olen as usize))
                .into_owned();
            kvspaceBytesFree(out, olen);
            s.split('\n')
                .filter(|x| !x.is_empty())
                .map(str::to_string)
                .collect()
        }
    }
    pub fn del_tree(&self, prefix: &str) {
        unsafe {
            let mut err = [0u8; 256];
            kvspaceDelTree(
                self.kv,
                cs(prefix).as_ptr(),
                err.as_mut_ptr() as *mut c_char,
                256,
            );
        }
    }
    /// 删除单个 key（更新其父 pkg 的成员索引）。del_tree 只清子树，叶子键需本方法。
    pub fn del(&self, key: &str) {
        unsafe {
            let ck = cs(key);
            let keys = [ck.as_ptr()];
            let mut err = [0u8; 256];
            kvspaceDel(
                self.kv,
                keys.as_ptr(),
                1,
                err.as_mut_ptr() as *mut c_char,
                256,
            );
        }
    }
    pub fn get_tlv(&self, key: &str) -> Vec<u8> {
        unsafe {
            let (mut out, mut olen) = (null_mut(), 0u32);
            kvspaceGet(self.kv, cs(key).as_ptr(), &mut out, &mut olen);
            if out.is_null() || olen == 0 {
                return Vec::new();
            }
            let v = std::slice::from_raw_parts(out, olen as usize).to_vec();
            kvspaceBytesFree(out, olen);
            v
        }
    }
    pub fn set_tlv(&self, key: &str, tlv: &[u8]) {
        if tlv.is_empty() {
            return;
        }
        unsafe {
            let ck = cs(key);
            let keys = [ck.as_ptr()];
            let lens = [tlv.len() as u32];
            let mut err = [0u8; 256];
            kvspaceSet(
                self.kv,
                keys.as_ptr(),
                tlv.as_ptr(),
                lens.as_ptr(),
                1,
                err.as_mut_ptr() as *mut c_char,
                256,
            );
        }
    }

    pub fn register(&self) {
        rwir::register(self);
    }

    /// 启动时把内嵌 stdlib（lib/**/*.kv）layout 进 kvspace，使其 rwfunc 可解析。
    /// 幂等、best-effort：坏了只 warn 不中断（tutorial 多不依赖 stdlib）。
    /// 把单段内存源码 layout 进 kvspace，返回是否成功（失败打印错误）。
    pub fn layout_src(&self, name: &str, src: &str) -> bool {
        let mut err = [0u8; 4096];
        let rc = unsafe {
            kvlangLayoutCode(
                cs(src).as_ptr(),
                cs(&self.dsn).as_ptr(),
                std::ptr::null_mut(),
                0,
                err.as_mut_ptr() as *mut c_char,
                err.len() as u32,
            )
        };
        if rc != 0 {
            eprintln!("kvlang: layout {name} 失败: {}", cbuf(&err));
            return false;
        }
        true
    }

    pub fn layout_stdlib(&self) {
        for (name, src) in crate::stdlib::EMBEDDED_KV {
            self.layout_src(name, src);
        }
    }

    /// layout 之后单独运行内嵌 stdlib 各 lib 的 init（与 layout_stdlib 分离，不写进 layout 逻辑）。
    /// 常量成员式（如 /lib/math·Pi）在 init 里赋值落值，故启动时须 run，run 完即删该 init（一次性引导）。
    /// 递归遍历内嵌 stdlib 的 pkg 树（含嵌套 lib，如 time → time/duration），按深度升序 = 从 /lib 根
    /// 到叶子 pkg，父 pkg 的 init 先于子 pkg。仅限内嵌 stdlib pkg，不遍历整棵 /lib —— 否则会把用户
    /// 代码的顶层 init/入口也误跑并删掉。
    pub fn run_stdlib_init(&self) {
        // 从内嵌文件路径构造祖先闭包 pkg 集：time/duration.kv → {time, time/duration}。
        let mut pkgs: std::collections::BTreeSet<String> = std::collections::BTreeSet::new();
        for (name, _) in crate::stdlib::EMBEDDED_KV {
            let pkg = name.trim_end_matches(".kv");
            let mut acc = String::new();
            for seg in pkg.split('/') {
                if !acc.is_empty() {
                    acc.push('/');
                }
                acc.push_str(seg);
                pkgs.insert(acc.clone());
            }
        }
        let mut pkgs: Vec<String> = pkgs.into_iter().collect();
        pkgs.sort_by_key(|p| (p.matches('/').count(), p.clone())); // 父 pkg 先于子 pkg
        for pkg in pkgs {
            let initfn = format!("{pkg}\u{b7}init");
            if self.get_kv(&format!("/lib/{initfn}.src")).is_empty() {
                continue; // 纯 rwfunc lib 无 init
            }
            self.run_fn(&initfn);
            self.del_tree(&format!("/lib/{initfn}")); // 帧子树 /lib/<pkg>·init/*
            self.del(&format!("/lib/{initfn}.src")); // .src 叶（同步更新 pkg 成员索引）
        }
    }

    /// bootstrap 一个已入库函数并主导驱动其 vthread 到结束（就地 dispatch 纯净 rwir）。best-effort。
    pub fn run_fn(&self, funcname: &str) {
        let vid =
            take(unsafe { kvlangRuntimeBootstrap(self.rt, cs(funcname).as_ptr(), null_mut(), 0) });
        if vid.is_empty() {
            eprintln!("kvlang: bootstrap {funcname} 失败");
            return;
        }
        let vpc = format!("/vthread/{vid}/\u{2025}pc");
        loop {
            let mut pc: *mut c_char = null_mut();
            let rc = unsafe { kvlangRuntimeExecuteVthread(self.rt, cs(&vid).as_ptr(), &mut pc) };
            if rc == 0 {
                break; // done
            }
            if rc != 1 {
                let st = self.get_kv(&format!("/vthread/{vid}/\u{2025}status"));
                let msg = self.get_kv(&format!("/vthread/{vid}/\u{2025}error/msg"));
                eprintln!("kvlang: vthread {vid} 错误 rc={rc} {st}: {msg}");
                break;
            }
            let c = take(pc);
            let params = take(unsafe { kvlang_rwirextParams(self.kv, cs(&c).as_ptr()) });
            let op = params.lines().next().unwrap_or("").to_string();
            let handled = if rwir::is_inproc(&op) {
                rwir::dispatch(self, &op, &c);
                true
            } else if let Some(f) = self.ext {
                f(self, &op, &c)
            } else {
                false
            };
            if !handled {
                eprintln!("kvlang: 未知 rwir: {op} @ {c}");
            }
            let nxt = take(unsafe { kvlang_rwirextNextPc(cs(&c).as_ptr()) });
            self.set_kv(&vpc, &nxt);
        }
    }
}
