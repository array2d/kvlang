
fn main() {
    let manifest = std::env::var("CARGO_MANIFEST_DIR").expect("CARGO_MANIFEST_DIR");
    let root = format!("{manifest}/..");
    let lib = format!("{root}/lib");
    let backend = format!("{lib}/pathspace");

    println!("cargo:rustc-link-search=native={lib}");
    println!("cargo:rustc-link-lib=dylib=pathspace");
    
    println!("cargo:rustc-link-lib=static=pathlang_xvalue");
    
    let origin = if cfg!(target_os = "macos") {
        "@loader_path"
    } else {
        "$ORIGIN"
    };
    
    println!("cargo:rustc-link-arg=-Wl,-rpath,{origin}");
    println!("cargo:rustc-link-arg=-Wl,-rpath,{origin}/../lib");
    println!("cargo:rustc-link-arg=-Wl,-rpath,{origin}/pathspace");
    println!("cargo:rustc-link-arg=-Wl,-rpath,{origin}/../lib/pathspace");
    if !cfg!(target_os = "macos") {
        
        println!("cargo:rustc-link-arg=-Wl,--disable-new-dtags");
    }
    println!("cargo:rerun-if-changed={backend}");
}
