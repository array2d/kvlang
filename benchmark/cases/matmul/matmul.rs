use std::time::Instant;

fn main() {
    let n: usize = std::env::var("BENCH_SCALE").unwrap().parse().unwrap();
    let mut a = vec![0.0f64; n * n];
    let mut b = vec![0.0f64; n * n];
    for i in 0..n {
        for j in 0..n {
            let idx = i * n + j;
            a[idx] = ((i + j) % 4) as f64 * 0.25 + 0.25;
            b[idx] = ((i * 2 + j) % 4) as f64 * 0.25 + 0.25;
        }
    }
    let t0 = Instant::now();
    let mut checksum = 0.0f64;
    for i in 0..n {
        for j in 0..n {
            let mut acc = 0.0f64;
            for k in 0..n {
                acc += a[i * n + k] * b[k * n + j];
            }
            checksum += acc;
        }
    }
    let ns = t0.elapsed().as_nanos();
    let out = (checksum * 1000000.0) as i64;
    println!("matmul: check = {}", out);
    println!("__bench_ns: {}", ns);
}
