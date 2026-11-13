
use std::ffi::{c_char, c_void};
use std::ptr::null_mut;

use crate::ffi::*;
use crate::rwir;

pub struct Engine {
    pub rt: *mut c_void, 
    pub kv: *mut c_void, 
    pub dsn: String,     
    
    pub ext: Option<fn(&Engine, op: &str, pc: &str) -> bool>,
}

pub type KvResult<T = ()> = Result<T, String>;

fn pc_vid<'a>(pc: &'a str, dflt: &'a str) -> &'a str {
    pc.split('/').nth(2).filter(|s| !s.is_empty()).unwrap_or(dflt)
}

impl Engine {
    
    fn read_wire(&self, key: &str) -> Option<Vec<u8>> {
        let ck = cs(key);
        let mut off: u64 = 0;
        let mut err = [0u8; 256];
        let rc = unsafe {
            pathspaceResolveOffset(
                self.kv, ck.as_ptr(), 0, &mut off,
                err.as_mut_ptr() as *mut c_char, err.len() as u32,
            )
        };
        if rc != 0 {
            return None;
        }
        let base = unsafe { pathspaceXvalueInProcessAddress(self.kv) };
        if base.is_null() {
            return None;
        }
        unsafe {
            let wire = base.add(off as usize);
            
            let bodylen = u64::from_le_bytes(*(wire as *const [u8; 8]));
            let attrslen = u16::from_le_bytes(*(wire.add(8) as *const [u8; 2])) as u64;
            let total = (HEAD_LEN as u64).checked_add(bodylen)?.checked_add(attrslen)?;
            Some(std::slice::from_raw_parts(wire, total as usize).to_vec())
        }
    }

    fn write_wire(&self, key: &str, wire: &[u8]) -> KvResult {
        if wire.len() < HEAD_LEN {
            return Err(format!("pathspace set {key}: wire shorter than head"));
        }
        let bodylen = u64::from_le_bytes(wire[0..8].try_into().unwrap());
        let attrslen = u16::from_le_bytes(wire[8..10].try_into().unwrap()) as u64;
        let total = (HEAD_LEN as u64)
            .checked_add(bodylen)
            .and_then(|n| n.checked_add(attrslen))
            .ok_or_else(|| format!("pathspace set {key}: wire length overflow"))?;
        if total != wire.len() as u64 {
            return Err(format!(
                "pathspace set {key}: wire length {} != head+body+attr {total}",
                wire.len()
            ));
        }
        let ck = cs(key);
        let mut off: u64 = 0;
        let mut err = [0u8; 256];
        let rc = unsafe {
            pathspaceRealloc(
                self.kv, ck.as_ptr(), bodylen, attrslen, &mut off,
                err.as_mut_ptr() as *mut c_char, err.len() as u32,
            )
        };
        if rc != 0 {
            return Err(format!("pathspace set {key}: {}", cbuf(&err)));
        }
        
        unsafe {
            let base = pathspaceXvalueInProcessAddress(self.kv);
            if base.is_null() {
                return Err(format!("pathspace set {key}: null data base"));
            }
            let body_at = base.add(off as usize + HEAD_LEN);
            std::ptr::copy_nonoverlapping(wire[HEAD_LEN..].as_ptr(), body_at, (bodylen + attrslen) as usize);
        }
        Ok(())
    }

    pub fn list_kv(&self, prefix: &str) -> Vec<String> {
        let cp = cs(prefix);
        let mut count: i64 = 0;
        let mut maxlen: u32 = 0;
        let mut err = [0u8; 256];
        let rc = unsafe {
            pathspaceListlen(
                self.kv, cp.as_ptr(), &mut count, &mut maxlen,
                err.as_mut_ptr() as *mut c_char, err.len() as u32,
            )
        };
        if rc != 0 || count <= 0 {
            return Vec::new();
        }
        
        let cap = (count as u64).saturating_mul(maxlen as u64 + 1);
        let Ok(cap) = u32::try_from(cap) else {
            return Vec::new();
        };
        let mut buf = vec![0u8; cap as usize];
        let mut length: u32 = 0;
        let mut got: u32 = 0;
        let mut generation: u64 = 0;
        let rc = unsafe {
            pathspaceList(
                self.kv, cp.as_ptr(), buf.as_mut_ptr() as *mut c_char, cap,
                &mut length, &mut got, &mut generation,
                err.as_mut_ptr() as *mut c_char, err.len() as u32,
            )
        };
        if rc != 0 {
            return Vec::new();
        }
        buf[..length as usize]
            .split(|&b| b == 0)
            .filter(|n| !n.is_empty())
            .map(|n| String::from_utf8_lossy(n).into_owned())
            .collect()
    }

