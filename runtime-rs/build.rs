// 链接 stock 三方 .so（camelCase，符合 deepx-design/doc/abi-naming-standard.md）：
//   kvlang_runtime  —— 模式2 主导执行 + rwirext 宿主 ABI（kvlang/bin 新构建优先，回落 /usr/lib）
//   kvspace         —— dispatch 前端，按 DSN 运行时选后端（/usr/lib）
//   kvlanglayout    —— .kv 编译入库（kvlanglayout·* rwir + 启动 layout stdlib 用，/usr/lib）
// 并把 stdlib/**/*.kv（顶层语言级标准库）全部 include_str! 进二进制（EMBEDDED_KV），启动时 layout 进 kvspace。
use std::path::{Path, PathBuf};

fn collect_kv(dir: &Path, files: &mut Vec<PathBuf>) {
    let Ok(entries) = std::fs::read_dir(dir) else {
        return;
    };
    for e in entries.flatten() {
        let p = e.path();
        if p.is_dir() {
            collect_kv(&p, files);
        } else if p.extension().is_some_and(|x| x == "kv") {
            files.push(p);
        }
    }
}

fn main() {
    let manifest = env!("CARGO_MANIFEST_DIR");
    let bin = format!("{manifest}/../bin"); // kvlang/bin（新构建的 libkvlang_runtime.so）

    println!("cargo:rustc-link-search=native={bin}");
    println!("cargo:rustc-link-search=native=/usr/lib");
    println!("cargo:rustc-link-lib=dylib=kvlang_runtime");
    println!("cargo:rustc-link-lib=dylib=kvspace");
    println!("cargo:rustc-link-lib=dylib=kvlanglayout");

    println!("cargo:rustc-link-arg=-Wl,--disable-new-dtags"); // rpath 转 DT_RPATH，传递解析子依赖
    println!("cargo:rustc-link-arg=-Wl,-rpath,$ORIGIN");
    println!("cargo:rustc-link-arg=-Wl,-rpath,$ORIGIN/../lib");
    println!("cargo:rustc-link-arg=-Wl,-rpath,/usr/lib");

    // 内嵌顶层 stdlib/**/*.kv → EMBEDDED_KV。
    let libdir = format!("{manifest}/../stdlib");
    let mut files: Vec<PathBuf> = Vec::new();
    collect_kv(Path::new(&libdir), &mut files);
    files.sort();
    let mut code = String::from("pub static EMBEDDED_KV: &[(&str, &str)] = &[\n");
    for p in &files {
        let rel = p.strip_prefix(&libdir).unwrap_or(p);
        let name = rel.to_string_lossy().trim_start_matches('/').to_string();
        let abs = p.to_string_lossy();
        code.push_str(&format!("    ({name:?}, include_str!({abs:?})),\n"));
        println!("cargo:rerun-if-changed={abs}");
    }
    code.push_str("];\n");
    let out = std::env::var("OUT_DIR").unwrap();
    std::fs::write(format!("{out}/embedded_kv.rs"), code).unwrap();
    println!("cargo:rerun-if-changed={libdir}");
}
