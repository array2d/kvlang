// 链接 kvspace dispatch 前端：产物只放 repo 内 <root>/{bin,lib}（不装 /usr）。
//   <root>/lib/libkvspace.so                dispatch 前端（与 backend 同装 lib/）
//   <root>/lib/kvspace/libkvspace-c.so      后端：shm
//   <root>/lib/kvspace/libkvspace_durable.so 后端：redis/fs/s3
// 链接期用 repo 内绝对路径定位，运行期用相对自身的 $ORIGIN 解 rpath，
// 故 tarball 放到任意 prefix（bin/ + lib/ + lib/kvspace/）都可运行。
fn main() {
    let manifest = std::env::var("CARGO_MANIFEST_DIR").expect("CARGO_MANIFEST_DIR");
    let root = format!("{manifest}/..");
    let lib = format!("{root}/lib");
    let backend = format!("{lib}/kvspace");

    println!("cargo:rustc-link-search=native={lib}");
    println!("cargo:rustc-link-lib=dylib=kvspace");

    let origin = if cfg!(target_os = "macos") {
        "@loader_path"
    } else {
        "$ORIGIN"
    };
    // 可执行在 bin/（../lib），cdylib 自身在 lib/（$ORIGIN）。两条都写，
    // 使同一 build.rs 产出的两类货都对。
    println!("cargo:rustc-link-arg=-Wl,-rpath,{origin}");
    println!("cargo:rustc-link-arg=-Wl,-rpath,{origin}/../lib");
    println!("cargo:rustc-link-arg=-Wl,-rpath,{origin}/kvspace");
    println!("cargo:rustc-link-arg=-Wl,-rpath,{origin}/../lib/kvspace");
    if !cfg!(target_os = "macos") {
        // rpath 转 DT_RPATH，传递解析子依赖；ld64 无 --disable-new-dtags
        println!("cargo:rustc-link-arg=-Wl,--disable-new-dtags");
    }
    println!("cargo:rerun-if-changed={backend}");
}
