// 链接 kvspace dispatch 前端（与后端同装 <prefix>/lib/kvspace）。布局侧只通过 extern "C" ABI 调用，
// 运行期由前端按 DSN 选后端（shm://→kvspace-c，其余→kvspace-durable）。
// KVSPACE_LIB_DIR 可覆盖安装目录：macOS 的 /usr 受 SIP 保护，应指向 <prefix>/lib/kvspace。
fn main() {
    let dir = std::env::var("KVSPACE_LIB_DIR").unwrap_or_else(|_| "/usr/lib/kvspace".into());
    println!("cargo:rustc-link-search=native={dir}");
    println!("cargo:rustc-link-lib=dylib=kvspace");
    println!("cargo:rustc-link-arg=-Wl,-rpath,{dir}");
    // 相对自身位置解析：tarball 里 <root>/bin 与 <root>/lib 相邻，装到 <prefix>/bin
    // 时也成立（Linux 前端在 <prefix>/lib，macOS 在 <prefix>/lib/kvspace）。
    let origin = if cfg!(target_os = "macos") {
        "@loader_path"
    } else {
        "$ORIGIN"
    };
    println!("cargo:rustc-link-arg=-Wl,-rpath,{origin}/../lib");
    println!("cargo:rustc-link-arg=-Wl,-rpath,{origin}/../lib/kvspace");
    if !cfg!(target_os = "macos") {
        // rpath 转 DT_RPATH，传递解析子依赖；ld64 无 --disable-new-dtags
        println!("cargo:rustc-link-arg=-Wl,--disable-new-dtags");
    }
}
