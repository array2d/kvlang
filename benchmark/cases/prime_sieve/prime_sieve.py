import os
import time


def prime_sieve(limit: int) -> None:
    print("primes up to", limit)
    count = 0
    n = 2
    while n <= limit:
        is_prime = True
        d = 2
        while d < n:
            if n % d == 0:
                is_prime = False
                break
            d += 1
        if is_prime:
            print("  prime:", n)
            count += 1
        n += 1
    print("total primes up to", limit, "=", count)


t0 = time.perf_counter_ns()
prime_sieve(int(os.environ["BENCH_SCALE"]))
t1 = time.perf_counter_ns()
print("__bench_ns:", t1 - t0)