    pub fn set_xvalue_wire(&self, key: &str, kind: &str, raw: &[u8], dims: &[i32]) -> KvResult {
        let value = xvalue_to_wire(kind, raw, dims);
        if value.is_empty() {
            return Err(format!(
                "invalid XValue for {key} (kind={kind}, {} bytes)",
                raw.len()
            ));
        }
        
        let last_seg = key.rsplit('/').next().unwrap_or(key);
        let wire = if last_seg.contains('\u{2025}') {
            xvalue_to_wire_untyped(kind, raw, dims)
        } else {
            value
        };
        self.set_xvalue(key, &wire)
    }
    
    pub fn set_kv(&self, key: &str, val: &str) -> KvResult {
        let utf32: Vec<u32> = val.chars().map(|c| c as u32).collect();
        let raw: Vec<u8> = utf32.iter().flat_map(|v| v.to_le_bytes()).collect();
        let n = i32::try_from(utf32.len()).map_err(|_| format!("text too long for {key}"))?;
        self.set_xvalue_wire(key, "char/utf32", &raw, &[n])
    }

    pub fn set_sys_text(&self, key: &str, val: &str) -> KvResult {
        let value = xvalue_to_wire("char/utf8", val.as_bytes(), &[val.len() as i32]);
        if value.is_empty() {
            return Err(format!("invalid XValue for {key}"));
        }
        self.set_xvalue(key, &value)
    }

    pub fn set_vthread_error(&self, vid: &str, pc: &str, msg: &str) {
        let root = format!("/vthread/{vid}/\u{2025}");
        let _ = self.set_sys_text(&format!("{root}pc"), pc);
        let _ = self.set_sys_text(&format!("{root}msg"), msg);
        let _ = self.set_sys_text(&format!("{root}status"), "error");
    }
    
