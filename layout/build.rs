// 链接 kvspace dispatch 前端（与后端同装 <prefix>/lib/kvspace）。布局侧只通过 extern "C" ABI 调用，
// 运行期由前端按 DSN 选后端（shm://→kvspace-c，其余→kvspace-durable）。
// 安装目录固定（无构建期覆盖变量）：Linux /usr/lib + /usr/lib/kvspace；
// macOS /usr/local/lib + /usr/local/lib/kvspace（/usr 受 SIP 保护）。
fn main() {
    let (lib, backend) = if cfg!(target_os = "macos") {
        ("/usr/local/lib", "/usr/local/lib/kvspace")
    } else {
        ("/usr/lib", "/usr/lib/kvspace")
    };
    println!("cargo:rustc-link-search=native={lib}");
    println!("cargo:rustc-link-search=native={backend}");
    println!("cargo:rustc-link-lib=dylib=kvspace");
    println!("cargo:rustc-link-arg=-Wl,-rpath,{lib}");
    println!("cargo:rustc-link-arg=-Wl,-rpath,{backend}");
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
