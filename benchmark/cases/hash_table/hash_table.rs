use std::collections::HashMap;
use std::time::Instant;

fn main() {
    let n: i64 = std::env::var("BENCH_SCALE").unwrap().parse().unwrap();
    let mut h: HashMap<i64, i64> = HashMap::new();
    let t0 = Instant::now();
    for i in 0..n {
        let key = (i * 2654435761) % 100003;
        h.insert(key, i);
    }
    let mut sum = 0i64;
    let mut hits = 0i64;
    for i in 0..n {
        let key = (i * 2654435761) % 100003;
        if let Some(v) = h.get(&key) {
            hits += 1;
            sum += *v;
        }
    }
    let ns = t0.elapsed().as_nanos();
    println!("hash: hits = {} sum = {}", hits, sum);
    println!("__bench_ns: {}", ns);
}