    pub fn kv_exists(&self, key: &str) -> bool {
        self.read_wire(key).is_some()
    }
    pub fn get_kv(&self, key: &str) -> String {
        {
            
            let Some(d) = self.read_wire(key) else {
                return String::new();
            };
            let kx = crate::ffi::langtype_of(self.kv, key);
            let body = value_body(&d);
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

    pub fn set_ext_handle(&self, key: &str, target_langtype: &str, locator: &str) -> KvResult {
        
        let value = unsafe {
            let mut out = null_mut();
            let mut len = 0u64;
            let rc = pathlangXvalueHeadNewExt(
                locator.as_ptr(),
                locator.len() as u32,
                cs(target_langtype).as_ptr(),
                &mut out,
                &mut len,
            );
            if rc != 0 || out.is_null() || len == 0 {
                if !out.is_null() {
                    libc::free(out as *mut c_void);
                }
                return Err(format!("invalid ext locator for {key}: {locator}"));
            }
            let value = std::slice::from_raw_parts(out, len as usize).to_vec();
            libc::free(out as *mut c_void);
            value
        };
        self.set_xvalue(key, &value)
    }

    fn head_ref_body(&self, key: &str) -> (i32, String) {
        let xvalue = self.get_xvalue(key);
        if xvalue.is_empty() {
            return (0, String::new());
        }
        let r = if value_is_ptr(&xvalue) {
            1
        } else if value_is_ext(&xvalue) {
            2
        } else {
            0
        };
        if r != 2 {
            return (r, String::new());
        }
        let body = String::from_utf8_lossy(value_body(&xvalue)).into_owned();
        (r, body)
    }

    fn resolve_ext_bytes(&self, locator: &str) -> Vec<u8> {
        if locator.starts_with("/networld/") && locator.contains("/proc/") {
            return rwir::networld::proc::resolve_read(locator);
        }
        locator.as_bytes().to_vec()
    }
    fn resolve_ext(&self, locator: &str) -> String {
        String::from_utf8_lossy(&self.resolve_ext_bytes(locator)).into_owned()
    }

    pub fn read_i64(&self, pc: &str, idx: i32) -> i64 {
        self.read_at(pc, idx).trim().parse().unwrap_or(0)
    }

    pub fn read_f64(&self, pc: &str, idx: i32) -> f64 {
        let p = take(unsafe { pathlangRwirextResolveReadPath(self.kv, cs(pc).as_ptr(), idx) });
        if !p.is_empty() {
            let xv = self.get_xvalue(&p);
            if !xv.is_empty() && !value_is_ptr(&xv) && !value_is_ext(&xv) {
                let body = value_body(&xv);
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

    pub fn read_at(&self, pc: &str, idx: i32) -> String {
        let p = take(unsafe { pathlangRwirextResolveReadPath(self.kv, cs(pc).as_ptr(), idx) });
        if !p.is_empty() {
            let (r, body) = self.head_ref_body(&p);
            if r == 2 {
                return self.resolve_ext(&body);
            }
        }
        take(unsafe { pathlangRwirextResolveRead(self.kv, cs(pc).as_ptr(), idx) })
    }
    pub fn read0(&self, pc: &str) -> String {
        self.read_at(pc, 0)
    }
    pub fn write_at(&self, pc: &str, idx: i32) -> String {
        take(unsafe { pathlangRwirextResolveWrite(self.kv, cs(pc).as_ptr(), idx) })
    }
    pub fn write0(&self, pc: &str) -> String {
        self.write_at(pc, 0)
    }
    
    pub fn read_bytes(&self, pc: &str, idx: i32) -> Vec<u8> {
        let p = take(unsafe { pathlangRwirextResolveReadPath(self.kv, cs(pc).as_ptr(), idx) });
        if p.is_empty() {
            return self.read_at(pc, idx).into_bytes();
        }
        let xvalue = self.get_xvalue(&p);
        if xvalue.is_empty() {
            return Vec::new();
        }
        let r = if value_is_ptr(&xvalue) {
            1
        } else if value_is_ext(&xvalue) {
            2
        } else {
            0
        };
        let body = value_body(&xvalue);
        if r == 2 {
            return self.resolve_ext_bytes(&String::from_utf8_lossy(body));
        }
        body.to_vec()
    }

    pub fn set_str_list(&self, dst: &str, items: &[String]) -> KvResult {
        self.set_xvalue_wire(dst, "'[int64]'[]char/utf32", &[], &[])?;
        for (i, it) in items.iter().enumerate() {
            self.set_kv(&format!("{dst}\u{b7}[{i}]"), it)?;
        }
        Ok(())
    }

    pub fn del_tree(&self, prefix: &str) {
        unsafe {
            let mut err = [0u8; 256];
            
            pathspaceDeltree(
                self.kv,
                cs(prefix).as_ptr(),
                err.as_mut_ptr() as *mut c_char,
                256,
            );
        }
    }
    
    pub fn del(&self, key: &str) {
        unsafe {
            let mut err = [0u8; 256];
            pathspaceFree(
                self.kv,
                cs(key).as_ptr(),
                err.as_mut_ptr() as *mut c_char,
                256,
            );
        }
    }
    pub fn get_xvalue(&self, key: &str) -> Vec<u8> {
        self.read_wire(key).unwrap_or_default()
    }
    pub fn set_xvalue(&self, key: &str, xvalue: &[u8]) -> KvResult {
        if xvalue.is_empty() {
            self.del(key);
            return Ok(());
        }
        
        self.write_wire(key, xvalue)
    }

    pub fn register(&self) {
        rwir::register(self);
    }

    pub fn layout_src(&self, name: &str, src: &str) -> Vec<String> {
        match self.layout_src_try(src) {
            Ok(v) => v,
            Err(e) => {
                crate::elog!("layout {name} failed: {e}");
                Vec::new()
            }
        }
    }

    fn layout_src_try(&self, src: &str) -> Result<Vec<String>, String> {
        let mut entry = [0u8; 4096];
        let mut err = [0u8; 4096];
        let rc = unsafe {
            pathlangLayoutCode(
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

    pub fn layout_libs<S: AsRef<str>>(&self, libs: &[(S, S)]) -> Vec<String> {
        let mut inits = Vec::new();
        let mut pending: Vec<usize> = (0..libs.len()).collect();
        
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
                match self.layout_src_try(libs[i].1.as_ref()) {
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
            crate::elog!("layout {name} failed: {e}");
        }
        inits
    }
    pub fn layout_stdlib(&self) -> Vec<String> {
        self.layout_libs(crate::stdlib::EMBEDDED_KV)
    }

    pub fn run_inits(&self, inits: &[String]) {
        let mut inits: Vec<String> = inits.to_vec();
        inits.sort();
        inits.dedup();
        inits.sort_by_key(|f| f.matches('/').count()); 
        for initfn in inits {
            if self.get_kv(&format!("/lib/{initfn}.src")).is_empty() {
                continue;
            }
            
            if let Err(e) = self.run_fn(&initfn) {
                crate::elog!("init {initfn}: {e}");
            }
            self.del_tree(&format!("/lib/{initfn}")); 
            self.del(&format!("/lib/{initfn}.src")); 
        }
    }

    pub fn run_fn(&self, funcname: &str) -> KvResult {
        let vid =
            take(unsafe { pathlangRuntimeBootstrap(self.rt, cs(funcname).as_ptr(), null_mut(), 0) });
        if vid.is_empty() {
            return Err(format!("bootstrap {funcname} failed"));
        }
        self.drive_vid(&vid)
    }

    pub fn drive_vid(&self, vid: &str) -> KvResult {
        let mut guard_pc: Option<String> = None;
        let mut guard_st = String::new();
        let mut guard_n = 0u32;
        let mut guard_ran = false;

        loop {
            let mut pc: *mut c_char = null_mut();
            let rc = unsafe { pathlangRuntimeExecuteVthread(self.rt, cs(vid).as_ptr(), &mut pc) };
            if rc == 0 {
                return Ok(()); 
            }
            if rc != 1 {
                let st = self.get_kv(&format!("/vthread/{vid}/\u{2025}status"));
                let msg = self.get_kv(&format!("/vthread/{vid}/\u{2025}msg"));
                let st = if st.is_empty() { "error".into() } else { st };
                return Err(format!("vthread {vid} rc={rc} {st}: {msg}"));
            }

            let st_before = self.get_kv(&format!("/vthread/{vid}/\u{2025}status"));
            let c0 = take(pc);
            if st_before != "init" && st_before != "running" && st_before != "wait" {
                return Ok(());
            }

            let same =
                guard_pc.as_deref() == Some(c0.as_str()) && !guard_ran && guard_st == st_before;
            guard_n = if same { guard_n + 1 } else { 0 };
            guard_pc = Some(c0.clone());
            guard_st = st_before;
            guard_ran = false;
            if guard_n >= 3 {
                let op_at = self.op_at(&c0);
                let msg = self.get_kv(&format!("/vthread/{vid}/\u{2025}msg"));
                return Err(format!(
                    "op '{op_at}' not implemented by any runtime (Handoff): pc stalled \
                     3 times @ {c0} status={guard_st} {msg}"
                ));
            }

            let mut c = c0;
            let stop_op = loop {
                let op = self.op_at(&c);
                let handled = if rwir::is_inproc(&op) {
                    if let Err(e) = rwir::dispatch(self, &op, &c) {
                        let owner = pc_vid(&c, vid);
                        let msg = format!("{op}: {e}");
                        self.set_vthread_error(owner, &c, &msg);
                        return Err(format!("vthread {owner} error: {msg}"));
                    }
                    true
                } else if let Some(f) = self.ext {
                    f(self, &op, &c)
                } else {
                    false
                };
                if !handled {
                    break op;
                }
                guard_ran = true;
                let nxt = take(unsafe { pathlangRwirextNextPc(cs(&c).as_ptr()) });
                
                let nsub = pc_vid(&nxt, vid);
                self.set_sys_text(&format!("/vthread/{nsub}/\u{2025}pc"), &nxt)?;
                c = nxt;
            };

            let sub = pc_vid(&c, vid);

            let claimed =
                !stop_op.is_empty() && self.kv_exists(&format!("/lib/{stop_op}/vids"));
            if claimed {
                if unsafe { pathlangRwirextHandoff(self.kv, cs(sub).as_ptr(), cs(&c).as_ptr()) } != 0 {
                    return Err(format!("handoff {stop_op} failed @ {c}"));
                }
            } else {
                self.set_sys_text(&format!("/vthread/{sub}/\u{2025}pc"), &c)?;
            }
        }
    }

    fn op_at(&self, pc: &str) -> String {
        let params = take(unsafe { pathlangRwirextParams(self.kv, cs(pc).as_ptr()) });
        params.split('\n').next().unwrap_or("").to_string()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn init_with_unclaimed_rwir_errors_instead_of_skipping() {
        let base = std::env::temp_dir().join(format!("pathseek-drive-{}.shm", std::process::id()));
        let dsn = format!("shm://{}", base.display());
        let rt = unsafe { pathlangRuntimeConnect(cs(&dsn).as_ptr()) };
        assert!(!rt.is_null());
        let kv = unsafe { pathlangRuntimePathspaceHandle(rt) };
        let eng = Engine {
            rt,
            kv,
            dsn: dsn.clone(),
            ext: None,
        };
        eng.register();
        let rp = [cs("*")];
        let rpp = [rp[0].as_ptr()];
        let op = cs("drivetest·unclaimed");
        unsafe { pathlangDefRwir(kv, op.as_ptr(), rpp.as_ptr(), 1, std::ptr::null(), 0) };
        let src = "lib drivetest {\n\trwfunc init() -> () {\n\t\tdrivetest·unclaimed(1)\n\t}\n}\n";
        let inits = eng.layout_libs(&[("drivetest.kv", src)]);
        assert_eq!(inits, ["drivetest·init"]);
        let err = eng.run_fn(&inits[0]).unwrap_err();
        assert!(
            err.contains("'drivetest·unclaimed' not implemented by any runtime"),
            "{err}"
        );
        unsafe { pathlangRuntimeDisconnect(rt) };
        let b = base.display().to_string();
        for stem in [b.clone(), format!("{b}.isa")] {
            for suf in ["", ".sbo.data", ".sbo.head", ".garb", ".gen"] {
                let _ = std::fs::remove_file(format!("{stem}{suf}"));
            }
        }
    }
}
