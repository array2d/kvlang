// 链接 kvspace dispatch 前端（与后端同装 /usr/lib/kvspace）。布局侧只通过 extern "C" ABI 调用，
// 运行期由前端按 DSN 选后端（shm://→kvspace-c，其余→kvspace-durable）。
fn main() {
    println!("cargo:rustc-link-search=native=/usr/lib/kvspace");
    println!("cargo:rustc-link-lib=dylib=kvspace");
    println!("cargo:rustc-link-arg=-Wl,--disable-new-dtags"); // rpath 转 DT_RPATH，传递解析子依赖
    println!("cargo:rustc-link-arg=-Wl,-rpath,/usr/lib/kvspace");
}
