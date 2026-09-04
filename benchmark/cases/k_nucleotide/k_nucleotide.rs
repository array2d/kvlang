use std::collections::HashMap;
use std::time::Instant;

fn main() {
    let base = "GGTATTGAGCACTGGCAATTGACGTCAGGTATCCGAATTGCACGTTAGCATGCATGCATGCACGT";
    let scale: usize = std::env::var("BENCH_SCALE").unwrap().parse().unwrap();
    let s = base.repeat(scale);
    let mut h: HashMap<i64, i64> = HashMap::new();
    let t0 = Instant::now();
    for ch in s.chars() {
        let c = ch as i64;
        *h.entry(c).or_insert(0) += 1;
    }
    let ns = t0.elapsed().as_nanos();
    println!(
        "knuc: A = {} C = {} G = {} T = {}",
        h[&65], h[&67], h[&71], h[&84]
    );
    println!("__bench_ns: {}", ns);
}
