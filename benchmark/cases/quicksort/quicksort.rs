use std::time::Instant;

fn qsort(a: &mut [i64], lo: i64, hi: i64) {
    if lo < hi {
        let pivot = a[hi as usize];
        let mut i = lo - 1;
        let mut j = lo;
        while j < hi {
            if a[j as usize] <= pivot {
                i += 1;
                a.swap(i as usize, j as usize);
            }
            j += 1;
        }
        a.swap((i + 1) as usize, hi as usize);
        let p = i + 1;
        qsort(a, lo, p - 1);
        qsort(a, p + 1, hi);
    }
}

fn main() {
    let n: i64 = std::env::var("BENCH_SCALE").unwrap().parse().unwrap();
    let mut arr: Vec<i64> = Vec::new();
    let mut seed: i64 = 1;
    for _ in 0..n {
        seed = (seed * 1103515245 + 12345) % 2147483648;
        arr.push(seed % 100);
    }
    let t0 = Instant::now();
    qsort(&mut arr, 0, n - 1);
    let ns = t0.elapsed().as_nanos();
    println!("qsort: a0 = {} amid = {} alast = {}", arr[0], arr[(n / 2) as usize], arr[(n - 1) as usize]);
    println!("__bench_ns: {}", ns);
}
