fn main() {
    println!("cargo:rustc-link-lib=dylib=kvspace");
    println!("cargo:rustc-link-lib=dylib=kvlang_xvalue");
    println!("cargo:rerun-if-changed=build.rs");
}
