// 链接 repo 内的 kvspace 前端（与 layout 同一套定位方式）：
//   <root>/lib/libkvspace.so
// 运行期用 $ORIGIN 相对解析，产物放到任意 prefix 都能跑。
fn main() {
    let manifest = std::env::var("CARGO_MANIFEST_DIR").expect("CARGO_MANIFEST_DIR");
    let lib = format!("{manifest}/../lib");

    println!("cargo:rustc-link-search=native={lib}");
    println!("cargo:rustc-link-lib=dylib=kvspace");

    let origin = if cfg!(target_os = "macos") {
        "@loader_path"
    } else {
        "$ORIGIN"
    };
    println!("cargo:rustc-link-arg=-Wl,-rpath,{origin}");
    println!("cargo:rustc-link-arg=-Wl,-rpath,{origin}/../lib");
    println!("cargo:rustc-link-arg=-Wl,-rpath,{origin}/kvspace");
    println!("cargo:rustc-link-arg=-Wl,-rpath,{origin}/../lib/kvspace");
    if !cfg!(target_os = "macos") {
        println!("cargo:rustc-link-arg=-Wl,--disable-new-dtags");
    }
}
