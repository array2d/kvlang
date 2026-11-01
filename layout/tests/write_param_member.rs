//! 容器写参在函数体里以成员路径（`h·0` 静态键 / `h·*i` 动态键）出现时，layout 必须为它
//! 发一条参数寄存器：runtime 的成员写按寄存器找 base，缺寄存器就会写进被调帧本地。
//! 断言：函数段里既有参数寄存器 `*[0,+1]`（入口拷贝的读槽），也有以它为 base 的写槽。

use kvlanglayout::{compile, init_dirs, kvkind, Kv};

fn fresh_kv(tag: &str) -> Kv {
    let dsn = format!(
        "fs:///tmp/kvlanglayout_wpmember_{}_{}_{}",
        tag,
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos()
    );
    let mut kv = Kv::conn(&dsn);
    init_dirs(&mut kv).unwrap();
    kv
}

fn slot_text(kv: &mut Kv, key: &str) -> String {
    let v = kv.get_one(key);
    let lt = kv.langtype_of(key);
    let h = kvkind::head(&v);
    let b = kvkind::body(&v, &h);
    String::from_utf8_lossy(&b[5.min(b.len())..]).into_owned()
}

fn slot_texts(kv: &mut Kv, func: &str) -> Vec<String> {
    let prefix = format!("/lib/{func}/");
    let children = kv.list(&prefix, false, false);
    children
        .iter()
        .filter(|c| c.trim_end_matches('/').starts_with('['))
        .map(|c| slot_text(kv, &format!("{prefix}{c}")))
        .collect()
}

const STATIC_KEY: &str =
    "rwfunc fill() -> (h:*[int64]·int64) {\n    1 -> h·0\n    2 -> h·1\n}\n";

const DYNAMIC_KEY: &str = "rwfunc fill(n:int64) -> (h:*[int64]·int64) {\n    i:int64 = 0\n    while (i < n) {\n        i -> h·*i\n        i + 1 -> i\n    }\n}\n";

#[test]
fn static_member_key_writes_param_register() {
    let mut kv = fresh_kv("static");
    compile(&mut kv, STATIC_KEY).unwrap();
    let texts = slot_texts(&mut kv, "fill");
    assert!(
        texts.iter().any(|t| t.starts_with("*[0,1]")),
        "缺参数寄存器 *[0,1]：{texts:?}"
    );
    assert!(
        texts.iter().any(|t| t == "*[0,1]·0"),
        "静态键写槽未以参数寄存器为 base：{texts:?}"
    );
}

#[test]
fn dynamic_member_key_writes_param_register() {
    let mut kv = fresh_kv("dynamic");
    compile(&mut kv, DYNAMIC_KEY).unwrap();
    let texts = slot_texts(&mut kv, "fill");
    assert!(
        texts.iter().any(|t| t.starts_with("*[0,1]")),
        "缺参数寄存器 *[0,1]：{texts:?}"
    );
    assert!(
        texts.iter().any(|t| t == "*[0,1]·*i"),
        "动态键写槽未以参数寄存器为 base：{texts:?}"
    );
}
