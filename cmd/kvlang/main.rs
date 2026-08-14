use std::env;
use kvlang::kvcpu::cpu::KVCpu;
use kvlang::kvcpu::execute;

fn main() {
    let func = env::args().nth(1).unwrap_or_else(|| "main".into());
    let shm = env::var("KVSPACE_SHM").expect("KVSPACE_SHM not set");
    let kv_ptr = KVCpu::open(&shm).expect("open shm");
    let cpu = KVCpu::new(kv_ptr, "rust");
    if let Err(e) = execute::execute(&cpu, &func) {
        kvlang::logx::logx::error(format_args!("{}", e));
        std::process::exit(1);
    }
    KVCpu::close(kv_ptr);
}
