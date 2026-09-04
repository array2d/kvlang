use std::time::Instant;

fn fib(n: i64) -> i64 {
    if n <= 1 {
        return n;
    }
    fib(n - 1) + fib(n - 2)
}

fn main() {
    let scale: i64 = std::env::var("BENCH_SCALE").unwrap().parse().unwrap();
    let t0 = Instant::now();
    let ans = fib(scale);
    let ns = t0.elapsed().as_nanos();
    println!("fib = {}", ans);
    println!("__bench_ns: {}", ns);
}
