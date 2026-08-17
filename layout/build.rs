// 链接 kvspace 动态库（cdylib）。布局侧只通过 extern "C" ABI 调用。
// 默认链接 kvspace-durable；KVLANG_KVSPACE_LIB=kvspace-c 时链接 kvspace-c（SHM，durable 兼容 ABI）。
// 依赖路径从 CARGO_MANIFEST_DIR 推导（array2d 工作区根），不 hardcode 绝对路径。
fn main() {
    println!("cargo:rerun-if-env-changed=KVLANG_KVSPACE_LIB");
    let lib = std::env::var("KVLANG_KVSPACE_LIB").unwrap_or_else(|_| "kvspace_durable".into());
    let root = format!("{}/../..", env!("CARGO_MANIFEST_DIR")); // array2d 工作区根
    let (lib_dir, extra) = if lib == "kvspace-c" {
        (format!("{root}/kvspace/build"), true)
    } else {
        (format!("{root}/kvspace-durable/target/release"), false)
    };
    println!("cargo:rustc-link-search=native={lib_dir}");
    println!("cargo:rustc-link-lib=dylib={lib}");
    // --disable-new-dtags 使 rpath 转 DT_RPATH（传递），让 kvspace-c → blockmalloc/slotsboxmalloc
    // 的传递依赖能被 layout 的 rpath 找到。
    println!("cargo:rustc-link-arg=-Wl,-rpath,{lib_dir}");
    println!("cargo:rustc-link-arg=-Wl,--disable-new-dtags");
    if extra {
        println!("cargo:rustc-link-arg=-Wl,-rpath,{root}/blockmalloc/build");
        println!("cargo:rustc-link-arg=-Wl,-rpath,{root}/slotsboxmalloc/build");
    }
}
