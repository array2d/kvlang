//! KVSpace access and rwir primitives.

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
    // ── kvspace 读写（绝对路径，char/utf8 与 char/utf32 互转）─────────
    /// 通用类型化写入：任意 kind + body 字节 + dims（空=标量 ndim0）。
    /// set_kv 与各 rwir 的类型化输出共用，避免每种 kind 一个专用写函数。
    /// 完整 langtype（含 [dims]）拼入值 attr 区（#571）——结果槽
    /// （math·sin/json/http 输出）跨 Handoff/println 读依赖类型。
    pub fn set_xvalue_wire(&self, key: &str, kind: &str, raw: &[u8], dims: &[i32]) {
        let value = xvalue_to_wire(kind, raw, dims);
        assert!(!value.is_empty(), "invalid XValue for {key}");
        // 系统键（末段含 ‥：status/pc/‥lib）无类型保留空 attr；其余填 langtype 记录。
        let last_seg = key.rsplit('/').next().unwrap_or(key);
        let wire = if last_seg.contains('\u{2025}') {
            value
        } else {
            with_langtype(&value, &full_langtype(kind, dims))
        };
        self.set_xvalue(key, &wire);
    }
    /// 普通文本值（含中文）：utf32 编码 + 写 langtype 记录。
    pub fn set_kv(&self, key: &str, val: &str) {
        let utf32: Vec<u32> = val.chars().map(|c| c as u32).collect();
        let raw: Vec<u8> = utf32.iter().flat_map(|v| v.to_le_bytes()).collect();
        self.set_xvalue_wire(key, "char/utf32", &raw, &[utf32.len() as i32]);
    }

    /// 系统文本（`‥pc`/`‥status` 等，末段含 ‥）：纯字节 SLACK、不写 langtype 记录
    /// （保留 codec 空 attr）——对齐 runtime 的 write_text（read_text 对无类型值
    /// 按纯字节文本读）。
    pub fn set_sys_text(&self, key: &str, val: &str) {
        let value = xvalue_to_wire("char/utf8", val.as_bytes(), &[val.len() as i32]);
        self.set_xvalue(key, &value);
    }
    /// key 是否存在（不取值；容器类值也能判）。
    pub fn kv_exists(&self, key: &str) -> bool {
        unsafe {
            let (mut out, mut olen) = (null_mut(), 0u32);
            let rc = kvspaceGet(self.kv, cs(key).as_ptr(), 0, &mut out, &mut olen);
            rc == 0 && !out.is_null()
        }
    }
    pub fn get_kv(&self, key: &str) -> String {
        unsafe {
            let (mut out, mut olen) = (null_mut(), 0u32);
            kvspaceGet(self.kv, cs(key).as_ptr(), 0, &mut out, &mut olen);
            if out.is_null() || olen == 0 {
                return String::new();
            }
            let d = std::slice::from_raw_parts(out, olen as usize);
            let kx = crate::ffi::langtype_of(self.kv, key);
            let body = xh_body_slice(d);
            if kx.ends_with("char/utf32") {
                body.chunks_exact(4)
                    .map(|c| {
                        char::from_u32(u32::from_le_bytes([c[0], c[1], c[2], c[3]]))
                            .unwrap_or('\u{FFFD}')
                    })
                    .collect()
            } else {
                String::from_utf8_lossy(body).into_owned()
            }
        }
    }

    pub fn set_ext_handle(&self, key: &str, target_langtype: &str, locator: &str) {
        // #571：@ext 句柄 = NewExt（body=定位串）；langtype 拼入值 attr 区。
        unsafe {
            let mut out = null_mut();
            let mut len = 0u64;
            let rc = kvspaceXvalueHeadNewExt(
                locator.as_ptr(),
                locator.len() as u32,
                cs(target_langtype).as_ptr(),
                &mut out,
                &mut len,
            );
            assert_eq!(rc, 0, "invalid ext locator for {key}");
            let value = std::slice::from_raw_parts(out, len as usize).to_vec();
            libc::free(out as *mut c_void);
            self.set_xvalue(key, &value);
        }
    }

    /// 读 key 的 head，返回 (ref, body 串)。仅 ref==2 时 body 有意义（扩展句柄定位串）。
    fn head_ref_body(&self, key: &str) -> (i32, String) {
        let xvalue = self.get_xvalue(key);
        if xvalue.is_empty() {
            return (0, String::new());
        }
        let r = if xh_is_ptr(&xvalue) {
            1
        } else if xh_is_ext(&xvalue) {
            2
        } else {
            0
        };
        if r != 2 {
            return (r, String::new());
        }
        let body = String::from_utf8_lossy(xh_body_slice(&xvalue)).into_owned();
        (r, body)
    }

    /// 扩展句柄兑现：按 body 前缀路由 —— /networld/{host}/proc/... → 进程内 proc 兑现器
    /// （逻辑路径 → /tmp/byteseek/{pid}/… 物理文件 → 读回字节）。其余前缀暂原样退化。
    fn resolve_ext_bytes(&self, locator: &str) -> Vec<u8> {
        if locator.starts_with("/networld/") && locator.contains("/proc/") {
            return rwir::networld::proc::resolve_read(locator);
        }
        locator.as_bytes().to_vec()
    }
    fn resolve_ext(&self, locator: &str) -> String {
        String::from_utf8_lossy(&self.resolve_ext_bytes(locator)).into_owned()
    }

    /// 读一个 int64 读参：ValueString 输出十进制串，直接 parse（缺槽/空串退 0）。
    pub fn read_i64(&self, pc: &str, idx: i32) -> i64 {
        self.read_at(pc, idx).trim().parse().unwrap_or(0)
    }

    pub fn read_f64(&self, pc: &str, idx: i32) -> f64 {
        let p = take(unsafe { kvlangRwirextResolveReadPath(self.kv, cs(pc).as_ptr(), idx) });
        if !p.is_empty() {
            let xv = self.get_xvalue(&p);
            if !xv.is_empty() && !xh_is_ptr(&xv) && xh_storetype(&xv) == 0 {
                let body = xh_body_slice(&xv);
                match (crate::ffi::langtype_of(self.kv, &p).as_str(), body.len()) {
                    ("float64", 8) => return f64::from_le_bytes(body.try_into().unwrap()),
                    ("float32", 4) => return f32::from_le_bytes(body.try_into().unwrap()) as f64,
                    ("int64", 8) => return i64::from_le_bytes(body.try_into().unwrap()) as f64,
                    _ => {}
                }
            }
        }
        self.read_at(pc, idx).trim().parse().unwrap_or(0.0)
    }

    // ── rwir 派发时按下标解析读/写槽（rwirext 宿主 ABI，传 kvspace 句柄）─
    /// @-aware 读参：@ 句柄按 body 前缀路由兑现真实字节，其余（字面量/普通值/指针）沿用 C ResolveRead。
    pub fn read_at(&self, pc: &str, idx: i32) -> String {
        let p = take(unsafe { kvlangRwirextResolveReadPath(self.kv, cs(pc).as_ptr(), idx) });
        if !p.is_empty() {
            let (r, body) = self.head_ref_body(&p);
            if r == 2 {
                return self.resolve_ext(&body);
            }
        }
        take(unsafe { kvlangRwirextResolveRead(self.kv, cs(pc).as_ptr(), idx) })
    }
    pub fn read0(&self, pc: &str) -> String {
        self.read_at(pc, 0)
    }
    pub fn write_at(&self, pc: &str, idx: i32) -> String {
        take(unsafe { kvlangRwirextResolveWrite(self.kv, cs(pc).as_ptr(), idx) })
    }
    pub fn write0(&self, pc: &str) -> String {
        self.write_at(pc, 0)
    }
    /// Read the complete operand body, resolving external handles.
    pub fn read_bytes(&self, pc: &str, idx: i32) -> Vec<u8> {
        let p = take(unsafe { kvlangRwirextResolveReadPath(self.kv, cs(pc).as_ptr(), idx) });
        if p.is_empty() {
            return self.read_at(pc, idx).into_bytes();
        }
        let xvalue = self.get_xvalue(&p);
        if xvalue.is_empty() {
            return Vec::new();
        }
        let r = if xh_is_ptr(&xvalue) {
            1
        } else if xh_is_ext(&xvalue) {
            2
        } else {
            0
        };
        let body = xh_body_slice(&xvalue);
        if r == 2 {
            return self.resolve_ext_bytes(&String::from_utf8_lossy(body));
        }
        body.to_vec()
    }

    pub fn set_str_list(&self, dst: &str, items: &[String]) {
        self.set_xvalue_wire(dst, "[int64]·[]char/utf32", &[], &[]);
        for (i, it) in items.iter().enumerate() {
            self.set_kv(&format!("{dst}\u{b7}[{i}]"), it);
        }
    }

    // ── kvspace 结构操作（json/http 扩展遍历子树用）────────────────────
    /// 前缀遍历：先 ListLen 定计数，再逐 idx ListAt 取名（借用回收缓冲，读出即自持），
    /// 不经一次性整段名单缓冲。
    pub fn list_kv(&self, prefix: &str) -> Vec<String> {
        unsafe {
            let cp = cs(prefix);
            let mut count = 0i32;
            if kvspaceListLen(self.kv, cp.as_ptr(), 0, 0, &mut count) != 0 || count <= 0 {
                return Vec::new();
            }
            let mut v = Vec::with_capacity(count as usize);
            for i in 0..count {
                let mut buf = [0u8; 1024];
                let mut olen = 0u32;
                if kvspaceListAt(
                    self.kv,
                    cp.as_ptr(),
                    0,
                    0,
                    i,
                    buf.as_mut_ptr(),
                    buf.len() as u32,
                    &mut olen,
                ) == 0
                    && olen > 0
                {
                    v.push(String::from_utf8_lossy(&buf[..olen as usize]).into_owned());
                }
            }
            v
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
    pub fn get_xvalue(&self, key: &str) -> Vec<u8> {
        unsafe {
            let (mut out, mut olen) = (null_mut(), 0u32);
            kvspaceGet(self.kv, cs(key).as_ptr(), 0, &mut out, &mut olen);
            if out.is_null() || olen == 0 {
                return Vec::new();
            }
            std::slice::from_raw_parts(out, olen as usize).to_vec()
        }
    }
    pub fn set_xvalue(&self, key: &str, xvalue: &[u8]) {
        if xvalue.is_empty() {
            self.del(key);
            return;
        }
        unsafe {
            let mut err = [0u8; 256];
            let rc = kvspaceSetValue(
                self.kv,
                cs(key).as_ptr(),
                xvalue.as_ptr(),
                xvalue.len() as u32,
                0,
                0,
                err.as_mut_ptr() as *mut c_char,
                err.len() as u32,
            );
            assert_eq!(rc, 0, "kvspace set {key}: {}", cbuf(&err));
        }
    }

    pub fn register(&self) {
        rwir::register(self);
    }

    /// 把单段内存源码 layout 进 kvspace，返回其写入的 init 函数名列表（entry_out，\n 分隔，
    /// pkg 严格取自源码 `lib` 声明）。失败打印错误并返回空。幂等、best-effort。
    pub fn layout_src(&self, name: &str, src: &str) -> Vec<String> {
        match self.layout_src_try(name, src) {
            Ok(v) => v,
            Err(e) => {
                crate::elog!("layout {name} 失败: {e}");
                Vec::new()
            }
        }
    }

    fn layout_src_try(&self, name: &str, src: &str) -> Result<Vec<String>, String> {
        let mut entry = [0u8; 4096];
        let mut err = [0u8; 4096];
        let rc = unsafe {
            kvlangLayoutCode(
                cs(src).as_ptr(),
                cs(&self.dsn).as_ptr(),
                entry.as_mut_ptr() as *mut c_char,
                entry.len() as u32,
                err.as_mut_ptr() as *mut c_char,
                err.len() as u32,
            )
        };
        if rc != 0 {
            return Err(cbuf(&err));
        }
        Ok(cbuf(&entry)
            .split('\n')
            .filter(|s| !s.is_empty())
            .map(str::to_string)
            .collect())
    }

    /// layout 一组 (名, 源码) 库进 kvspace（stdlib 与 `--lib` 共用），汇总各库的 init 函数名。
    /// 多文件库允许前向引用：未解析的库在多轮中重试，直到无进展；仍失败才报错（硬失败）。
    pub fn layout_libs<S: AsRef<str>>(&self, libs: &[(S, S)]) -> Vec<String> {
        let mut inits = Vec::new();
        let mut pending: Vec<usize> = (0..libs.len()).collect();
        // 前向引用重试：最多 libs.len() 轮；每轮至少一个库成功才继续，否则停。
        let mut last_errs: Vec<(String, String)> = Vec::new();
        for _round in 0..libs.len().max(1) {
            if pending.is_empty() {
                last_errs.clear();
                break;
            }
            let mut next = Vec::new();
            let mut errs: Vec<(String, String)> = Vec::new();
            for &i in &pending {
                let name = libs[i].0.as_ref();
                match self.layout_src_try(name, libs[i].1.as_ref()) {
                    Ok(v) => inits.extend(v),
                    Err(e) => {
                        next.push(i);
                        errs.push((name.to_string(), e));
                    }
                }
            }
            let no_progress = next.len() == pending.len();
            pending = next;
            last_errs = errs;
            if no_progress {
                break;
            }
        }
        for (name, e) in last_errs {
            crate::elog!("layout {name} 失败: {e}");
        }
        inits
    }
    pub fn layout_stdlib(&self) -> Vec<String> {
        self.layout_libs(crate::stdlib::EMBEDDED_KV)
    }

    /// 运行 layout 返回的一组 init 函数（与 layout 分离）：按 pkg 深度升序（父先于子）run，
    /// run 完即删（一次性引导）。init 名由 layout 按 `lib` 声明给出（`X·init`/`init`），
    /// 不做文件系统路径推导。常量成员式（如 /lib/math·Pi）在 init 落值；`--lib` 库
    /// （如 byteseek，把 config/种子/REPL 编排进 <pkg>·init）同款引导。
    pub fn run_inits(&self, inits: &[String]) {
        let mut inits: Vec<String> = inits.to_vec();
        inits.sort();
        inits.dedup();
        inits.sort_by_key(|f| f.matches('/').count()); // 父 pkg 先于子 pkg
        for initfn in inits {
            if self.get_kv(&format!("/lib/{initfn}.src")).is_empty() {
                continue;
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
            crate::elog!("bootstrap {funcname} 失败");
            return;
        }
        self.run_vid(&vid);
    }

    /// 主导驱动一个已 bootstrap 的 vid 从其持久化 pc 跑到结束（不 bootstrap、不 close kv，可重入）。
    /// vthread·run rwir 与 run_fn 共用：前者驱动 create 出的独立子 vid，后者驱动 init。
    pub fn run_vid(&self, vid: &str) {
        loop {
            let mut pc: *mut c_char = null_mut();
            let rc = unsafe { kvlangRuntimeExecuteVthread(self.rt, cs(vid).as_ptr(), &mut pc) };
            if rc == 0 {
                break; // done
            }
            if rc != 1 {
                let st = self.get_kv(&format!("/vthread/{vid}/\u{2025}status"));
                let msg = self.get_kv(&format!("/vthread/{vid}/\u{2025}msg"));
                crate::elog!("vthread {vid} 错误 rc={rc} {st}: {msg}");
                break;
            }
            let c = take(pc);
            let params = take(unsafe { kvlangRwirextParams(self.kv, cs(&c).as_ptr()) });
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
                crate::elog!("未知 rwir: {op} @ {c}");
            }
            let nxt = take(unsafe { kvlangRwirextNextPc(cs(&c).as_ptr()) });
            // pc 可能属子 vthread（native vthread·run 冒泡上来）：nextpc 写回其所属 vid，不写主 vid。
            let sub = c.split('/').nth(2).unwrap_or(vid);
            self.set_sys_text(&format!("/vthread/{sub}/\u{2025}pc"), &nxt);
        }
    }
}
