// 链接 kvspace dispatch 前端（已安装 /usr/lib）。布局侧只通过 extern "C" ABI 调用，
// 运行期由前端按 DSN 选后端（shm://→kvspace-c，其余→kvspace-durable）。
fn main() {
    println!("cargo:rustc-link-lib=dylib=kvspace");
}
