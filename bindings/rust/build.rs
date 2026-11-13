
fn main() {
    println!("cargo:rustc-link-lib=dylib=pathspace");
    println!("cargo:rerun-if-changed=build.rs");
}
