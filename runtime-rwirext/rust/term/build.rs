// 链接 C runtime（libkvlang_runtime.so）。产物统一在 kvlang/bin/，
// 依赖路径从 CARGO_MANIFEST_DIR 推导，不 hardcode 绝对路径。
fn main() {
    let bin = format!("{}/../../../bin", env!("CARGO_MANIFEST_DIR")); // kvlang/bin
    println!("cargo:rustc-link-search=native={bin}");
    println!("cargo:rustc-link-lib=dylib=kvlang_runtime");
    println!("cargo:rustc-link-arg=-Wl,-rpath,$ORIGIN");
}
