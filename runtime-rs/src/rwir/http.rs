//! rwir `http·call`：网络抓取原语（rust 原生 ureq，不经 curl 子进程）。
//!   http·call(method, header, url, body) -> resp
//! header 为 "K: V\nK2: V2" 原文块，空串 = 无 header；body 空串 = 无请求体。

use std::time::Duration;

use crate::engine::Engine;
use crate::ffi::*;

pub fn call(eng: &Engine, pc: &str) {
    let method = eng.read0(pc);
    let header = read(eng, pc, 1);
    let url = read(eng, pc, 2);
    let body = read(eng, pc, 3);
    let out = request(&method, &header, &url, &body);
    eng.set_kv(&eng.write0(pc), &out);
}

fn read(eng: &Engine, pc: &str, idx: i32) -> String {
    take(unsafe { kvlang_rwirextResolveRead(eng.kv, cs(pc).as_ptr(), idx) })
}

fn agent() -> ureq::Agent {
    let mut b = ureq::AgentBuilder::new().timeout(Duration::from_secs(30));
    for key in ["https_proxy", "HTTPS_PROXY", "http_proxy", "HTTP_PROXY"] {
        if let Ok(p) = std::env::var(key) {
            if let Ok(proxy) = ureq::Proxy::new(&p) {
                b = b.proxy(proxy);
                break;
            }
        }
    }
    b.build()
}

fn request(method: &str, header: &str, url: &str, body: &str) -> String {
    let mut req = agent().request(method, url);
    for line in header.lines() {
        let line = line.trim();
        if let Some((k, v)) = line.split_once(':') {
            req = req.set(k.trim(), v.trim());
        }
    }
    let resp = if body.is_empty() {
        req.call()
    } else {
        req.send_string(body)
    };
    match resp {
        Ok(r) => {
            let code = r.status();
            let text = r.into_string().unwrap_or_default();
            if text.trim().is_empty() {
                format!("(空响应, status={code})")
            } else {
                text
            }
        }
        Err(ureq::Error::Status(code, r)) => {
            let text = r.into_string().unwrap_or_default();
            if text.trim().is_empty() {
                format!("(HTTP {code})")
            } else {
                format!("(HTTP {code}) {text}")
            }
        }
        Err(ureq::Error::Transport(t)) => format!("http 失败: {t}"),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::c_char;

    #[test]
    fn http_lib_vets() {
        let dsn = "fs:///tmp/kvlang-http-test";
        let kv = unsafe { kvspaceConnect(cs(dsn).as_ptr()) };
        assert!(!kv.is_null());
        let mut err = [0u8; 256];
        unsafe { kvspaceClear(kv, err.as_mut_ptr() as *mut c_char, 256) };
        let eng = Engine {
            rt: std::ptr::null_mut(),
            kv,
            dsn: dsn.to_string(),
        };
        let src = r#"
lib http {
    rwfunc get(url:[]char/utf32) -> (resp:[]char/utf32) {
        http·call("GET", "", url, "") -> resp
    }
}
"#;
        assert_eq!(crate::rwir::kvlanglayout::vet(&eng, src), "ok");
    }
}
