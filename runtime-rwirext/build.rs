fn main() {
    let rt = "/home/peng.li24/github.com/array2d/kvlang/runtime/build";
    let dual = "/home/peng.li24/github.com/array2d/kvspace-durable/target/release";
    println!("cargo:rustc-link-search=native={rt}");
    println!("cargo:rustc-link-lib=dylib=kvlang_runtime");
    println!("cargo:rustc-link-arg=-Wl,-rpath,{rt}");
    println!("cargo:rustc-link-arg=-Wl,-rpath,{dual}");
}
