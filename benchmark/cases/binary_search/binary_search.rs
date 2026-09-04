use std::time::Instant;

fn bsearch(arr: &[i64], n: i64, target: i64) -> i64 {
    let mut lo = 0i64;
    let mut hi = n - 1;
    let mut idx = -1i64;
    while lo <= hi {
        let mid = (lo + hi) / 2;
        let mv = arr[mid as usize];
        if mv == target {
            idx = mid;
            lo = hi + 1;
        } else if mv < target {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    idx
}

fn main() {
    let n: i64 = std::env::var("BENCH_SCALE").unwrap().parse().unwrap();
    let arr: Vec<i64> = (0..n).collect();
    let t0 = Instant::now();
    let mut sum = 0i64;
    let mut found = 0i64;
    for q in 0..n {
        let idx = bsearch(&arr, n, q);
        if idx != -1 {
            found += 1;
            sum += idx;
        }
    }
    let ns = t0.elapsed().as_nanos();
    println!("bsearch: found = {} sum = {}", found, sum);
    println!("__bench_ns: {}", ns);
}
