use std::time::Instant;

fn prime_sieve(limit: i64) {
    println!("primes up to {}", limit);
    let mut count = 0;
    let mut n = 2;
    while n <= limit {
        let mut is_prime = true;
        let mut d = 2;
        while d < n {
            if n % d == 0 {
                is_prime = false;
                break;
            }
            d += 1;
        }
        if is_prime {
            println!("  prime: {}", n);
            count += 1;
        }
        n += 1;
    }
    println!("total primes up to {} = {}", limit, count);
}

fn main() {
    let scale: i64 = std::env::var("BENCH_SCALE").unwrap().parse().unwrap();
    let t0 = Instant::now();
    prime_sieve(scale);
    let ns = t0.elapsed().as_nanos();
    println!("__bench_ns: {}", ns);
}
