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
    /// 写即构造：按 (kindexpr, body) 向 kvspace 要偏移指针后直接写 body 字节——
    /// key 已存在且同 body_len → WriteInPlace（原 box 就地）；否则 WriteNewPlace（新 box）。
    /// 两分支各调唯一原语、无预 encode 整条 TLV、无中转 buffer、无 free。
    fn write_construct(&self, key: &str, kindexpr: &str, body: &[u8]) {
        unsafe {
            let ck = cs(key);
            let mut bp: *mut u8 = null_mut();
            let mut err = [0u8; 256];
            let rc = kvspaceWriteInPlace(
                self.kv,
                ck.as_ptr(),
                1,
                body.len() as u32,
                &mut bp,
                err.as_mut_ptr() as *mut c_char,
                256,
            );
            if rc != 0 {
                kvspaceWriteNewPlace(
                    self.kv,
                    ck.as_ptr(),
                    cs(kindexpr).as_ptr(),
                    body.len() as u32,
                    &mut bp,
                    err.as_mut_ptr() as *mut c_char,
                    256,
                );
            }
            if !body.is_empty() && !bp.is_null() {
                std::ptr::copy_nonoverlapping(body.as_ptr(), bp, body.len());
            }
        }
    }
    /// 通用类型化写入：任意 kind + body 字节 + dims（空=标量 ndim0）。
    /// set_kv 与各 rwir 的类型化输出共用，避免每种 kind 一个专用写函数。
    pub fn set_tlv_encoded(&self, key: &str, kind: &str, raw: &[u8], dims: &[i32]) {
        self.set_tlv(key, &tlv_encode(kind, raw, dims));
    }
    pub fn set_kv(&self, key: &str, val: &str) {
        let utf32: Vec<u32> = val.chars().map(|c| c as u32).collect();
        let raw: Vec<u8> = utf32.iter().flat_map(|v| v.to_le_bytes()).collect();
        self.set_tlv_encoded(key, "char/utf32", &raw, &[utf32.len() as i32]);
    }
    pub fn get_kv(&self, key: &str) -> String {
        unsafe {
            let (mut out, mut olen) = (null_mut(), 0u32);
            kvspaceGet(self.kv, cs(key).as_ptr(), 0, &mut out, &mut olen);
            if out.is_null() || olen == 0 {
                return String::new();
            }
            let mut head = KvspaceHead::default();
            kvspaceDecodeHead(out, olen, &mut head);
            let kx = String::from_utf8_lossy(&head.kindexpr)
                .trim_end_matches('\0')
                .to_string();
            let (bo, bl) = (head.body_offset as usize, head.body_len.max(0) as usize);
            if kx.ends_with("char/utf32") {
                std::slice::from_raw_parts(out.add(bo), bl)
                    .chunks_exact(4)
                    .map(|c| {
                        char::from_u32(u32::from_le_bytes([c[0], c[1], c[2], c[3]]))
                            .unwrap_or('\u{FFFD}')
                    })
                    .collect()
            } else {
                String::from_utf8_lossy(std::slice::from_raw_parts(out.add(bo), bl)).into_owned()
            }
        }
    }

    /// 扩展世界（@ ref=2）句柄编码写入：kind=目标完整 kindexpr（如 "[]uint8"），body=定位串。
    /// 读取该 key 时由 read_at 按 body 前缀路由给对应 /lib/networld/* 兑现器还原真实字节。
    pub fn set_ext_handle(&self, key: &str, target_kindexpr: &str, locator: &str) {
        self.write_construct(key, &format!("@{target_kindexpr}"), locator.as_bytes());
    }

    /// 读 key 的 head，返回 (ref, body 串)。仅 ref==2 时 body 有意义（扩展句柄定位串）。
    fn head_ref_body(&self, key: &str) -> (i32, String) {
        let tlv = self.get_tlv(key);
        if tlv.is_empty() {
            return (0, String::new());
        }
        unsafe {
            let mut head = KvspaceHead::default();
            kvspaceDecodeHead(tlv.as_ptr(), tlv.len() as u32, &mut head);
            let r = match head.kindexpr[0] {
                b'@' => 2,
                b'*' => 1,
                _ => 0,
            };
            if r != 2 {
                return (r, String::new());
            }
            let bo = head.body_offset as usize;
            let bl = head.body_len.max(0) as usize;
            let body = String::from_utf8_lossy(&tlv[bo..bo + bl]).into_owned();
            (r, body)
        }
    }

    /// 扩展句柄兑现：按 body 前缀路由 —— /networld/{host}/proc/... → 进程内 proc 兑现器
    /// （逻辑路径 → /tmp/kvlangruntime-rs/{pid}/… 物理文件 → 读回字节）。其余前缀暂原样退化。
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

    // ── rwir 派发时按下标解析读/写槽（rwirext 宿主 ABI，传 kvspace 句柄）─
    /// @-aware 读参：@ 句柄按 body 前缀路由兑现真实字节，其余（字面量/普通值/指针）沿用 C ResolveRead。
    pub fn read_at(&self, pc: &str, idx: i32) -> String {
        let p = take(unsafe { kvlang_rwirextResolveReadPath(self.kv, cs(pc).as_ptr(), idx) });
        if !p.is_empty() {
            let (r, body) = self.head_ref_body(&p);
            if r == 2 {
                return self.resolve_ext(&body);
            }
        }
        take(unsafe { kvlang_rwirextResolveRead(self.kv, cs(pc).as_ptr(), idx) })
    }
    pub fn read0(&self, pc: &str) -> String {
        self.read_at(pc, 0)
    }
    pub fn write_at(&self, pc: &str, idx: i32) -> String {
        take(unsafe { kvlang_rwirextResolveWrite(self.kv, cs(pc).as_ptr(), idx) })
    }
    pub fn write0(&self, pc: &str) -> String {
        self.write_at(pc, 0)
    }
    /// @-aware 读参整块原始字节（read_at 对数组只返首元素，取字节须走容器路径 + TLV body）：
    /// 取读参 idx 容器路径 → 读 body 字节；@ 句柄按前缀兑现真实字节；无路径退化为 read_at 的 utf8。
    /// 供 fs·write/append 等需要 []uint8 整块的 rwir 用。
    pub fn read_bytes(&self, pc: &str, idx: i32) -> Vec<u8> {
        let p = take(unsafe { kvlang_rwirextResolveReadPath(self.kv, cs(pc).as_ptr(), idx) });
        if p.is_empty() {
            return self.read_at(pc, idx).into_bytes();
        }
        let tlv = self.get_tlv(&p);
        if tlv.is_empty() {
            return Vec::new();
        }
        unsafe {
            let mut h = KvspaceHead::default();
            if kvspaceDecodeHead(tlv.as_ptr(), tlv.len() as u32, &mut h) != 0 {
                return Vec::new();
            }
            let r = match h.kindexpr[0] {
                b'@' => 2,
                b'*' => 1,
                _ => 0,
            };
            let (bo, bl) = (h.body_offset as usize, h.body_len.max(0) as usize);
            if bo + bl > tlv.len() {
                return Vec::new();
            }
            if r == 2 {
                let body = String::from_utf8_lossy(&tlv[bo..bo + bl]).into_owned();
                return self.resolve_ext_bytes(&body);
            }
            tlv[bo..bo + bl].to_vec()
        }
    }

    /// 写一个 stringkeymap 容器（对齐 C kv·list 表示，供 fs·list 等返回可 for-in 的字符串列表）：
    ///   dst   = 容器标记：kind=stringkeymap，body 空，dims=[n]
    ///   dst·  = memindex：kind=index，body=[4B count LE]["[0]\n[1]\n..."]（成员坐标名唯一权威）
    ///   dst·[i] = 各成员字符串（char/utf32）
    pub fn set_str_list(&self, dst: &str, items: &[String]) {
        let n = items.len();
        self.set_tlv_encoded(dst, "stringkeymap", &[], &[n as i32]);
        let mut body = (n as u32).to_le_bytes().to_vec();
        for i in 0..n {
            if i > 0 {
                body.push(b'\n');
            }
            body.extend_from_slice(format!("[{i}]").as_bytes());
        }
        self.set_tlv_encoded(&format!("{dst}\u{b7}"), "index", &body, &[]);
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
                let (mut out, mut olen) = (null_mut(), 0u32);
                if kvspaceListAt(self.kv, cp.as_ptr(), 0, 0, i, &mut out, &mut olen) == 0
                    && !out.is_null()
                    && olen > 0
                {
                    v.push(
                        String::from_utf8_lossy(std::slice::from_raw_parts(out, olen as usize))
                            .into_owned(),
                    );
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
    pub fn get_tlv(&self, key: &str) -> Vec<u8> {
        unsafe {
            let (mut out, mut olen) = (null_mut(), 0u32);
            kvspaceGet(self.kv, cs(key).as_ptr(), 0, &mut out, &mut olen);
            if out.is_null() || olen == 0 {
                return Vec::new();
            }
            std::slice::from_raw_parts(out, olen as usize).to_vec()
        }
    }
    /// 写预编码 TLV：解 head 取 (kindexpr, body) 后走写即构造（新建/换 kind/换尺寸唯一原语）。
    pub fn set_tlv(&self, key: &str, tlv: &[u8]) {
        if tlv.is_empty() {
            return;
        }
        unsafe {
            let mut head = KvspaceHead::default();
            if kvspaceDecodeHead(tlv.as_ptr(), tlv.len() as u32, &mut head) != 0 {
                return;
            }
            let kx = String::from_utf8_lossy(&head.kindexpr)
                .trim_end_matches('\0')
                .to_string();
            let (bo, bl) = (head.body_offset as usize, head.body_len.max(0) as usize);
            self.write_construct(key, &kx, &tlv[bo..bo + bl]);
        }
    }

    pub fn register(&self) {
        rwir::register(self);
    }

    /// 把单段内存源码 layout 进 kvspace，返回其写入的 init 函数名列表（entry_out，\n 分隔，
    /// pkg 严格取自源码 `lib` 声明）。失败打印错误并返回空。幂等、best-effort。
    pub fn layout_src(&self, name: &str, src: &str) -> Vec<String> {
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
            crate::elog!("layout {name} 失败: {}", cbuf(&err));
            return Vec::new();
        }
        cbuf(&entry)
            .split('\n')
            .filter(|s| !s.is_empty())
            .map(str::to_string)
            .collect()
    }

    /// layout 一组 (名, 源码) 库进 kvspace（stdlib 与 KVLANG_LIB 共用），汇总各库的 init 函数名。
    pub fn layout_libs<S: AsRef<str>>(&self, libs: &[(S, S)]) -> Vec<String> {
        let mut inits = Vec::new();
        for (name, src) in libs {
            inits.extend(self.layout_src(name.as_ref(), src.as_ref()));
        }
        inits
    }
    pub fn layout_stdlib(&self) -> Vec<String> {
        self.layout_libs(crate::stdlib::EMBEDDED_KV)
    }

    /// 运行 layout 返回的一组 init 函数（与 layout 分离）：按 pkg 深度升序（父先于子）run，
    /// run 完即删（一次性引导）。init 名由 layout 按 `lib` 声明给出（`X·init`/`init`），
    /// 不做文件系统路径推导。常量成员式（如 /lib/math·Pi）在 init 落值；KVLANG_LIB 库
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
                let msg = self.get_kv(&format!("/vthread/{vid}/\u{2025}error/msg"));
                crate::elog!("vthread {vid} 错误 rc={rc} {st}: {msg}");
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
                crate::elog!("未知 rwir: {op} @ {c}");
            }
            let nxt = take(unsafe { kvlang_rwirextNextPc(cs(&c).as_ptr()) });
            // pc 可能属子 vthread（native vthread·run 冒泡上来）：nextpc 写回其所属 vid，不写主 vid。
            let sub = c.split('/').nth(2).unwrap_or(vid);
            self.set_kv(&format!("/vthread/{sub}/\u{2025}pc"), &nxt);
        }
    }
}
