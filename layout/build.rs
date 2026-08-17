// 链接 kvspace 动态库（cdylib）。布局侧只通过 extern "C" ABI 调用。
// 默认链接 kvspace-durable；KVLANG_KVSPACE_LIB=kvspace-c 时链接 kvspace-c（SHM，durable 兼容 ABI）。
fn main() {
    let lib = std::env::var("KVLANG_KVSPACE_LIB").unwrap_or_else(|_| "kvspace_durable".into());
    let (lib_dir, extra_rpath) = if lib == "kvspace-c" {
        ("/home/peng.li24/github.com/array2d/kvspace/build", true)
    } else {
        ("/home/peng.li24/github.com/array2d/kvspace-durable/target/release", false)
    };
    println!("cargo:rustc-link-search=native={lib_dir}");
    println!("cargo:rustc-link-lib=dylib={lib}");
    println!("cargo:rustc-link-arg=-Wl,-rpath,{lib_dir}");
    if extra_rpath {
        println!("cargo:rustc-link-arg=-Wl,-rpath,/home/peng.li24/github.com/array2d/blockmalloc/build");
        println!("cargo:rustc-link-arg=-Wl,-rpath,/home/peng.li24/github.com/array2d/slotsboxmalloc/build");
    }
}
