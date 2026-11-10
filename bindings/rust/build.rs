// 链接两个库：kvspace 的存储 ABI（动态）+ kvlang 的 wire 构造器（静态）。
//
// **这里不解析任何路径**：全生态只允许 `KVSPACE` 一个环境变量，而 build.rs 也
// 收不到命令行参数。外部路径由调用方写进 Cargo 配置文件：
//
//   bindings/rust/.cargo/config.toml           （该文件被 gitignore）
//     [target.'cfg(all())']
//     rustflags = [
//       "-L", "native=/path/to/kvspace/build",
//       "-C", "link-arg=-Wl,-rpath,/path/to/kvspace/build",
//       "-C", "link-arg=/path/to/kvlang/lib/libkvlang_xvalue.a",
//     ]
//
// build.rs 只声明「要链 kvspace」；路径找不到时 rustc 的链接错误会点名缺哪个库
// ——这比在这里猜一个目录更诚实。语言层静态库用全路径传，`-lkvlang_xvalue`
// 会去系统目录找错对象，所以由配置文件直接给 `-C link-arg=<...>.a`。
fn main() {
    println!("cargo:rustc-link-lib=dylib=kvspace");
    println!("cargo:rerun-if-changed=build.rs");
}
