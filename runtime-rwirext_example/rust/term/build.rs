// 链接 C runtime（libkvlang_runtime.so，bin/）+ kvspace dispatch 前端（已安装 /usr/lib）。
// 产物统一在 kvlang/bin/；kvspace 后端由前端按 DSN 运行时选择。
fn main() {
    let manifest = env!("CARGO_MANIFEST_DIR");
    let bin = format!("{manifest}/../../../bin"); // kvlang/bin

    println!("cargo:rustc-link-search=native={bin}");
    println!("cargo:rustc-link-lib=dylib=kvlang_runtime");
    println!("cargo:rustc-link-lib=dylib=kvspace");

    println!("cargo:rustc-link-arg=-Wl,--disable-new-dtags"); // rpath 转 DT_RPATH，传递解析 kvspace 的子依赖
    println!("cargo:rustc-link-arg=-Wl,-rpath,$ORIGIN");
}
