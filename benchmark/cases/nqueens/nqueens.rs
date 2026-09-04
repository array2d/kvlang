use std::time::Instant;

fn nq(cols: i64, d1: i64, d2: i64, all: i64) -> i64 {
    if cols == all {
        return 1;
    }
    let mut cnt = 0;
    let mut avail = ((cols | d1 | d2) & all) ^ all;
    while avail != 0 {
        let p = avail & (-avail);
        avail ^= p;
        cnt += nq(cols | p, (d1 | p) << 1, (d2 | p) >> 1, all);
    }
    cnt
}

fn main() {
    let scale: i64 = std::env::var("BENCH_SCALE").unwrap().parse().unwrap();
    let t0 = Instant::now();
    let all = (1i64 << scale) - 1;
    let ans = nq(0, 0, 0, all);
    let ns = t0.elapsed().as_nanos();
    println!("queens = {}", ans);
    println!("__bench_ns: {}", ns);
}
