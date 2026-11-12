
fn main() {
    println!("cargo:rustc-link-lib=dylib=kvspace");
    println!("cargo:rerun-if-changed=build.rs");
}
