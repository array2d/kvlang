use std::time::Instant;

fn iops(n: i64) -> i64 {
    let mut a = 0;
    let mut i = 1;
    while i <= n {
        a = a + 1;
        i = i + 1;
    }
    a
}

fn main() {
    let scale: i64 = std::env::var("BENCH_SCALE").unwrap().parse().unwrap();
    let t0 = Instant::now();
    let ans = iops(scale);
    let ns = t0.elapsed().as_nanos();
    println!("iops a = {}", ans);
    println!("__bench_ns: {}", ns);
}
