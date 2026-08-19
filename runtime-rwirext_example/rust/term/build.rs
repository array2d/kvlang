// 链接 C runtime（libkvlang_runtime.so）+ kvspace 后端（扩展宿主自连 kvspace）。
// 产物统一在 kvlang/bin/，后端由 KVLANG_KVSPACE_LIB 选（对齐 runtime/layout），不 hardcode 绝对路径。
fn main() {
    println!("cargo:rerun-if-env-changed=KVLANG_KVSPACE_LIB");
    let manifest = env!("CARGO_MANIFEST_DIR");
    let bin = format!("{manifest}/../../../bin"); // kvlang/bin
    let array2d = format!("{manifest}/../../../.."); // array2d 工作区

    println!("cargo:rustc-link-search=native={bin}");
    println!("cargo:rustc-link-lib=dylib=kvlang_runtime");

    let backend = std::env::var("KVLANG_KVSPACE_LIB").unwrap_or_else(|_| "kvspace-c".to_string());
    let (lib, dirs) = if backend == "kvspace_durable" {
        ("kvspace_durable", vec![format!("{array2d}/kvspace-durable/target/release")])
    } else {
        // kvspace-c 依赖 blockmalloc/slotsboxmalloc，需一并入 rpath（DT_RPATH 传递）
        (
            "kvspace-c",
            vec![
                format!("{array2d}/kvspace-c/build"),
                format!("{array2d}/blockmalloc/build"),
                format!("{array2d}/slotsboxmalloc/build"),
            ],
        )
    };
    println!("cargo:rustc-link-search=native={}", dirs[0]);
    println!("cargo:rustc-link-lib=dylib={lib}");

    println!("cargo:rustc-link-arg=-Wl,--disable-new-dtags"); // rpath 转 DT_RPATH，传递解析 kvspace 的子依赖
    println!("cargo:rustc-link-arg=-Wl,-rpath,$ORIGIN");
    for d in &dirs {
        println!("cargo:rustc-link-arg=-Wl,-rpath,{d}");
    }
}
