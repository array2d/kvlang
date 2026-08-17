// 链接 kvspace-durable 的动态库（cdylib）。
// 布局侧只通过 extern "C" ABI 调用 kvspace_durable，不依赖其 Rust API。
// 前置：先在 ../kvspace-durable 执行 `cargo build --release`，产出 libkvspace_durable.so。
fn main() {
    let lib_dir = "/home/peng.li24/github.com/array2d/kvspace-durable/target/release";
    println!("cargo:rustc-link-search=native={lib_dir}");
    println!("cargo:rustc-link-lib=dylib=kvspace_durable");
    println!("cargo:rustc-link-arg=-Wl,-rpath,{lib_dir}");
}
