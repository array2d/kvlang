// 链接 kvspace 动态库（cdylib）。布局侧只通过 extern "C" ABI 调用。
// 依赖 .so 由 ci/deps.sh 下载到 libso/lib；默认 kvspace-durable，KVLANG_KVSPACE_LIB=kvspace-c 时用 SHM 版。
fn main() {
    println!("cargo:rerun-if-env-changed=KVLANG_KVSPACE_LIB");
    let lib = std::env::var("KVLANG_KVSPACE_LIB").unwrap_or_else(|_| "kvspace_durable".into());
    let libso = format!("{}/../libso/lib", env!("CARGO_MANIFEST_DIR")); // kvlang/libso/lib
    println!("cargo:rustc-link-search=native={libso}");
    println!("cargo:rustc-link-lib=dylib={lib}");
    println!("cargo:rustc-link-arg=-Wl,-rpath,{libso}");
    println!("cargo:rustc-link-arg=-Wl,--disable-new-dtags");
}
